/*
 * Copyright (c) 2016-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: contains nan public API function definitions
 */

#include "nan_main_i.h"
#include "wlan_nan_api.h"
#include "target_if_nan.h"
#include "nan_public_structs.h"
#include "wlan_objmgr_cmn.h"
#include "wlan_objmgr_global_obj.h"
#include "wlan_objmgr_psoc_obj.h"
#include "wlan_objmgr_pdev_obj.h"
#include "wlan_objmgr_vdev_obj.h"
#include "nan_ucfg_api.h"
#include <wlan_mlme_api.h>
#include "cfg_ucfg_api.h"
#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
#include "wma_tgt_cfg.h"
#if defined(WLAN_FEATURE_11AX) || defined(WLAN_FEATURE_11BE)
#include "dot11f.h"
#include "cds_api.h"
#endif
#ifdef WLAN_FEATURE_11BE
#include "wlan_cmn_ieee80211.h"
#endif
#endif

static QDF_STATUS nan_psoc_obj_created_notification(
		struct wlan_objmgr_psoc *psoc, void *arg_list)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct nan_psoc_priv_obj *nan_obj;

	nan_debug("nan_psoc_create_notif called");
	nan_obj = qdf_mem_malloc(sizeof(*nan_obj));
	if (!nan_obj)
		return QDF_STATUS_E_NOMEM;

	qdf_spinlock_create(&nan_obj->lock);
	status = wlan_objmgr_psoc_component_obj_attach(psoc, WLAN_UMAC_COMP_NAN,
						       nan_obj,
						       QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_alert("obj attach with psoc failed");
		goto nan_psoc_notif_failed;
	}

	target_if_nan_register_tx_ops(&nan_obj->tx_ops);
	target_if_nan_register_rx_ops(&nan_obj->rx_ops);

	return QDF_STATUS_SUCCESS;

nan_psoc_notif_failed:

	qdf_spinlock_destroy(&nan_obj->lock);
	qdf_mem_free(nan_obj);
	return status;
}

static QDF_STATUS nan_psoc_obj_destroyed_notification(
				struct wlan_objmgr_psoc *psoc, void *arg_list)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct nan_psoc_priv_obj *nan_obj = nan_get_psoc_priv_obj(psoc);

	nan_debug("nan_psoc_delete_notif called");
	if (!nan_obj) {
		nan_err("nan_obj is NULL");
		return QDF_STATUS_E_FAULT;
	}

	status = wlan_objmgr_psoc_component_obj_detach(psoc,
						       WLAN_UMAC_COMP_NAN,
						       nan_obj);
	if (QDF_IS_STATUS_ERROR(status))
		nan_err("nan_obj detach failed");

	nan_debug("nan_obj deleted with status %d", status);
	qdf_spinlock_destroy(&nan_obj->lock);
	qdf_mem_free(nan_obj);

	return status;
}

/**
 * nan_vdev_obj_created_notification() - Handler for VDEV object creation
 * notification event
 * @vdev: Pointer to the VDEV Object
 * @arg_list: Pointer to private argument - NULL
 *
 * This function gets called from object manager when VDEV is being created.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS nan_vdev_obj_created_notification(
		struct wlan_objmgr_vdev *vdev, void *arg_list)
{
	struct nan_vdev_priv_obj *nan_obj;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct wlan_objmgr_psoc *psoc;

	nan_debug("nan_vdev_create_notif called");
	if (ucfg_is_nan_vdev(vdev)) {
		psoc = wlan_vdev_get_psoc(vdev);
		if (!psoc) {
			nan_err("psoc is NULL");
			return QDF_STATUS_E_INVAL;
		}
		target_if_nan_set_vdev_feature_config(psoc,
						      wlan_vdev_get_id(vdev));
	}

	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_NDI_MODE &&
	    wlan_vdev_mlme_get_opmode(vdev) != QDF_NAN_DISC_MODE) {
		nan_debug("not a ndi vdev. do nothing");
		return QDF_STATUS_SUCCESS;
	}

	nan_obj = qdf_mem_malloc(sizeof(*nan_obj));
	if (!nan_obj)
		return QDF_STATUS_E_NOMEM;

	qdf_spinlock_create(&nan_obj->lock);
	qdf_event_create(&nan_obj->migration_complete_event);
	status = wlan_objmgr_vdev_component_obj_attach(vdev, WLAN_UMAC_COMP_NAN,
						       (void *)nan_obj,
						       QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_alert("obj attach with vdev failed");
		goto nan_vdev_notif_failed;
	}

	return QDF_STATUS_SUCCESS;

nan_vdev_notif_failed:

	qdf_event_destroy(&nan_obj->migration_complete_event);
	qdf_spinlock_destroy(&nan_obj->lock);
	qdf_mem_free(nan_obj);
	return status;
}

/**
 * nan_vdev_obj_destroyed_notification() - Handler for VDEV object deletion
 * notification event
 * @vdev: Pointer to the VDEV Object
 * @arg_list: Pointer to private argument - NULL
 *
 * This function gets called from object manager when VDEV is being destroyed.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS nan_vdev_obj_destroyed_notification(
				struct wlan_objmgr_vdev *vdev, void *arg_list)
{
	struct nan_vdev_priv_obj *nan_obj;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	nan_debug("nan_vdev_delete_notif called");

	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_NDI_MODE &&
	    wlan_vdev_mlme_get_opmode(vdev) != QDF_NAN_DISC_MODE) {
		nan_debug("not a ndi vdev. do nothing");
		return QDF_STATUS_SUCCESS;
	}

	nan_obj = nan_get_vdev_priv_obj(vdev);
	if (!nan_obj) {
		nan_err("nan_obj is NULL");
		return QDF_STATUS_E_FAULT;
	}

	status = wlan_objmgr_vdev_component_obj_detach(vdev, WLAN_UMAC_COMP_NAN,
						       nan_obj);
	if (QDF_IS_STATUS_ERROR(status))
		nan_err("nan_obj detach failed");

	nan_debug("nan_obj deleted with status %d", status);
	qdf_event_destroy(&nan_obj->migration_complete_event);
	qdf_spinlock_destroy(&nan_obj->lock);
	qdf_mem_free(nan_obj);

	return status;
}

/**
 * nan_peer_obj_created_notification() - Handler for peer object creation
 * notification event
 * @peer: Pointer to the PEER Object
 * @arg_list: Pointer to private argument - NULL
 *
 * This function gets called from object manager when peer is being
 * created.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS nan_peer_obj_created_notification(
		struct wlan_objmgr_peer *peer, void *arg_list)
{
	struct nan_peer_priv_obj *nan_peer_obj;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	nan_peer_obj = qdf_mem_malloc(sizeof(*nan_peer_obj));
	if (!nan_peer_obj)
		return QDF_STATUS_E_NOMEM;

	qdf_spinlock_create(&nan_peer_obj->lock);
	status = wlan_objmgr_peer_component_obj_attach(peer, WLAN_UMAC_COMP_NAN,
						       (void *)nan_peer_obj,
						       QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_alert("obj attach with peer failed");
		goto nan_peer_notif_failed;
	}

	return QDF_STATUS_SUCCESS;

nan_peer_notif_failed:

	qdf_spinlock_destroy(&nan_peer_obj->lock);
	qdf_mem_free(nan_peer_obj);
	return status;
}

/**
 * nan_peer_obj_destroyed_notification() - Handler for peer object deletion
 * notification event
 * @peer: Pointer to the PEER Object
 * @arg_list: Pointer to private argument - NULL
 *
 * This function gets called from object manager when peer is being destroyed.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS nan_peer_obj_destroyed_notification(
				struct wlan_objmgr_peer *peer, void *arg_list)
{
	struct nan_peer_priv_obj *nan_peer_obj;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	nan_peer_obj = nan_get_peer_priv_obj(peer);
	if (!nan_peer_obj) {
		nan_err("nan_peer_obj is NULL");
		return QDF_STATUS_E_FAULT;
	}

	status = wlan_objmgr_peer_component_obj_detach(peer, WLAN_UMAC_COMP_NAN,
						       nan_peer_obj);
	if (QDF_IS_STATUS_ERROR(status))
		nan_err("nan_peer_obj detach failed");

	nan_debug("nan_peer_obj deleted with status %d", status);
	qdf_spinlock_destroy(&nan_peer_obj->lock);
	qdf_mem_free(nan_peer_obj);

	return status;
}

QDF_STATUS nan_init(void)
{
	QDF_STATUS status;

	/* register psoc create handler functions. */
	status = wlan_objmgr_register_psoc_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_psoc_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_register_psoc_create_handler failed");
		return status;
	}

	/* register psoc delete handler functions. */
	status = wlan_objmgr_register_psoc_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_psoc_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_register_psoc_destroy_handler failed");
		goto err_psoc_destroy_reg;
	}

	/* register vdev create handler functions. */
	status = wlan_objmgr_register_vdev_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_vdev_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_register_psoc_create_handler failed");
		goto err_vdev_create_reg;
	}

	/* register vdev delete handler functions. */
	status = wlan_objmgr_register_vdev_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_vdev_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_register_psoc_destroy_handler failed");
		goto err_vdev_destroy_reg;
	}

	/* register peer create handler functions. */
	status = wlan_objmgr_register_peer_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_peer_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_register_peer_create_handler failed");
		goto err_peer_create_reg;
	}

	/* register peer delete handler functions. */
	status = wlan_objmgr_register_peer_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_peer_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status))
		nan_err("wlan_objmgr_register_peer_destroy_handler failed");
	else
		return QDF_STATUS_SUCCESS;

	wlan_objmgr_unregister_peer_create_handler(WLAN_UMAC_COMP_NAN,
					nan_peer_obj_created_notification,
					NULL);
err_peer_create_reg:
	wlan_objmgr_unregister_vdev_destroy_handler(WLAN_UMAC_COMP_NAN,
					nan_vdev_obj_destroyed_notification,
					NULL);
err_vdev_destroy_reg:
	wlan_objmgr_unregister_vdev_create_handler(WLAN_UMAC_COMP_NAN,
					nan_vdev_obj_created_notification,
					NULL);
err_vdev_create_reg:
	wlan_objmgr_unregister_psoc_destroy_handler(WLAN_UMAC_COMP_NAN,
					nan_psoc_obj_destroyed_notification,
					NULL);
err_psoc_destroy_reg:
	wlan_objmgr_unregister_psoc_create_handler(WLAN_UMAC_COMP_NAN,
					nan_psoc_obj_created_notification,
					NULL);

	return status;
}

QDF_STATUS nan_deinit(void)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS, status;

	/* register psoc create handler functions. */
	status = wlan_objmgr_unregister_psoc_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_psoc_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_unregister_psoc_create_handler failed");
		ret = status;
	}

	/* register vdev create handler functions. */
	status = wlan_objmgr_unregister_psoc_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_psoc_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_deregister_psoc_destroy_handler failed");
		ret = status;
	}

	/* de-register vdev create handler functions. */
	status = wlan_objmgr_unregister_vdev_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_vdev_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_unregister_psoc_create_handler failed");
		ret = status;
	}

	/* de-register vdev delete handler functions. */
	status = wlan_objmgr_unregister_vdev_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_vdev_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_deregister_psoc_destroy_handler failed");
		ret = status;
	}

	/* de-register peer create handler functions. */
	status = wlan_objmgr_unregister_peer_create_handler(
		WLAN_UMAC_COMP_NAN,
		nan_peer_obj_created_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_unregister_peer_create_handler failed");
		ret = status;
	}

	/* de-register peer delete handler functions. */
	status = wlan_objmgr_unregister_peer_destroy_handler(
		WLAN_UMAC_COMP_NAN,
		nan_peer_obj_destroyed_notification,
		NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("wlan_objmgr_deregister_peer_destroy_handler failed");
		ret = status;
	}

	return ret;
}

QDF_STATUS nan_psoc_enable(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS status = target_if_nan_register_events(psoc);

	if (QDF_IS_STATUS_ERROR(status))
		nan_err("target_if_nan_register_events failed");

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS nan_psoc_disable(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS status = target_if_nan_deregister_events(psoc);

	if (QDF_IS_STATUS_ERROR(status))
		nan_err("target_if_nan_deregister_events failed");

	return QDF_STATUS_SUCCESS;
}

static bool
wlan_is_nan_allowed_on_6ghz_freq(struct wlan_objmgr_pdev *pdev, uint32_t freq)
{
	QDF_STATUS status;
	struct regulatory_channel *chan_list;
	uint32_t len_6g =
			NUM_6GHZ_CHANNELS * sizeof(struct regulatory_channel);
	uint16_t i;
	bool ret = false;

	chan_list = qdf_mem_malloc(len_6g);
	if (!chan_list)
		return ret;

	status = wlan_reg_get_6g_ap_master_chan_list(pdev,
						     REG_VERY_LOW_POWER_AP,
						     chan_list);

	for (i = 0; i < NUM_6GHZ_CHANNELS; i++) {
		if ((freq == chan_list[i].center_freq) &&
		    (chan_list[i].state == CHANNEL_STATE_ENABLE)) {
			ret = true;
			goto end;
		}
	}

end:
	qdf_mem_free(chan_list);
	return ret;
}

bool wlan_is_nan_allowed_on_freq(struct wlan_objmgr_pdev *pdev, uint32_t freq)
{
	bool nan_allowed = true;
	bool enable_nan_on_dfs_channels = false;
	wmi_unified_t wmi_handle;
	uint8_t sta_count = 0;
	uint32_t freq_list[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	uint8_t cfg_sta_indoor_ch_peer_scc = 0;
	uint8_t i;
	QDF_STATUS status;

	wmi_handle = get_wmi_unified_hdl_from_pdev(pdev);
	if (!wmi_handle) {
		nan_err("Invalid WMI handle");
		return false;
	}

	sta_count = policy_mgr_get_mode_specific_conn_info(wlan_pdev_get_psoc(pdev),
							   freq_list,
							   NULL, PM_STA_MODE);

	wlan_mlme_get_support_for_nan_dfs_channel(wlan_pdev_get_psoc(pdev),
						  &enable_nan_on_dfs_channels);

	status = policy_mgr_get_cfg_sta_indoor_ch_peer_scc(wlan_pdev_get_psoc(pdev),
							   &cfg_sta_indoor_ch_peer_scc);

	if (QDF_IS_STATUS_ERROR(status)) {
		nan_err("Failed to get cfg_sta_indoor_ch_peer_scc");
		cfg_sta_indoor_ch_peer_scc = 0;
	}

	/* Check for 6GHz channels */
	if (wlan_reg_is_6ghz_chan_freq(freq)) {
		nan_allowed = wlan_is_nan_allowed_on_6ghz_freq(pdev, freq);
		return nan_allowed;
	}

	/* Check for SRD channels */
	if (wlan_reg_is_etsi_srd_chan_for_freq(pdev, freq))
		wlan_mlme_get_srd_master_mode_for_vdev(wlan_pdev_get_psoc(pdev),
						       QDF_NAN_DISC_MODE,
						       &nan_allowed);
	if (wlan_reg_is_dfs_for_freq(pdev, freq)) {
		if (enable_nan_on_dfs_channels &&
		    wmi_service_enabled(wmi_handle,
					wmi_service_ndp_dfs_channel_support)) {
			return true;
		} else
			return false;
	} else if (wlan_reg_is_freq_indoor(pdev, freq)) {
		if ((cfg_sta_indoor_ch_peer_scc & PM_INDOOR_STA_NAN_SCC)) {
			for (i = 0; i < sta_count; i++) {
				if (freq_list[i] == freq)
					return true;
			}
		}

		wlan_mlme_get_indoor_support_for_nan(wlan_pdev_get_psoc(pdev),
						     &nan_allowed);
	} else if (wlan_reg_is_passive_for_freq(pdev, freq)) {
		return false;
	}

	return nan_allowed;
}

bool wlan_get_disable_6g_nan(struct wlan_objmgr_psoc *psoc)
{
	struct nan_psoc_priv_obj *nan_obj = nan_get_psoc_priv_obj(psoc);

	if (!nan_obj) {
		nan_err("nan psoc priv object is NULL");
		return cfg_default(CFG_DISABLE_6G_NAN);
	}

	return nan_obj->cfg_param.disable_6g_nan;
}

QDF_STATUS nan_set_ndi_state(struct wlan_objmgr_vdev *vdev,
			     enum nan_datapath_state state,
			     const char *func)
{
	struct nan_vdev_priv_obj *priv_obj = nan_get_vdev_priv_obj(vdev);
	enum nan_datapath_state current_state;

	if (!priv_obj) {
		nan_err("priv_obj is null");
		return QDF_STATUS_E_NULL_VALUE;
	}
	qdf_spin_lock_bh(&priv_obj->lock);
	current_state = priv_obj->state;
	priv_obj->state = state;
	qdf_spin_unlock_bh(&priv_obj->lock);
	nan_nofl_debug("%s: ndi state: current: %u, new: %u", func,
		       current_state, state);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS nan_set_active_peers(struct wlan_objmgr_vdev *vdev,
				uint32_t val)
{
	struct nan_vdev_priv_obj *priv_obj = nan_get_vdev_priv_obj(vdev);

	if (!priv_obj) {
		nan_err("priv_obj is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	qdf_spin_lock_bh(&priv_obj->lock);
	priv_obj->active_ndp_peers = val;
	qdf_spin_unlock_bh(&priv_obj->lock);

	return QDF_STATUS_SUCCESS;
}

#ifdef WLAN_FEATURE_11BE_MLO
bool wlan_is_mlo_sta_nan_ndi_allowed(struct wlan_objmgr_psoc *psoc)
{
	struct nan_psoc_priv_obj *psoc_nan_obj;

	psoc_nan_obj = nan_get_psoc_priv_obj(psoc);
	if (!psoc_nan_obj) {
		nan_err("psoc_nan_obj is null");
		return false;
	}

	return psoc_nan_obj->nan_caps.mlo_sta_nan_ndi_allowed;
}
#endif

#if defined(WLAN_FEATURE_NAN)
bool wlan_nan_is_sta_sap_nan_allowed(struct wlan_objmgr_psoc *psoc)
{
	struct nan_psoc_priv_obj *psoc_priv;

	psoc_priv = nan_get_psoc_priv_obj(psoc);
	if (!psoc_priv) {
		nan_err("nan psoc priv object is NULL");
		return false;
	}

	return QDF_MIN(psoc_priv->cfg_param.support_sta_sap_ndp,
		       psoc_priv->nan_caps.sta_sap_ndp_support);
}

qdf_freq_t wlan_nan_sap_override_freq(struct wlan_objmgr_psoc *psoc,
				      uint32_t vdev_id,
				      qdf_freq_t chan_freq)
{
	qdf_freq_t nan_freq_2g = 0, sta_freq = 0;

	if (policy_mgr_is_vdev_ll_lt_sap(psoc, vdev_id))
		return chan_freq;

	nan_freq_2g = policy_mgr_mode_specific_get_channel(psoc,
							   PM_NAN_DISC_MODE);

	/*
	 * Override nan freq if 2 GHz legacy STA is present.
	 * In case of 2 GHz ML STA no need to override,
	 * As it will get disabled if not SCC
	 */
	if (policy_mgr_is_non_ml_sta_present(psoc) &&
	    !policy_mgr_is_mlo_sta_present(psoc)) {
		sta_freq = policy_mgr_mode_specific_get_channel(psoc,
								PM_STA_MODE);
		if (WLAN_REG_IS_24GHZ_CH_FREQ(sta_freq))
			nan_freq_2g = sta_freq;
	}

	if (!nan_freq_2g)
		return chan_freq;
	return nan_freq_2g;
}
#endif

#if defined(WLAN_FEATURE_NAN) && defined(WLAN_CHIPSET_STATS)
void nan_cstats_log_nan_enable_resp_evt(struct nan_event_params *nan_event)
{
	struct cstats_nan_disc_enable_resp stat = {0};
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(nan_event->psoc,
						    nan_event->vdev_id,
						    WLAN_NAN_ID);
	if (!vdev) {
		nan_err("Invalid vdev!");
		return;
	}

	stat.cmn.hdr.evt_id =
		WLAN_CHIPSET_STATS_NAN_DISCOVERY_ENABLE_RESP_EVENT_ID;
	stat.cmn.hdr.length = sizeof(struct cstats_nan_disc_enable_resp) -
			      sizeof(struct cstats_hdr);
	stat.cmn.opmode = wlan_vdev_mlme_get_opmode(vdev);
	stat.cmn.vdev_id = wlan_vdev_get_id(vdev);
	stat.cmn.timestamp_us = qdf_get_time_of_the_day_us();
	stat.cmn.time_tick = qdf_get_log_timestamp();

	stat.is_enable_success = nan_event->is_nan_enable_success;
	stat.mac_id = nan_event->mac_id;
	stat.disc_state = nan_get_discovery_state(nan_event->psoc);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);

	wlan_cstats_host_stats(sizeof(struct cstats_nan_disc_enable_resp),
			       &stat);
}

void nan_cstats_log_nan_disable_resp_evt(uint8_t vdev_id,
					 struct wlan_objmgr_psoc *psoc)
{
	struct cstats_nan_disc_disable_resp stat = {0};

	stat.cmn.hdr.evt_id =
	   WLAN_CHIPSET_STATS_NAN_DISCOVERY_DISABLE_RESP_EVENT_ID;
	stat.cmn.hdr.length =
		sizeof(struct cstats_nan_disc_disable_resp) -
		sizeof(struct cstats_hdr);
	stat.cmn.opmode = QDF_NAN_DISC_MODE;
	stat.cmn.vdev_id = vdev_id;
	stat.cmn.timestamp_us = qdf_get_time_of_the_day_us();
	stat.cmn.time_tick = qdf_get_log_timestamp();
	stat.disc_state = nan_get_discovery_state(psoc);

	wlan_cstats_host_stats(sizeof(struct cstats_nan_disc_disable_resp),
			       &stat);
}
#endif /* WLAN_CHIPSET_STATS */

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
QDF_STATUS nan_get_device_caps(struct wlan_objmgr_psoc *psoc,
			       struct nan_capabilities *caps)
{
	return target_if_nan_set_device_caps(psoc, caps);
}

/**
 * nan_intersect_vht_mcs_map() - intersect two VHT MCS-NSS maps
 * @fw_map: FW-effective VHT MCS-NSS map
 * @drv_map: host-policy VHT MCS-NSS map
 *
 * Each NSS is encoded in 2 bits: 0=MCS0-7, 1=MCS0-8, 2=MCS0-9, 3=unsupported.
 * The intersection per NSS is the more restrictive (numerically smaller,
 * unless either side reports unsupported) of the two encodings.
 *
 * Return: intersected VHT MCS-NSS map
 */
static uint16_t nan_intersect_vht_mcs_map(uint16_t fw_map, uint16_t drv_map)
{
	uint16_t out = 0;
	int nss;

	for (nss = 0; nss < 8; nss++) {
		uint16_t shift = nss * 2;
		uint16_t a = (fw_map >> shift) & 0x3;
		uint16_t b = (drv_map >> shift) & 0x3;
		uint16_t c;

		if (a == 3 || b == 3)
			c = 3;
		else
			c = (a < b) ? a : b;
		out |= (c & 0x3) << shift;
	}
	return out;
}

#ifdef WLAN_FEATURE_11AX
/**
 * nan_pack_he_cap_ie() - Pack a dot11f HE capability into its on-wire IE
 * bytes and extract the MAC/PHY capability fields
 * @he_cap_cfg: dot11f HE capability
 * @mac_cap_info: output 6-byte MAC capability info
 * @phy_cap_info: output 11-byte PHY capability info
 *
 * Return: None
 */
static void nan_pack_he_cap_ie(const tDot11fIEhe_cap *he_cap_cfg,
			       uint8_t mac_cap_info[6],
			       uint8_t phy_cap_info[11])
{
	struct mac_context *mac_ctx = cds_get_context(QDF_MODULE_ID_PE);
	uint8_t buf[128];
	uint32_t consumed = 0;
	uint32_t status;
	const uint8_t *payload;
	/*
	 * dot11f_pack_ie_he_cap() does not modify its input in practice, but
	 * its generated signature does not take a const pointer. Make a local
	 * copy to avoid casting away const from the caller's data.
	 */
	tDot11fIEhe_cap he_cap_local;

	qdf_mem_zero(mac_cap_info, 6);
	qdf_mem_zero(phy_cap_info, 11);
	if (!mac_ctx || !he_cap_cfg || !he_cap_cfg->present)
		return;

	he_cap_local = *he_cap_cfg;
	status = dot11f_pack_ie_he_cap(mac_ctx, &he_cap_local,
				       buf, sizeof(buf), &consumed);
	if (!DOT11F_SUCCEEDED(status) || consumed < 3 + 6 + 11)
		return;

	/*
	 * Packed layout: Element ID (255, Extension Element),
	 * Length, Extension ID (35, HE Capabilities), then
	 * 6 bytes MAC cap info + 11 bytes PHY cap info.
	 */
	if (buf[0] != DOT11F_EID_HE_CAP || buf[2] != 35)
		return;

	payload = &buf[3];
	qdf_mem_copy(mac_cap_info, payload, 6);
	qdf_mem_copy(phy_cap_info, payload + 6, 11);
}

static uint16_t nan_u16_from_u8_pair(const uint8_t *u8_pair)
{
	uint16_t v;

	qdf_mem_copy(&v, u8_pair, sizeof(v));
	return v;
}

/**
 * nan_intersect_he_mcs_map() - Merge HE MCS maps (strict intersection)
 * @a: per-band MCS map for 2.4 GHz
 * @a_valid: whether 2.4 GHz band is enabled
 * @b: per-band MCS map for 5 GHz
 * @b_valid: whether 5 GHz band is enabled
 * @c: per-band MCS map for 6 GHz
 * @c_valid: whether 6 GHz band is enabled
 *
 * HE MCS map encoding: 2 bits per NSS (0=0-7, 1=0-9, 2=0-11, 3=not
 * supported). Disabled bands contribute the neutral element (0x2, the
 * maximum) so they don't restrict the intersection.
 *
 * Return: merged MCS map
 */
static uint16_t nan_intersect_he_mcs_map(uint16_t a, bool a_valid,
					 uint16_t b, bool b_valid,
					 uint16_t c, bool c_valid)
{
	uint16_t out = 0;
	int s;

	for (s = 0; s < 8; s++) {
		uint8_t ca = a_valid ? ((a >> (2 * s)) & 0x3) : 0x2;
		uint8_t cb = b_valid ? ((b >> (2 * s)) & 0x3) : 0x2;
		uint8_t cc = c_valid ? ((c >> (2 * s)) & 0x3) : 0x2;
		uint8_t merged;

		if (ca == 0x3 || cb == 0x3 || cc == 0x3)
			merged = 0x3;
		else
			merged = QDF_MIN(ca, QDF_MIN(cb, cc));

		out |= ((uint16_t)merged) << (2 * s);
	}

	return out;
}

/**
 * struct nan_he_band_caps - per-band OS-agnostic HE capability
 * @present: whether HE is present for this band
 * @mac_cap_info: packed MAC capability info bytes
 * @phy_cap_info: packed PHY capability info bytes
 * @rx_mcs_map_lt_80: RX MCS map for <80 MHz
 * @tx_mcs_map_lt_80: TX MCS map for <80 MHz
 * @rx_mcs_map_160: RX MCS map for 160 MHz
 * @tx_mcs_map_160: TX MCS map for 160 MHz
 * @rx_mcs_map_80p80: RX MCS map for 80+80 MHz
 * @tx_mcs_map_80p80: TX MCS map for 80+80 MHz
 */
struct nan_he_band_caps {
	bool present;
	uint8_t mac_cap_info[6];
	uint8_t phy_cap_info[11];
	uint16_t rx_mcs_map_lt_80;
	uint16_t tx_mcs_map_lt_80;
	uint16_t rx_mcs_map_160;
	uint16_t tx_mcs_map_160;
	uint16_t rx_mcs_map_80p80;
	uint16_t tx_mcs_map_80p80;
};

/**
 * nan_build_he_cap_per_band() - Build per-band OS-agnostic HE cap
 * @he_cap_cfg: effective (FW ∩ host) HE cap for this band
 * @out: output per-band HE cap
 *
 * Return: None
 */
static void nan_build_he_cap_per_band(const tDot11fIEhe_cap *he_cap_cfg,
				      struct nan_he_band_caps *out)
{
	qdf_mem_zero(out, sizeof(*out));
	if (!he_cap_cfg || !he_cap_cfg->present)
		return;

	out->present = true;
	nan_pack_he_cap_ie(he_cap_cfg, out->mac_cap_info, out->phy_cap_info);

	out->rx_mcs_map_lt_80 = he_cap_cfg->rx_he_mcs_map_lt_80;
	out->tx_mcs_map_lt_80 = he_cap_cfg->tx_he_mcs_map_lt_80;
	out->rx_mcs_map_160 =
		nan_u16_from_u8_pair(he_cap_cfg->rx_he_mcs_map_160[0]);
	out->tx_mcs_map_160 =
		nan_u16_from_u8_pair(he_cap_cfg->tx_he_mcs_map_160[0]);
	out->rx_mcs_map_80p80 =
		nan_u16_from_u8_pair(he_cap_cfg->rx_he_mcs_map_80_80[0]);
	out->tx_mcs_map_80p80 =
		nan_u16_from_u8_pair(he_cap_cfg->tx_he_mcs_map_80_80[0]);
}

/**
 * nan_populate_he_phy_caps() - Compute the NAN HE PHY capability
 * intersection and store it into @caps
 * @cfg: effective target config (FW intersected with host)
 * @enable_2g: whether NAN is enabled on 2.4 GHz band
 * @enable_5g: whether NAN is enabled on 5 GHz band
 * @enable_6g: whether NAN is enabled on 6 GHz band
 * @caps: output NAN PHY capability struct
 *
 * Strict intersection: a capability is advertised only if supported on all
 * bands on which NAN is enabled. There is no dedicated 6 GHz HE cap field
 * in struct wma_tgt_cfg today, so the 5 GHz HE cap is used as a
 * conservative fallback for 6 GHz.
 *
 * Return: None
 */
static void nan_populate_he_phy_caps(struct wma_tgt_cfg *cfg, bool enable_2g,
				     bool enable_5g, bool enable_6g,
				     struct nan_phy_caps *caps)
{
	struct nan_he_band_caps he2 = {0}, he5 = {0}, he6 = {0};
	bool v2, v5, v6;
	bool base_is_2g = false, base_is_5g = false, base_is_6g = false;
	int i;

	if (enable_2g && cfg->he_cap_2g.present)
		nan_build_he_cap_per_band(&cfg->he_cap_2g, &he2);
	if (enable_5g && cfg->he_cap_5g.present)
		nan_build_he_cap_per_band(&cfg->he_cap_5g, &he5);
	if (enable_6g && cfg->he_cap_5g.present)
		nan_build_he_cap_per_band(&cfg->he_cap_5g, &he6);

	v2 = enable_2g && he2.present;
	v5 = enable_5g && he5.present;
	v6 = enable_6g && he6.present;

	caps->he_supported = (v2 || !enable_2g) && (v5 || !enable_5g) &&
			      (v6 || !enable_6g) && (v2 || v5 || v6);
	if (!caps->he_supported)
		return;

	if (v2) {
		qdf_mem_copy(caps->he_mac_cap_info, he2.mac_cap_info, 6);
		qdf_mem_copy(caps->he_phy_cap_info, he2.phy_cap_info, 11);
		base_is_2g = true;
	} else if (v5) {
		qdf_mem_copy(caps->he_mac_cap_info, he5.mac_cap_info, 6);
		qdf_mem_copy(caps->he_phy_cap_info, he5.phy_cap_info, 11);
		base_is_5g = true;
	} else if (v6) {
		qdf_mem_copy(caps->he_mac_cap_info, he6.mac_cap_info, 6);
		qdf_mem_copy(caps->he_phy_cap_info, he6.phy_cap_info, 11);
		base_is_6g = true;
	}

	if (!base_is_2g && v2) {
		for (i = 0; i < 6; i++)
			caps->he_mac_cap_info[i] &= he2.mac_cap_info[i];
		for (i = 0; i < 11; i++)
			caps->he_phy_cap_info[i] &= he2.phy_cap_info[i];
	}
	if (!base_is_5g && v5) {
		for (i = 0; i < 6; i++)
			caps->he_mac_cap_info[i] &= he5.mac_cap_info[i];
		for (i = 0; i < 11; i++)
			caps->he_phy_cap_info[i] &= he5.phy_cap_info[i];
	}
	if (!base_is_6g && v6) {
		for (i = 0; i < 6; i++)
			caps->he_mac_cap_info[i] &= he6.mac_cap_info[i];
		for (i = 0; i < 11; i++)
			caps->he_phy_cap_info[i] &= he6.phy_cap_info[i];
	}

	caps->he_rx_mcs_map_lt_80 = nan_intersect_he_mcs_map(
		v2 ? he2.rx_mcs_map_lt_80 : 0, v2,
		v5 ? he5.rx_mcs_map_lt_80 : 0, v5,
		v6 ? he6.rx_mcs_map_lt_80 : 0, v6);
	caps->he_tx_mcs_map_lt_80 = nan_intersect_he_mcs_map(
		v2 ? he2.tx_mcs_map_lt_80 : 0, v2,
		v5 ? he5.tx_mcs_map_lt_80 : 0, v5,
		v6 ? he6.tx_mcs_map_lt_80 : 0, v6);
	caps->he_rx_mcs_map_160 = nan_intersect_he_mcs_map(
		v2 ? he2.rx_mcs_map_160 : 0, v2,
		v5 ? he5.rx_mcs_map_160 : 0, v5,
		v6 ? he6.rx_mcs_map_160 : 0, v6);
	caps->he_tx_mcs_map_160 = nan_intersect_he_mcs_map(
		v2 ? he2.tx_mcs_map_160 : 0, v2,
		v5 ? he5.tx_mcs_map_160 : 0, v5,
		v6 ? he6.tx_mcs_map_160 : 0, v6);
	caps->he_rx_mcs_map_80p80 = nan_intersect_he_mcs_map(
		v2 ? he2.rx_mcs_map_80p80 : 0, v2,
		v5 ? he5.rx_mcs_map_80p80 : 0, v5,
		v6 ? he6.rx_mcs_map_80p80 : 0, v6);
	caps->he_tx_mcs_map_80p80 = nan_intersect_he_mcs_map(
		v2 ? he2.tx_mcs_map_80p80 : 0, v2,
		v5 ? he5.tx_mcs_map_80p80 : 0, v5,
		v6 ? he6.tx_mcs_map_80p80 : 0, v6);
}
#else
static inline void nan_populate_he_phy_caps(struct wma_tgt_cfg *cfg,
					    bool enable_2g, bool enable_5g,
					    bool enable_6g,
					    struct nan_phy_caps *caps)
{
}
#endif /* WLAN_FEATURE_11AX */

#ifdef WLAN_FEATURE_11BE
/**
 * nan_pack_eht_cap_ie() - Pack a dot11f EHT capability into its on-wire IE
 * bytes and extract the MAC/PHY capability fields and MCS/NSS set
 * @eht_cap_cfg: dot11f EHT capability
 * @mac_cap_info: output 2-byte MAC capability info
 * @phy_cap_info: output 9-byte PHY capability info
 * @mcs_nss_supp: output buffer for the packed MCS/NSS Set bytes, sized
 *     NAN_EHT_MCS_NSS_MAX_LEN
 * @mcs_nss_supp_len: output number of valid bytes written to @mcs_nss_supp
 *
 * Return: None
 */
static void nan_pack_eht_cap_ie(const tDot11fIEeht_cap *eht_cap_cfg,
				uint8_t mac_cap_info[2],
				uint8_t phy_cap_info[9],
				uint8_t mcs_nss_supp[NAN_EHT_MCS_NSS_MAX_LEN],
				uint32_t *mcs_nss_supp_len)
{
	struct mac_context *mac_ctx = cds_get_context(QDF_MODULE_ID_PE);
	uint8_t buf[128];
	uint32_t consumed = 0;
	uint32_t status;
	const uint8_t *payload;
	/*
	 * dot11f_pack_ie_eht_cap() does not modify its input in practice, but
	 * its generated signature does not take a const pointer. Make a local
	 * copy to avoid casting away const from the caller's data.
	 */
	tDot11fIEeht_cap eht_cap_local;
	/*
	 * Minimum valid EHT cap IE size: 3-byte header (elem_id + length +
	 * ext_id) + 2-byte MAC cap + 9-byte PHY cap = 14 bytes total.
	 */
	uint32_t min_len = 3 + 2 + 9;

	qdf_mem_zero(mac_cap_info, 2);
	qdf_mem_zero(phy_cap_info, 9);
	qdf_mem_zero(mcs_nss_supp, NAN_EHT_MCS_NSS_MAX_LEN);
	*mcs_nss_supp_len = 0;
	if (!mac_ctx || !eht_cap_cfg || !eht_cap_cfg->present)
		return;

	eht_cap_local = *eht_cap_cfg;
	status = dot11f_pack_ie_eht_cap(mac_ctx, &eht_cap_local,
					buf, sizeof(buf), &consumed);
	if (!DOT11F_SUCCEEDED(status) || consumed < min_len)
		return;

	/*
	 * Packed layout: Element ID (255, Extension Element), Length,
	 * Extension ID (108, EHT Capabilities), then 2 bytes MAC cap info +
	 * 9 bytes PHY cap info + variable-length MCS/NSS Set.
	 */
	if (buf[0] != WLAN_ELEMID_EXTN_ELEM ||
	    buf[2] != WLAN_EXTN_ELEMID_EHTCAP)
		return;

	payload = &buf[3];
	qdf_mem_copy(mac_cap_info, payload, 2);
	qdf_mem_copy(phy_cap_info, payload + 2, 9);

	if (consumed > min_len)
		*mcs_nss_supp_len = QDF_MIN(consumed - min_len,
					    (uint32_t)NAN_EHT_MCS_NSS_MAX_LEN);
	if (*mcs_nss_supp_len)
		qdf_mem_copy(mcs_nss_supp, &buf[min_len], *mcs_nss_supp_len);
}

/**
 * nan_populate_eht_phy_caps() - Compute the NAN EHT PHY capability and
 * store it into @caps
 * @cfg: effective target config (FW intersected with host)
 * @caps: output NAN PHY capability struct
 *
 * Populates EHT capabilities for NAN from the aggregated target EHT cap
 * (cfg->eht_cap). Per-band intersection (using eht_cap_2g/eht_cap_5g) is not
 * yet implemented, analogous to the HE path.
 *
 * Return: None
 */
static void nan_populate_eht_phy_caps(struct wma_tgt_cfg *cfg,
				      struct nan_phy_caps *caps)
{
	caps->eht_supported = cfg->eht_cap.present;
	if (!caps->eht_supported)
		return;

	nan_pack_eht_cap_ie(&cfg->eht_cap, caps->eht_mac_cap_info,
			    caps->eht_phy_cap_info, caps->eht_mcs_nss_supp,
			    &caps->eht_mcs_nss_supp_len);
}
#else
static inline void nan_populate_eht_phy_caps(struct wma_tgt_cfg *cfg,
					     struct nan_phy_caps *caps)
{
}
#endif /* WLAN_FEATURE_11BE */

void nan_populate_phy_caps(struct wlan_objmgr_psoc *psoc,
			   struct wma_tgt_cfg *cfg, uint8_t num_rf_chains,
			   bool enable_2g, bool enable_5g, bool enable_6g)
{
	struct nan_psoc_priv_obj *psoc_priv = nan_get_psoc_priv_obj(psoc);
	struct nan_phy_caps *caps;
	bool nan_band_enabled;
	int nss;
	uint16_t drv_mcs_map, fw_rx_mcs, fw_tx_mcs;

	if (!psoc_priv || !cfg)
		return;

	caps = &psoc_priv->phy_caps;
	qdf_mem_zero(caps, sizeof(*caps));

	nan_band_enabled = enable_2g || enable_5g || enable_6g;

	/*
	 * cfg->ht_cap/vht_cap are already the effective FW∩host capability
	 * (not per-band), so the intersection across bands reduces to a
	 * single check: is NAN enabled on at least one band?
	 */
	if (nan_band_enabled) {
		caps->ht_ldpc = cfg->ht_cap.ht_rx_ldpc;
		caps->ht_sgi_20 = cfg->ht_cap.ht_sgi_20;
		caps->ht_sgi_40 = cfg->ht_cap.ht_sgi_40;
		caps->ht_rx_stbc = cfg->ht_cap.ht_rx_stbc;
		caps->ht_tx_stbc = cfg->ht_cap.ht_tx_stbc;
		caps->ht_mpdu_density = cfg->ht_cap.mpdu_density;
	}
	caps->ht_supported = caps->ht_ldpc || caps->ht_sgi_20 ||
			      caps->ht_sgi_40 || caps->ht_rx_stbc ||
			      caps->ht_tx_stbc;

	/*
	 * Build driver MCS map from num_rf_chains: supported NSS get
	 * MCS 0-9 (encoding 0x1), all others are unsupported (0x3).
	 */
	drv_mcs_map = 0xFFFF; /* all NSS unsupported by default */
	for (nss = 0; nss < num_rf_chains && nss < 8; nss++) {
		drv_mcs_map &= ~(0x3U << (nss * 2));
		drv_mcs_map |= (0x1U << (nss * 2)); /* MCS 0-9 */
	}

	/*
	 * vht_supp_mcs lower 16 bits = RX MCS map, upper 16 bits = TX MCS
	 * map (0 if not separately encoded).
	 */
	fw_rx_mcs = (uint16_t)(cfg->vht_cap.vht_supp_mcs & 0xFFFF);
	fw_tx_mcs = (uint16_t)((cfg->vht_cap.vht_supp_mcs >> 16) & 0xFFFF);
	if (!fw_tx_mcs)
		fw_tx_mcs = fw_rx_mcs;

	caps->vht_rx_mcs_map = nan_intersect_vht_mcs_map(fw_rx_mcs,
							 drv_mcs_map);
	caps->vht_tx_mcs_map = nan_intersect_vht_mcs_map(fw_tx_mcs,
							 drv_mcs_map);

	caps->vht_short_gi_80 = cfg->vht_cap.vht_short_gi_80;
	caps->vht_short_gi_160 = cfg->vht_cap.vht_short_gi_160;
	caps->vht_rx_ldpc = cfg->vht_cap.vht_rx_ldpc;
	caps->vht_tx_stbc = cfg->vht_cap.vht_tx_stbc;
	caps->vht_rx_stbc = cfg->vht_cap.vht_rx_stbc;
	caps->vht_su_bformer = cfg->vht_cap.vht_su_bformer;
	caps->vht_su_bformee = cfg->vht_cap.vht_su_bformee;

	caps->vht_supported = caps->vht_short_gi_80 || caps->vht_short_gi_160 ||
			       caps->vht_rx_ldpc || caps->vht_tx_stbc ||
			       caps->vht_rx_stbc || caps->vht_su_bformer ||
			       caps->vht_su_bformee ||
			       caps->vht_rx_mcs_map != 0xFFFF;

	nan_populate_he_phy_caps(cfg, enable_2g, enable_5g, enable_6g, caps);
	nan_populate_eht_phy_caps(cfg, caps);
}

void nan_get_phy_caps(struct wlan_objmgr_psoc *psoc,
		      struct nan_phy_caps *caps)
{
	struct nan_psoc_priv_obj *psoc_priv = nan_get_psoc_priv_obj(psoc);

	if (!psoc_priv || !caps)
		return;

	*caps = psoc_priv->phy_caps;
}
#endif

