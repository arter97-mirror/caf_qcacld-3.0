/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * DOC: N79-WLAN 5 GHz antenna sharing coexistence — core implementation
 */

#include "wlan_n79_coex.h"
#include "wlan_coex_tgt_api.h"
#include "wlan_objmgr_psoc_obj.h"
#include "wlan_objmgr_vdev_obj.h"
#include "wlan_mlme_vdev_mgr_interface.h"
#include "wlan_mlme_main.h"
#include "qdf_mem.h"
#include "wmi_unified_param.h"
#include "wlan_tdls_api.h"
#include "scheduler_api.h"
#include "cfg_ucfg_api.h"

/**
 * wlan_coex_n79_psoc_release_ref_cb() - flush callback for
 * psoc-level N79 scheduler messages
 * @msg: scheduler message being flushed
 *
 * Releases the psoc ref taken when the message was posted.
 *
 * Return: QDF_STATUS_SUCCESS
 */
static QDF_STATUS wlan_coex_n79_psoc_release_ref_cb(struct scheduler_msg *msg)
{
	wlan_objmgr_psoc_release_ref(msg->bodyptr, WLAN_COEX_ID);
	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_coex_n79_vdev_release_ref_cb() - flush callback for
 * vdev-level N79 scheduler messages
 * @msg: scheduler message being flushed
 *
 * Releases the vdev ref taken when the message was posted.
 *
 * Return: QDF_STATUS_SUCCESS
 */
static QDF_STATUS wlan_coex_n79_vdev_release_ref_cb(struct scheduler_msg *msg)
{
	wlan_objmgr_vdev_release_ref(msg->bodyptr, WLAN_COEX_ID);
	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_coex_n79_vdev_active_handler() - scheduler message callback for per-vdev
 * N79-active event
 * @msg: scheduler message; bodyptr is struct wlan_objmgr_vdev
 *
 * Calls wlan_coex_n79_apply_active_vdev() for the specific vdev. Guards on
 * n79_coex_active to handle the race where N79 deactivates before this
 * message is dequeued. Releases the vdev ref on exit.
 *
 * Return: QDF_STATUS_SUCCESS
 */
static QDF_STATUS wlan_coex_n79_vdev_active_handler(struct scheduler_msg *msg)
{
	struct wlan_objmgr_vdev *vdev = msg->bodyptr;
	struct wlan_objmgr_psoc *psoc;
	struct coex_psoc_obj *psoc_obj;

	psoc = wlan_vdev_get_psoc(vdev);
	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || !qdf_atomic_read(&psoc_obj->n79_coex_active))
		goto done;

	wlan_coex_n79_apply_active_vdev(psoc, vdev, NULL);
done:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_COEX_ID);
	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_coex_n79_post_vdev_active_cmd() - take vdev ref and schedule
 * per-vdev N79-active WMI work on the driver scheduler queue
 * @vdev: vdev object
 *
 * On post failure the ref is released before returning.
 *
 * Return: QDF_STATUS_SUCCESS on success; error otherwise
 */
static QDF_STATUS
wlan_coex_n79_post_vdev_active_cmd(struct wlan_objmgr_vdev *vdev)
{
	struct scheduler_msg msg = {0};
	QDF_STATUS status;

	if (wlan_objmgr_vdev_try_get_ref(vdev, WLAN_COEX_ID) !=
	    QDF_STATUS_SUCCESS)
		return QDF_STATUS_E_FAILURE;

	msg.callback       = wlan_coex_n79_vdev_active_handler;
	msg.bodyptr        = vdev;
	msg.flush_callback = wlan_coex_n79_vdev_release_ref_cb;

	status = scheduler_post_message(QDF_MODULE_ID_COEX,
					QDF_MODULE_ID_COEX,
					QDF_MODULE_ID_OS_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status))
		wlan_objmgr_vdev_release_ref(vdev, WLAN_COEX_ID);

	return status;
}

static inline void
wlan_coex_n79_restore_vdev_iter(struct wlan_objmgr_psoc *psoc,
				void *object, void *arg)
{
	wlan_coex_n79_restore_vdev(psoc,
				   (struct wlan_objmgr_vdev *)object, arg);
}

/**
 * wlan_coex_n79_ss_restore_handler() - scheduler message callback
 * for SS hysteresis timer expiry
 * @msg: scheduler message; bodyptr is struct wlan_objmgr_psoc
 *
 * Called when the 30-minute spatial-stream hysteresis timer fires. Iterates
 * all vdevs and calls wlan_coex_n79_restore_vdev(). Guards on
 * n79_coex_active to handle the race where N79 re-activates before this
 * message is dequeued.
 *
 * Return: QDF_STATUS_SUCCESS
 */
static QDF_STATUS wlan_coex_n79_ss_restore_handler(struct scheduler_msg *msg)
{
	struct wlan_objmgr_psoc *psoc = msg->bodyptr;
	struct coex_psoc_obj *psoc_obj;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || qdf_atomic_read(&psoc_obj->n79_coex_active))
		goto done;

	coex_debug("N79 SS timer: restore NSS");
	wlan_objmgr_iterate_obj_list(psoc, WLAN_VDEV_OP,
				     wlan_coex_n79_restore_vdev_iter,
				     NULL, 0, WLAN_COEX_ID);

done:
	wlan_objmgr_psoc_release_ref(psoc, WLAN_COEX_ID);
	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_coex_n79_rxdiv_restore_handler() - scheduler message callback
 * for RxDiv hysteresis timer expiry
 * @msg: scheduler message; bodyptr is struct wlan_objmgr_psoc
 *
 * Called when the 10-second Rx-diversity hysteresis timer fires. Iterates
 * all vdevs and calls wlan_coex_n79_restore_vdev(). Guards on
 * n79_coex_active to handle the race where N79 re-activates before this
 * message is dequeued.
 *
 * Return: QDF_STATUS_SUCCESS
 */
static QDF_STATUS wlan_coex_n79_rxdiv_restore_handler(struct scheduler_msg *msg)
{
	struct wlan_objmgr_psoc *psoc = msg->bodyptr;
	struct coex_psoc_obj *psoc_obj;
	bool chains_only = true;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || qdf_atomic_read(&psoc_obj->n79_coex_active))
		goto done;

	coex_debug("N79 RxDiv timer: restore chains");
	wlan_objmgr_iterate_obj_list(psoc, WLAN_VDEV_OP,
				     wlan_coex_n79_restore_vdev_iter,
				     &chains_only, 0, WLAN_COEX_ID);

done:
	wlan_objmgr_psoc_release_ref(psoc, WLAN_COEX_ID);
	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_coex_n79_post_cmd() - take psoc ref and post a message to OS_IF queue
 * @psoc: psoc object
 * @handler: scheduler callback to invoke on the OS_IF thread
 *
 * Used by timer callbacks for the restore path. On post failure the ref is
 * released before returning.
 *
 * Return: QDF_STATUS_SUCCESS on success; error otherwise
 */
static QDF_STATUS
wlan_coex_n79_post_cmd(struct wlan_objmgr_psoc *psoc,
		       scheduler_msg_process_fn_t handler)
{
	struct scheduler_msg msg = {0};
	QDF_STATUS status;

	if (wlan_objmgr_psoc_try_get_ref(psoc, WLAN_COEX_ID) !=
	    QDF_STATUS_SUCCESS)
		return QDF_STATUS_E_FAILURE;

	msg.callback       = handler;
	msg.bodyptr        = psoc;
	msg.flush_callback = wlan_coex_n79_psoc_release_ref_cb;

	status = scheduler_post_message(QDF_MODULE_ID_COEX,
					QDF_MODULE_ID_COEX,
					QDF_MODULE_ID_OS_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status))
		wlan_objmgr_psoc_release_ref(psoc, WLAN_COEX_ID);

	return status;
}

/**
 * wlan_coex_n79_ss_timer_cb() - softirq callback for SS hysteresis timer
 * @arg: psoc pointer registered at timer init
 *
 * Runs in softirq context; must not call WMI or sleep. Posts
 * wlan_coex_n79_ss_restore_handler to the OS_IF queue. Skips posting
 * if N79 was re-activated before the timer fired.
 */
static void wlan_coex_n79_ss_timer_cb(void *arg)
{
	struct wlan_objmgr_psoc *psoc = arg;
	struct coex_psoc_obj *psoc_obj;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || qdf_atomic_read(&psoc_obj->n79_coex_active))
		return;

	wlan_coex_n79_post_cmd(psoc, wlan_coex_n79_ss_restore_handler);
}

/**
 * wlan_coex_n79_rxdiv_timer_cb() - softirq callback for RxDiv hysteresis timer
 * @arg: psoc pointer registered at timer init
 *
 * Runs in softirq context; must not call WMI or sleep. Posts
 * wlan_coex_n79_rxdiv_restore_handler to the OS_IF queue. Skips posting
 * if N79 was re-activated before the timer fired.
 */
static void wlan_coex_n79_rxdiv_timer_cb(void *arg)
{
	struct wlan_objmgr_psoc *psoc = arg;
	struct coex_psoc_obj *psoc_obj;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || qdf_atomic_read(&psoc_obj->n79_coex_active))
		return;

	wlan_coex_n79_post_cmd(psoc, wlan_coex_n79_rxdiv_restore_handler);
}

QDF_STATUS wlan_coex_n79_psoc_init(struct coex_psoc_obj *psoc_obj,
				   struct wlan_objmgr_psoc *psoc)
{
	psoc_obj->psoc = psoc;
	qdf_atomic_init(&psoc_obj->n79_coex_active);
	psoc_obj->n79_ss_ms = cfg_get(psoc, CFG_N79_SS_HYSTERESIS_TIMER) * 1000;
	psoc_obj->n79_rx_div_ms  =
		cfg_get(psoc, CFG_N79_RX_DIV_HYSTERESIS_TIMER) * 1000;
	psoc_obj->n79_coex_policy =
		(enum n79_coex_policy)cfg_get(psoc, CFG_N79_COEX_POLICY);

	/* Limits are only meaningful for 2x2 policy; zero for others */
	if (psoc_obj->n79_coex_policy == N79_COEX_POLICY_2X2) {
		psoc_obj->n79_limit_rx_nss   = 2;
		psoc_obj->n79_limit_tx_nss   = 2;
		psoc_obj->n79_limit_rx_chain = 2;
		psoc_obj->n79_limit_tx_chain = 2;
	}

	qdf_timer_init(NULL, &psoc_obj->n79_ss_hysteresis_timer,
		       wlan_coex_n79_ss_timer_cb, psoc,
		       QDF_TIMER_TYPE_SW);

	qdf_timer_init(NULL, &psoc_obj->n79_rx_div_hysteresis_timer,
		       wlan_coex_n79_rxdiv_timer_cb, psoc,
		       QDF_TIMER_TYPE_SW);

	coex_debug("N79 psoc timers initialised");
	return QDF_STATUS_SUCCESS;
}

void wlan_coex_n79_psoc_deinit(struct coex_psoc_obj *psoc_obj)
{
	qdf_timer_stop(&psoc_obj->n79_ss_hysteresis_timer);
	qdf_timer_free(&psoc_obj->n79_ss_hysteresis_timer);
	qdf_timer_stop(&psoc_obj->n79_rx_div_hysteresis_timer);
	qdf_timer_free(&psoc_obj->n79_rx_div_hysteresis_timer);
	psoc_obj->psoc = NULL;
}

void wlan_coex_n79_vdev_init(struct coex_vdev_obj *vdev_obj)
{
	qdf_mem_zero(vdev_obj, sizeof(*vdev_obj));
}

void wlan_coex_n79_apply_active_vdev(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_vdev *vdev,
				     void *arg)
{
	struct coex_vdev_obj *vdev_obj;
	struct coex_psoc_obj *psoc_obj;
	struct wlan_mlme_nss_chains params;
	struct wlan_mlme_nss_chains *dyn_cfg;
	uint8_t cap_rx_nss = 0, cap_tx_nss = 0, op_rx_nss = 0, op_tx_nss = 0;
	qdf_freq_t freq;
	QDF_STATUS status;

	freq = wlan_get_operation_chan_freq(vdev);
	if (!wlan_reg_is_5ghz_ch_freq(freq))
		return;

	vdev_obj = wlan_vdev_get_coex_obj(vdev);
	if (!vdev_obj) {
		coex_err("vdev%u: coex vdev obj is NULL",
			 wlan_vdev_get_id(vdev));
		return;
	}

	wlan_vdev_mlme_get_bss_nss_params(vdev, &cap_tx_nss, &cap_rx_nss,
					  &op_tx_nss, &op_rx_nss);

	dyn_cfg = mlme_get_dynamic_vdev_config(vdev);
	if (!dyn_cfg) {
		coex_err("vdev%u: dynamic vdev config is NULL",
			 wlan_vdev_get_id(vdev));
		return;
	}

	params = *dyn_cfg;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj) {
		coex_err("vdev%u: coex psoc obj is NULL",
			 wlan_vdev_get_id(vdev));
		return;
	}

	if (cap_rx_nss <= psoc_obj->n79_limit_rx_nss &&
	    dyn_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_rx_chain &&
	    dyn_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_tx_nss &&
	    dyn_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_tx_chain) {
		coex_debug("vdev%u: skip N79 apply, at limit rx/tx nss=%u/%u chains=%u/%u",
			   wlan_vdev_get_id(vdev),
			   psoc_obj->n79_limit_rx_nss,
			   psoc_obj->n79_limit_tx_nss,
			   psoc_obj->n79_limit_rx_chain,
			   psoc_obj->n79_limit_tx_chain);
		return;
	}

	/*
	 * Save original values only on first activation.  On re-activation
	 * (Scenario 2: re-activate after RxDiv restore chains but before SS
	 * timer fires), wmi_sent is true and saved values already hold the
	 * true pre-N79 configuration — do not overwrite them.
	 */
	if (!vdev_obj->wmi_sent) {
		vdev_obj->saved_rx_nss =
			(uint8_t)dyn_cfg->rx_nss[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_tx_nss =
			(uint8_t)dyn_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_rx_chains =
			(uint8_t)dyn_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_tx_chains =
			(uint8_t)dyn_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_force =
			(dyn_cfg->nss_band_state[NSS_CHAINS_BAND_5GHZ] ==
			 BAND_REQ_FORCE ||
			 dyn_cfg->chains_band_state[NSS_CHAINS_BAND_5GHZ] ==
			 BAND_REQ_FORCE);

		coex_debug("vdev%u: N79 save: rx_nss=%u rx_chains=%u tx_nss=%u tx_chains=%u",
			   wlan_vdev_get_id(vdev),
			   vdev_obj->saved_rx_nss, vdev_obj->saved_rx_chains,
			   vdev_obj->saved_tx_nss, vdev_obj->saved_tx_chains);
	} else {
		coex_debug("vdev%u: N79 re-apply: saved rx_nss=%u rx_chains=%u tx_nss=%u tx_chains=%u",
			   wlan_vdev_get_id(vdev),
			   vdev_obj->saved_rx_nss, vdev_obj->saved_rx_chains,
			   vdev_obj->saved_tx_nss, vdev_obj->saved_tx_chains);
	}

	params.rx_nss[NSS_CHAINS_BAND_5GHZ] = psoc_obj->n79_limit_rx_nss;
	params.num_rx_chains[NSS_CHAINS_BAND_5GHZ] =
		psoc_obj->n79_limit_rx_chain;
	params.tx_nss[NSS_CHAINS_BAND_5GHZ] = psoc_obj->n79_limit_tx_nss;
	params.num_tx_chains[NSS_CHAINS_BAND_5GHZ] =
		psoc_obj->n79_limit_tx_chain;
	params.disable_rx_mrc[NSS_CHAINS_BAND_5GHZ]   = true;
	params.nss_band_state[NSS_CHAINS_BAND_5GHZ]   = BAND_REQ_FORCE;
	params.chains_band_state[NSS_CHAINS_BAND_5GHZ] = BAND_REQ_FORCE;

	status = tgt_send_n79_coex_nss_chains(vdev, &params);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		vdev_obj->wmi_sent = true;
		*dyn_cfg = params;
		coex_debug("vdev%u: N79 active applied (2x2)",
			   wlan_vdev_get_id(vdev));
	} else {
		coex_err("vdev%u: N79 active WMI failed: %d",
			 wlan_vdev_get_id(vdev), status);
	}
}

void wlan_coex_n79_restore_vdev(struct wlan_objmgr_psoc *psoc,
				struct wlan_objmgr_vdev *vdev,
				void *arg)
{
	struct coex_vdev_obj *vdev_obj;
	struct coex_psoc_obj *psoc_obj;
	struct wlan_mlme_nss_chains params;
	struct wlan_mlme_nss_chains *dyn_cfg;
	bool chains_only = arg && *(bool *)arg;
	QDF_STATUS status;

	vdev_obj = wlan_vdev_get_coex_obj(vdev);
	if (!vdev_obj || !vdev_obj->wmi_sent)
		return;

	/*
	 * Guard against SAP/STA CSA that switched to 2.4 GHz via a
	 * channel-change-only path (no BSS restart, so SAP_STOP was never
	 * fired).  The vdev is no longer on 5 GHz; clear N79 state without
	 * sending a restore WMI.
	 */
	if (!wlan_reg_is_5ghz_ch_freq(wlan_get_operation_chan_freq(vdev))) {
		vdev_obj->wmi_sent = false;
		coex_debug("vdev%u: no longer on 5GHz, clear N79 state",
			   wlan_vdev_get_id(vdev));
		return;
	}

	if (wlan_vdev_mlme_get_opmode(vdev) == QDF_STA_MODE &&
	    wlan_is_tdls_session_present(vdev) == QDF_STATUS_SUCCESS) {
		vdev_obj->n79_restore_pending = true;
		coex_debug("vdev%u: TDLS active, N79 restore deferred",
			   wlan_vdev_get_id(vdev));
		return;
	}

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj)
		return;

	dyn_cfg = mlme_get_dynamic_vdev_config(vdev);
	if (!dyn_cfg)
		return;

	params = *dyn_cfg;

	if (chains_only) {
		/* RxDiv restore: restore chains, keep NSS at N79 limit */
		params.rx_nss[NSS_CHAINS_BAND_5GHZ] =
			psoc_obj->n79_limit_rx_nss;
		params.tx_nss[NSS_CHAINS_BAND_5GHZ] =
			psoc_obj->n79_limit_tx_nss;
	} else {
		/* SS restore: restore full NSS */
		params.rx_nss[NSS_CHAINS_BAND_5GHZ] = vdev_obj->saved_rx_nss;
		params.tx_nss[NSS_CHAINS_BAND_5GHZ] = vdev_obj->saved_tx_nss;
	}
	params.num_rx_chains[NSS_CHAINS_BAND_5GHZ] = vdev_obj->saved_rx_chains;
	params.num_tx_chains[NSS_CHAINS_BAND_5GHZ] = vdev_obj->saved_tx_chains;
	params.disable_rx_mrc[NSS_CHAINS_BAND_5GHZ]   = false;
	if (vdev_obj->saved_force) {
		params.nss_band_state[NSS_CHAINS_BAND_5GHZ] = BAND_REQ_FORCE;
		params.chains_band_state[NSS_CHAINS_BAND_5GHZ] = BAND_REQ_FORCE;
	} else {
		params.nss_band_state[NSS_CHAINS_BAND_5GHZ] =
							BAND_REQ_NO_FORCE;
		params.chains_band_state[NSS_CHAINS_BAND_5GHZ] =
							BAND_REQ_NO_FORCE;
	}

	status = tgt_send_n79_coex_nss_chains(vdev, &params);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		if (!chains_only)
			vdev_obj->wmi_sent = false;
		*dyn_cfg = params;
		coex_debug("vdev%u: N79 %s restore sent (rx_nss=%u rx_chains=%u tx_nss=%u tx_chains=%u)",
			   wlan_vdev_get_id(vdev),
			   chains_only ? "rxdiv" : "ss",
			   params.rx_nss[NSS_CHAINS_BAND_5GHZ],
			   params.num_rx_chains[NSS_CHAINS_BAND_5GHZ],
			   params.tx_nss[NSS_CHAINS_BAND_5GHZ],
			   params.num_tx_chains[NSS_CHAINS_BAND_5GHZ]);
	} else {
		coex_err("vdev%u: N79 restore WMI failed: %d",
			 wlan_vdev_get_id(vdev), status);
	}
}

void wlan_coex_n79_activate_vdev(struct wlan_objmgr_psoc *psoc,
				 void *object,
				 void *arg)
{
	struct wlan_objmgr_vdev *vdev = (struct wlan_objmgr_vdev *)object;
	struct wlan_mlme_nss_chains *dyn_cfg;
	struct coex_psoc_obj *psoc_obj;
	uint8_t cap_rx_nss = 0, cap_tx_nss = 0, op_rx_nss = 0, op_tx_nss = 0;
	qdf_freq_t freq;

	freq = wlan_get_operation_chan_freq(vdev);
	if (!wlan_reg_is_5ghz_ch_freq(freq))
		return;

	wlan_vdev_mlme_get_bss_nss_params(vdev, &cap_tx_nss, &cap_rx_nss,
					  &op_tx_nss, &op_rx_nss);

	dyn_cfg = mlme_get_dynamic_vdev_config(vdev);
	if (!dyn_cfg)
		return;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj)
		return;
	if (cap_rx_nss <= psoc_obj->n79_limit_rx_nss &&
	    dyn_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_rx_chain &&
	    dyn_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_tx_nss &&
	    dyn_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ] <=
	    psoc_obj->n79_limit_tx_chain) {
		coex_debug("vdev%u: skip N79 apply, at limit rx/tx nss=%u/%u chains=%u/%u",
			   wlan_vdev_get_id(vdev),
			   psoc_obj->n79_limit_rx_nss,
			   psoc_obj->n79_limit_tx_nss,
			   psoc_obj->n79_limit_rx_chain,
			   psoc_obj->n79_limit_tx_chain);
		return;
	}

	if (wlan_vdev_mlme_get_opmode(vdev) == QDF_STA_MODE &&
	    wlan_is_tdls_session_present(vdev) == QDF_STATUS_SUCCESS)
		wlan_tdls_check_and_teardown_links_sync(psoc, vdev);

	/* Post per-vdev WMI task to OS_IF scheduler thread */
	wlan_coex_n79_post_vdev_active_cmd(vdev);
}

void wlan_coex_n79_active(struct wlan_objmgr_psoc *psoc)
{
	struct coex_psoc_obj *psoc_obj;
	bool rxdiv_was_active;
	bool ss_was_active;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj)
		return;

	if (psoc_obj->n79_coex_policy == N79_COEX_POLICY_DISABLE) {
		coex_debug("N79 coex policy disabled, skip");
		return;
	}

	/*
	 * Stop both hysteresis timers and capture whether each was still
	 * pending.  The return values tell us which restore phase the system
	 * was in, avoiding any need for extra per-vdev state flags.
	 *
	 *  rxdiv_was_active = true  → INACTIVE_FULL: FW at 2x2 NSS+chains.
	 *                             No WMI needed; just re-arm active flag.
	 *
	 *  ss_was_active = true     → INACTIVE_CHAINS_RESTORED: RxDiv already
	 *                             fired, FW at chains=4 NSS=2. Re-apply
	 *                             chains=2 via the normal activate path.
	 *
	 *  neither active           → RESTORED or first activation:
	 *                             full apply.
	 */
	rxdiv_was_active =
		qdf_timer_stop(&psoc_obj->n79_rx_div_hysteresis_timer);
	ss_was_active    = qdf_timer_stop(&psoc_obj->n79_ss_hysteresis_timer);

	qdf_atomic_set(&psoc_obj->n79_coex_active, 1);

	if (rxdiv_was_active) {
		coex_debug("re-activated during INACTIVE_FULL, FW already 2x2");
		return;
	}

	/*
	 * Either ss_was_active (chains need re-applying) or neither timer was
	 * running (full first activation).  In both cases iterate vdevs; the
	 * save guard in wlan_coex_n79_apply_active_vdev skips the save when
	 * wmi_sent is already true.
	 */
	wlan_objmgr_iterate_obj_list(psoc, WLAN_VDEV_OP,
				     wlan_coex_n79_activate_vdev,
				     NULL, 0, WLAN_COEX_ID);
}

void wlan_coex_n79_inactive(struct wlan_objmgr_psoc *psoc)
{
	struct coex_psoc_obj *psoc_obj;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj)
		return;

	qdf_atomic_set(&psoc_obj->n79_coex_active, 0);

	/* Start hysteresis timers; restore happens on expiry */
	qdf_timer_stop(&psoc_obj->n79_ss_hysteresis_timer);
	qdf_timer_start(&psoc_obj->n79_ss_hysteresis_timer,
			psoc_obj->n79_ss_ms);
	qdf_timer_stop(&psoc_obj->n79_rx_div_hysteresis_timer);
	qdf_timer_start(&psoc_obj->n79_rx_div_hysteresis_timer,
			psoc_obj->n79_rx_div_ms);

	coex_debug("N79 inactive: SS timer %u ms, RxDiv timer %u ms",
		   psoc_obj->n79_ss_ms, psoc_obj->n79_rx_div_ms);
}

static const char * const n79_evt_names[] = {
	[WLAN_COEX_N79_STA_CONNECT]    = "STA_CONNECT",
	[WLAN_COEX_N79_STA_DISCONNECT] = "STA_DISCONNECT",
	[WLAN_COEX_N79_SAP_START]      = "SAP_START",
	[WLAN_COEX_N79_SAP_STOP]       = "SAP_STOP",
	[WLAN_COEX_N79_NAN_START]      = "NAN_START",
	[WLAN_COEX_N79_NAN_STOP]       = "NAN_STOP",
};

QDF_STATUS wlan_coex_n79_event(struct wlan_objmgr_psoc *psoc,
			       struct wlan_objmgr_vdev *vdev,
			       enum wlan_coex_n79_event evt)
{
	struct coex_psoc_obj *psoc_obj;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj)
		return QDF_STATUS_E_INVAL;

	coex_debug("vdev%u: evt %s(%d)",
		   wlan_vdev_get_id(vdev),
		   evt < QDF_ARRAY_SIZE(n79_evt_names) ?
		   n79_evt_names[evt] : "UNKNOWN",
		   evt);

	switch (evt) {
	case WLAN_COEX_N79_STA_CONNECT:
	case WLAN_COEX_N79_SAP_START:
	case WLAN_COEX_N79_NAN_START:
		if (qdf_atomic_read(&psoc_obj->n79_coex_active))
			wlan_coex_n79_apply_active_vdev(psoc, vdev, NULL);
		else
			coex_debug("vdev%u: N79 inactive, skip apply on connect",
				   wlan_vdev_get_id(vdev));
		break;
	case WLAN_COEX_N79_STA_DISCONNECT:
	case WLAN_COEX_N79_SAP_STOP:
	case WLAN_COEX_N79_NAN_STOP: {
		struct coex_vdev_obj *vdev_obj = wlan_vdev_get_coex_obj(vdev);

		if (vdev_obj) {
			vdev_obj->saved_rx_nss        = 0;
			vdev_obj->saved_tx_nss        = 0;
			vdev_obj->saved_rx_chains     = 0;
			vdev_obj->saved_tx_chains     = 0;
			vdev_obj->wmi_sent            = false;
			vdev_obj->n79_restore_pending = false;
		}
		break;
	}
	default:
		break;
	}

	return QDF_STATUS_SUCCESS;
}

bool wlan_coex_n79_nss_chain_vdev_up_req(
			struct wlan_objmgr_vdev *vdev,
			const struct wlan_mlme_nss_chains *params)
{
	struct wlan_objmgr_psoc *psoc;
	struct coex_psoc_obj *psoc_obj;
	struct coex_vdev_obj *vdev_obj;
	qdf_freq_t freq;
	uint8_t nss_limit;

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc)
		return false;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || !qdf_atomic_read(&psoc_obj->n79_coex_active) ||
	    psoc_obj->n79_coex_policy == N79_COEX_POLICY_DISABLE)
		return false;

	freq = wlan_get_operation_chan_freq(vdev);
	if (!wlan_reg_is_5ghz_ch_freq(freq))
		return false;

	nss_limit = psoc_obj->n79_limit_rx_nss;
	if (params->rx_nss[NSS_CHAINS_BAND_5GHZ] <= nss_limit &&
	    params->num_rx_chains[NSS_CHAINS_BAND_5GHZ] <= nss_limit)
		return false;

	vdev_obj = wlan_vdev_get_coex_obj(vdev);
	if (!vdev_obj)
		return false;

	vdev_obj->saved_rx_nss    = params->rx_nss[NSS_CHAINS_BAND_5GHZ];
	vdev_obj->saved_tx_nss    = params->tx_nss[NSS_CHAINS_BAND_5GHZ];
	vdev_obj->saved_rx_chains = params->num_rx_chains[NSS_CHAINS_BAND_5GHZ];
	vdev_obj->saved_tx_chains = params->num_tx_chains[NSS_CHAINS_BAND_5GHZ];
	vdev_obj->saved_force     =
		(params->nss_band_state[NSS_CHAINS_BAND_5GHZ] ==
		 BAND_REQ_FORCE ||
		 params->chains_band_state[NSS_CHAINS_BAND_5GHZ] ==
		 BAND_REQ_FORCE);
	coex_debug("N79 saved txrx nss=%u/%u chains=%u/%u force=%d",
		   vdev_obj->saved_tx_nss, vdev_obj->saved_rx_nss,
		   vdev_obj->saved_tx_chains, vdev_obj->saved_rx_chains,
		   vdev_obj->saved_force);

	return true;

}
