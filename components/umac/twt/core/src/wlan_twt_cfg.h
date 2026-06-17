/*
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

#ifndef _WLAN_TWT_CFG_H
#define _WLAN_TWT_CFG_H

#include <wlan_objmgr_psoc_obj.h>

#if defined(WLAN_SUPPORT_TWT) && defined(WLAN_TWT_CONV_SUPPORTED)
/**
 * wlan_twt_cfg_init() - Initialize twt config params
 * @psoc: Pointer to global psoc
 *
 * This function initializes the twt private cfg params
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_twt_cfg_init(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_twt_cfg_deinit() - De-initialize twt config params
 * @psoc: Pointer to global psoc
 *
 * This function de-initializes the twt private cfg params
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_twt_cfg_deinit(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_twt_cfg_update() - Update twt config params
 * @psoc: Pointer to global psoc
 *
 * This function updates the cfg param structure based on the
 * intersection of target capabilities and other cfg params
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_twt_cfg_update(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_twt_cfg_get_requestor() - get cfg requestor
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_requestor(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_set_requestor() - set cfg requestor
 * @psoc: Pointer to global psoc
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_requestor(struct wlan_objmgr_psoc *psoc, bool val);

/**
 * wlan_twt_cfg_get_responder() - get cfg responder
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_responder(struct wlan_objmgr_psoc *psoc, uint8_t *val);

/**
 * wlan_twt_cfg_reset_responder() - Reset cfg responder
 * @psoc: psoc
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_reset_responder(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_twt_cfg_set_responder() - set cfg responder
 * @psoc: Pointer to global psoc
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_responder(struct wlan_objmgr_psoc *psoc, uint8_t val);

/**
 * wlan_twt_get_responder_support_for_ht_vht_mode() - Get twt responder
 * support for ht/vht mode
 * @psoc: Pointer to global psoc
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_get_responder_support_for_ht_vht_mode(struct wlan_objmgr_psoc *psoc,
					       bool *val);

/**
 * wlan_twt_cfg_is_twt_enabled() - API to check if TWT is enabled
 * @psoc: Pointer to PSOC object
 *
 * Return: True if TWT is enabled else false
 */
bool wlan_twt_cfg_is_twt_enabled(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_twt_cfg_get_congestion_timeout() - get congestion timeout
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_congestion_timeout(struct wlan_objmgr_psoc *psoc,
				    uint32_t *val);

/**
 * wlan_twt_cfg_get_voip_pkt_ul_delay() - get VOIP uplink packet offset (UPO)
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_voip_pkt_ul_delay(struct wlan_objmgr_psoc *psoc,
				   uint32_t *val);

/**
 * wlan_twt_cfg_set_congestion_timeout() - set congestion timeout
 * @psoc: Pointer to global psoc
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_congestion_timeout(struct wlan_objmgr_psoc *psoc,
				    uint32_t val);

/**
 * wlan_twt_cfg_get_congestion_timeout_per_mac() - get congestion timeout
 * for specific MAC
 * @psoc: Pointer to global psoc
 * @mac_id: MAC ID (0 or 1)
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_congestion_timeout_per_mac(struct wlan_objmgr_psoc *psoc,
					    uint8_t mac_id,
					    uint32_t *val);

/**
 * wlan_twt_cfg_set_congestion_timeout_per_mac() - set congestion timeout
 * for specific MAC
 * @psoc: Pointer to global psoc
 * @mac_id: MAC ID (0 or 1)
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_congestion_timeout_per_mac(struct wlan_objmgr_psoc *psoc,
					    uint8_t mac_id,
					    uint32_t val);

/**
 * wlan_twt_cfg_reset_congestion_timeout_per_mac_to_ini() - Reset congestion
 * timeout for specific MAC to INI configured value
 * @psoc: Pointer to global psoc
 * @mac_id: MAC ID (0 or 1)
 *
 * This function resets the twt_congestion_timeout parameter for the specified
 * MAC to the value configured in INI (CFG_TWT_CONGESTION_TIMEOUT).
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_reset_congestion_timeout_per_mac_to_ini(
					struct wlan_objmgr_psoc *psoc,
					uint8_t mac_id);

/**
 * wlan_twt_cfg_get_vdev_congestion_timeout() - Get per-vdev congestion timeout
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_vdev_congestion_timeout(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id, uint32_t *val);

/**
 * wlan_twt_cfg_set_vdev_congestion_timeout() - Set per-vdev congestion timeout
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 * @val: value to set (0 = TWT active; INI value = TWT inactive)
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_vdev_congestion_timeout(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id, uint32_t val);

/**
 * wlan_twt_cfg_reset_vdev_congestion_timeout_to_ini() - Reset per-vdev
 * congestion timeout to INI configured value
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_reset_vdev_congestion_timeout_to_ini(struct wlan_objmgr_psoc *psoc,
						  uint8_t vdev_id);

/**
 * wlan_twt_cfg_get_requestor_flag() - get requestor flag
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_requestor_flag(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_set_requestor_flag() - set requestor flag
 * @psoc: Pointer to global psoc
 * @val: value to be set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_requestor_flag(struct wlan_objmgr_psoc *psoc, bool val);

/**
 * wlan_twt_cfg_get_vdev_requestor_flag() - Get per-vdev TWT requestor flag
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 * @val: pointer to output variable; set to true if TWT requestor is enabled
 *       for this vdev (vdev-level TWT path)
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_vdev_requestor_flag(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id, bool *val);

/**
 * wlan_twt_cfg_set_vdev_requestor_flag() - Set per-vdev TWT requestor flag
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 * @val: true to enable, false to disable TWT requestor for this vdev
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_set_vdev_requestor_flag(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id, bool val);

/**
 * wlan_twt_cfg_get_responder_flag() - This API intersects TWT responder flag
 * from VDEV and MAC
 * @psoc: Pointer to global psoc
 * @vdev_id: VDEV ID
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_responder_flag(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id,
				bool *val);

/**
 * wlan_twt_cfg_get_flex_sched() - get flex scheduling
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_flex_sched(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_get_24ghz_enabled() - get 24ghz enable
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_24ghz_enabled(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_get_bcast_requestor() - get bcast requestor
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_bcast_requestor(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_get_bcast_responder() - get bcast responder
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_bcast_responder(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_get_rtwt_requestor() - get rtwt requestor
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_rtwt_requestor(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_cfg_get_twt_disabled_on_scan() - get twt_disabled_on_scan value
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_twt_disabled_on_scan(struct wlan_objmgr_psoc *psoc,
				      bool *val);

/**
 * wlan_twt_cfg_get_rtwt_responder() - get rtwt responder
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_cfg_get_rtwt_responder(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_get_requestor_support_for_ht_vht_mode() - Get TWT requestor support
 * for ht/vht mode
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_get_requestor_support_for_ht_vht_mode(struct wlan_objmgr_psoc *psoc,
					       bool *val);
/**
 * wlan_twt_get_restricted_support() - Get rTWT support
 * @psoc: Pointer to global psoc
 * @val: pointer to output variable
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_twt_get_restricted_support(struct wlan_objmgr_psoc *psoc, bool *val);

/**
 * wlan_twt_get_pmo_allowed() - Get pmo allowed
 * @psoc: psoc handler
 *
 * Return: True if twt pmo is allowed otherwise false
 */
bool
wlan_twt_get_pmo_allowed(struct wlan_objmgr_psoc *psoc);
#else

static inline QDF_STATUS wlan_twt_cfg_init(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS wlan_twt_cfg_deinit(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS wlan_twt_cfg_update(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_requestor(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_responder(struct wlan_objmgr_psoc *psoc, uint8_t *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_reset_responder(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_responder(struct wlan_objmgr_psoc *psoc, uint8_t val)
{
	return QDF_STATUS_SUCCESS;
}

static inline bool
wlan_twt_cfg_is_twt_enabled(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_congestion_timeout(struct wlan_objmgr_psoc *psoc,
				    uint32_t *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_voip_pkt_ul_delay(struct wlan_objmgr_psoc *psoc,
				   uint32_t *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_congestion_timeout(struct wlan_objmgr_psoc *psoc,
				    uint32_t val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_congestion_timeout_per_mac(struct wlan_objmgr_psoc *psoc,
					    uint8_t mac_id,
					    uint32_t *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_congestion_timeout_per_mac(struct wlan_objmgr_psoc *psoc,
					    uint8_t mac_id,
					    uint32_t val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_reset_congestion_timeout_per_mac_to_ini(
					struct wlan_objmgr_psoc *psoc,
					uint8_t mac_id)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_vdev_congestion_timeout(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id, uint32_t *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_vdev_congestion_timeout(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id, uint32_t val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_reset_vdev_congestion_timeout_to_ini(struct wlan_objmgr_psoc *psoc,
						  uint8_t vdev_id)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_requestor_flag(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_requestor_flag(struct wlan_objmgr_psoc *psoc, bool val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_vdev_requestor_flag(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id, bool *val)
{
	*val = false;
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_set_vdev_requestor_flag(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id, bool val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_responder_flag(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id,
				bool *val)
{
	*val = false;
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
wlan_twt_cfg_get_flex_sched(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_24ghz_enabled(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_bcast_requestor(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_cfg_get_bcast_responder(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_get_requestor_support_for_ht_vht_mode(struct wlan_objmgr_psoc *psoc,
					       bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_get_responder_support_for_ht_vht_mode(struct wlan_objmgr_psoc *psoc,
					       bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
wlan_twt_get_restricted_support(struct wlan_objmgr_psoc *psoc, bool *val)
{
	return QDF_STATUS_SUCCESS;
}

static inline bool
wlan_twt_get_pmo_allowed(struct wlan_objmgr_psoc *psoc)
{
	return true;
}
#endif

#endif /* End of _WLAN_TWT_CFG_H */
