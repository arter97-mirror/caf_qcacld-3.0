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
 * DOC: wlan_coex_utils_api.h
 *
 * This header file provides declaration of public APIs exposed to other UMAC
 * components.
 */

#ifndef _WLAN_COEX_UTILS_API_H_
#define _WLAN_COEX_UTILS_API_H_
#include <wlan_objmgr_psoc_obj.h>
#include "wlan_coex_public_structs.h"

/*
 * wlan_coex_init() - Coex module initialization API
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_coex_init(void);

/*
 * wlan_coex_deinit() - Coex module deinitialization API
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_coex_deinit(void);

/**
 * wlan_coex_psoc_open() - Open coex component
 * @psoc: soc context
 *
 * This function gets called when dispatcher opening.
 *
 * Return: QDF_STATUS_SUCCESS - in case of success
 */
QDF_STATUS
wlan_coex_psoc_open(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_coex_psoc_close() - Close coex component
 * @psoc: soc context
 *
 * This function gets called when dispatcher closing.
 *
 * Return: QDF_STATUS_SUCCESS - in case of success
 */
QDF_STATUS
wlan_coex_psoc_close(struct wlan_objmgr_psoc *psoc);

#ifdef WLAN_FEATURE_DBAM_CONFIG
/**
 * wlan_dbam_psoc_enable() - API to enable coex dbam psoc component
 * @psoc: pointer to psoc
 *
 * This API is invoked from dispatcher psoc enable.
 * This API will register dbam WMI event handlers.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_** on error
 */
QDF_STATUS wlan_dbam_psoc_enable(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_dbam_psoc_disable() - API to disable coex dbam psoc component
 * @psoc: pointer to psoc
 *
 * This API is invoked from dispatcher psoc disable.
 * This API will unregister dbam WMI event handlers.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_** on error
 */
QDF_STATUS wlan_dbam_psoc_disable(struct wlan_objmgr_psoc *psoc);
#endif /* WLAN_FEATURE_DBAM_CONFIG */

/**
 * wlan_coex_psoc_get_btc_chain_mode() - Wrapper API to get BT coex chain mode
 * from psoc
 * @psoc: pointer to psoc object
 * @val: pointer to BT coex chain mode
 *
 * Return : status of operation
 */
#ifdef FEATURE_COEX
QDF_STATUS
wlan_coex_psoc_get_btc_chain_mode(struct wlan_objmgr_psoc *psoc,
				  enum coex_btc_chain_mode *val);
#else
static inline QDF_STATUS
wlan_coex_psoc_get_btc_chain_mode(struct wlan_objmgr_psoc *psoc,
				  enum coex_btc_chain_mode *val)
{
	*val = WLAN_COEX_BTC_CHAIN_MODE_UNSETTLED;
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef FEATURE_N79_COEX
struct wlan_mlme_nss_chains;

/**
 * wlan_coex_n79_is_active() - query current N79 active state
 * @psoc: pointer to psoc object
 *
 * Return: true if N79 coexistence is currently active
 */
bool wlan_coex_n79_is_active(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_coex_n79_update_nss_chains() - apply N79 chain/NSS limits to config
 * @psoc: pointer to psoc object
 * @vdev: pointer to vdev object (used for bookkeeping: wmi_sent, saved values)
 * @nss_cfg: NSS/chains config to update in place (caller pre-fills with ini)
 * @ch_freq: vdev operating channel frequency
 *
 * Returns true and overwrites the 5 GHz Rx/Tx chain and NSS fields with the
 * N79 limits when all conditions hold: N79 is active and at least one of the
 * four fields exceeds the stored N79 limit.  Also marks
 * the 5 GHz band state BAND_REQ_FORCE so WMI sends USER_FORCE to FW.
 * Returns false without modifying @nss_cfg otherwise.
 */
bool wlan_coex_n79_update_nss_chains(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_vdev *vdev,
				     struct wlan_mlme_nss_chains *nss_cfg,
				     uint32_t ch_freq);
#else
static inline bool
wlan_coex_n79_is_active(struct wlan_objmgr_psoc *psoc)
{
	return false;
}

static inline bool
wlan_coex_n79_update_nss_chains(struct wlan_objmgr_psoc *psoc,
				struct wlan_objmgr_vdev *vdev,
				struct wlan_mlme_nss_chains *nss_cfg,
				uint32_t ch_freq)
{
	return false;
}
#endif /* FEATURE_N79_COEX */
#endif
