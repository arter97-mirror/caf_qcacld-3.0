/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

/**
 * DOC: wlan_coex_utils_api.c
 *
 * This file provides definitions of public APIs exposed to other UMAC
 * components.
 */

#include <wlan_coex_main.h>
#include <wlan_objmgr_global_obj.h>
#include <wlan_coex_utils_api.h>
#include "cfg_ucfg_api.h"
#ifdef FEATURE_N79_COEX
#include "wlan_mlme_public_struct.h"
#include "wlan_mlme_main.h"
#endif

QDF_STATUS wlan_coex_init(void)
{
	QDF_STATUS status;

	status = wlan_objmgr_register_psoc_create_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_created_notification, NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		coex_err("Failed to register psoc create handler");
		goto fail_create_psoc;
	}

	status = wlan_objmgr_register_psoc_destroy_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_destroyed_notification, NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		coex_err("Failed to create psoc delete handler");
		goto fail_psoc_destroy;
	}

	status = wlan_coex_n79_register_vdev_handlers();
	if (QDF_IS_STATUS_ERROR(status))
		goto fail_n79_vdev;

	coex_debug("coex psoc/vdev handlers registered");
	return status;

fail_n79_vdev:
	wlan_objmgr_unregister_psoc_destroy_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_destroyed_notification, NULL);
fail_psoc_destroy:
	wlan_objmgr_unregister_psoc_create_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_created_notification, NULL);
fail_create_psoc:
	return status;
}

QDF_STATUS wlan_coex_deinit(void)
{
	QDF_STATUS status;

	wlan_coex_n79_unregister_vdev_handlers();

	status = wlan_objmgr_unregister_psoc_destroy_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_destroyed_notification, NULL);
	if (status != QDF_STATUS_SUCCESS)
		coex_err("Failed to unregister psoc delete handler");

	status = wlan_objmgr_unregister_psoc_create_handler(
			WLAN_UMAC_COMP_COEX,
			wlan_coex_psoc_created_notification, NULL);
	if (status != QDF_STATUS_SUCCESS)
		coex_err("Failed to unregister psoc create handler");

	return status;
}

#ifdef FEATURE_BTC_CHAIN_MODE
/**
 * wlan_coex_set_btc_chain_mode_with_ini() - set BTC init chain mode
 * with ini
 * @psoc: pointer to psoc object
 *
 * This function is used to set BTC init chain mode with ini
 *
 * Return: None
 */
static void
wlan_coex_set_btc_chain_mode_with_ini(struct wlan_objmgr_psoc *psoc)
{
	enum coex_btc_chain_mode btc_chain_mode;
	QDF_STATUS status;

	status = wlan_coex_psoc_get_btc_chain_mode(psoc, &btc_chain_mode);
	if (QDF_IS_STATUS_ERROR(status)) {
		coex_err("error for getting btc chain mode");
		return;
	}

	if (btc_chain_mode == WLAN_COEX_BTC_CHAIN_MODE_UNSETTLED) {
		btc_chain_mode = cfg_get(psoc, CFG_SET_INIT_CHAIN_MODE_FOR_BTC);
		if (btc_chain_mode > WLAN_COEX_BTC_CHAIN_MODE_HYBRID &&
		    btc_chain_mode != WLAN_COEX_BTC_CHAIN_MODE_UNSETTLED) {
			coex_err("invalid ini config %d for btc chain mode",
				 btc_chain_mode);
			return;
		}

		status = wlan_coex_psoc_set_btc_chain_mode(psoc,
							   btc_chain_mode);
		if (QDF_IS_STATUS_ERROR(status))
			coex_err("error for setting btc init chain mode from ini");
	}
}
#else
static void
wlan_coex_set_btc_chain_mode_with_ini(struct wlan_objmgr_psoc *psoc)
{
}
#endif

QDF_STATUS
wlan_coex_psoc_open(struct wlan_objmgr_psoc *psoc)
{
	wlan_coex_set_btc_chain_mode_with_ini(psoc);
	return wlan_coex_psoc_init(psoc);
}

QDF_STATUS
wlan_coex_psoc_close(struct wlan_objmgr_psoc *psoc)
{
	return wlan_coex_psoc_deinit(psoc);
}

#ifdef WLAN_FEATURE_DBAM_CONFIG
QDF_STATUS wlan_dbam_psoc_enable(struct wlan_objmgr_psoc *psoc)
{
	return wlan_dbam_attach(psoc);
}

QDF_STATUS wlan_dbam_psoc_disable(struct wlan_objmgr_psoc *psoc)
{
	return wlan_dbam_detach(psoc);
}
#endif

QDF_STATUS
wlan_coex_psoc_get_btc_chain_mode(struct wlan_objmgr_psoc *psoc,
				  enum coex_btc_chain_mode *val)
{
	return coex_psoc_get_btc_chain_mode(psoc, val);
}

#ifdef FEATURE_N79_COEX
bool wlan_coex_n79_is_active(struct wlan_objmgr_psoc *psoc)
{
	struct coex_psoc_obj *psoc_obj;

	if (!psoc)
		return false;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);

	return psoc_obj ?
		!!qdf_atomic_read(&psoc_obj->n79_coex_active) : false;
}

bool wlan_coex_n79_update_nss_chains(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_vdev *vdev,
				     struct wlan_mlme_nss_chains *nss_cfg,
				     uint32_t ch_freq)
{
	struct coex_psoc_obj *psoc_obj;
	struct coex_vdev_obj *vdev_obj;
	struct wlan_mlme_nss_chains *dyn_cfg;

	if (!psoc || !vdev || !nss_cfg)
		return false;

	psoc_obj = wlan_psoc_get_coex_obj(psoc);
	if (!psoc_obj || !qdf_atomic_read(&psoc_obj->n79_coex_active))
		return false;

	coex_debug("N79 update nss chains: policy=%d",
		   psoc_obj->n79_coex_policy);
	if (psoc_obj->n79_coex_policy != N79_COEX_POLICY_2X2)
		return false;

	/* Only apply to STA/GC/GO/SAP; NAN and other modes do not use
	 * this chainmask command.
	 */
	if (!wlan_coex_n79_is_supported_opmode(vdev)) {
		coex_debug("vdev%u: opmode %d not supported, skip N79 nss update",
			   wlan_vdev_get_id(vdev),
			   wlan_vdev_mlme_get_opmode(vdev));
		return false;
	}

	/* Skip if all fields are already at or below the N79 limits */
	if (nss_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ] <=
			psoc_obj->n79_limit_rx_chain &&
	    nss_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ] <=
			psoc_obj->n79_limit_tx_chain &&
	    nss_cfg->rx_nss[NSS_CHAINS_BAND_5GHZ] <=
			psoc_obj->n79_limit_rx_nss &&
	    nss_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ] <=
			psoc_obj->n79_limit_tx_nss)
		return false;

	vdev_obj = wlan_vdev_get_coex_obj(vdev);
	dyn_cfg  = mlme_get_dynamic_vdev_config(vdev);
	if (vdev_obj && dyn_cfg && !vdev_obj->wmi_sent) {
		vdev_obj->saved_rx_nss =
			(uint8_t)nss_cfg->rx_nss[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_tx_nss =
			(uint8_t)nss_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_rx_chains =
			(uint8_t)nss_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_tx_chains =
			(uint8_t)nss_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ];
		vdev_obj->saved_tx_chains_11a =
			(uint8_t)nss_cfg->num_tx_chains_11a;
		vdev_obj->saved_tx_chains_11b =
			(uint8_t)nss_cfg->num_tx_chains_11b;
		vdev_obj->saved_tx_chains_11g =
			(uint8_t)nss_cfg->num_tx_chains_11g;
		vdev_obj->saved_force =
			(dyn_cfg->nss_band_state[NSS_CHAINS_BAND_5GHZ] ==
			 BAND_REQ_FORCE ||
			 dyn_cfg->chains_band_state[NSS_CHAINS_BAND_5GHZ] ==
			 BAND_REQ_FORCE);
	}

	nss_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ] =
					psoc_obj->n79_limit_rx_chain;
	nss_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ] =
					psoc_obj->n79_limit_tx_chain;
	nss_cfg->rx_nss[NSS_CHAINS_BAND_5GHZ] = psoc_obj->n79_limit_rx_nss;
	nss_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ] = psoc_obj->n79_limit_tx_nss;
	nss_cfg->num_tx_chains_11a = psoc_obj->n79_limit_tx_chain;
	nss_cfg->num_tx_chains_11b = psoc_obj->n79_limit_tx_chain;
	nss_cfg->num_tx_chains_11g = psoc_obj->n79_limit_tx_chain;
	nss_cfg->nss_band_state[NSS_CHAINS_BAND_5GHZ]    = BAND_REQ_FORCE;
	nss_cfg->chains_band_state[NSS_CHAINS_BAND_5GHZ] = BAND_REQ_FORCE;

	if (vdev_obj)
		vdev_obj->wmi_sent = true;

	coex_debug("Set nss=%u/%u chains=%u/%u",
		   nss_cfg->tx_nss[NSS_CHAINS_BAND_5GHZ],
		   nss_cfg->rx_nss[NSS_CHAINS_BAND_5GHZ],
		   nss_cfg->num_tx_chains[NSS_CHAINS_BAND_5GHZ],
		   nss_cfg->num_rx_chains[NSS_CHAINS_BAND_5GHZ]);

	return true;
}
#endif /* FEATURE_N79_COEX */
