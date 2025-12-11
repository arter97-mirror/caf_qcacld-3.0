/*
 * Copyright (c) 2011-2014, 2016, 2018-2021 The Linux Foundation. All rights reserved.
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

/*
 *
 * This file lim_prop_exts_utils.h contains the definitions
 * used by all LIM modules to support proprietary features.
 * Author:        Chandra Modumudi
 * Date:          12/11/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 *
 */

#ifndef __LIM_PROP_EXTS_UTILS_H
#define __LIM_PROP_EXTS_UTILS_H

#define LIM_ADAPTIVE_11R_OUI      "\x00\x40\x96\x2C"
#define LIM_ADAPTIVE_11R_OUI_SIZE 4

/**
 * lim_extract_ap_capability() - extract AP's HCF/WME/WSM capability
 * @mac_ctx: Pointer to Global MAC structure
 * @session: A pointer to session entry.
 * @bss_desc: BSS descriptor pointer
 * @qos_cap: Bits are set according to capabilities
 * @uapsd: pointer to uapsd
 * @local_constraint: Pointer to local power constraint.
 * @is_pwr_constraint: Check for Power constraint bit in beacon
 *
 * This function is called to extract AP's HCF/WME/WSM capability
 * from the IEs received from it in Beacon/Probe Response frames
 *
 * Return: QDF_STATUS
 */
QDF_STATUS lim_extract_ap_capability(struct mac_context *mac_ctx,
				     struct pe_session *session,
				     struct bss_description *bss_desc,
				     uint8_t *qos_cap, uint8_t *uapsd,
				     int8_t *local_constraint,
				     bool *is_pwr_constraint);

#ifdef WLAN_FEATURE_11BE
/**
 * lim_extract_eht_op() - Extract and process EHT Operation IE parameters
 * @mac: Pointer to MAC context
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function extracts EHT (Extremely High Throughput / 802.11be) Operation
 * IE parameters from the AP's beacon and configures the session's operating
 * bandwidth accordingly. It handles:
 * - Channel width determination (20/40/80/160/320 MHz)
 * - Center frequency segment configuration (CCFS0 and CCFS1)
 * - Firmware capability validation and bandwidth adjustment
 *
 * The function validates the AP's advertised EHT channel width against the
 * firmware's maximum supported bandwidth and adjusts downward if necessary.
 * For example, if the AP advertises 320MHz but firmware only supports 160MHz,
 * the session will be configured for 160MHz operation.
 *
 * EHT introduces support for 320MHz bandwidth in the 6GHz band, which is
 * handled by this function along with legacy bandwidths.
 *
 * Context: Called during AP capability extraction for EHT-capable connections
 *
 * Return: None (void function - updates session structure with EHT parameters)
 */
void lim_extract_eht_op(struct mac_context *mac,
			struct pe_session *session,
			tDot11fBeaconIEs *bcn_ies);
#else
static inline
void lim_extract_eht_op(struct mac_context *mac,
			struct pe_session *session,
			tDot11fBeaconIEs *bcn_ies)
{}
#endif

ePhyChanBondState lim_get_htcb_state(ePhyChanBondState aniCBMode);

/**
 * lim_update_he_mcs_12_13_map() - update he_mcs_12_13_map in vdev object
 * @psoc: Pointer to Global MAC structure
 * @vdev_id: vdev id
 * @he_mcs_12_13_map: he mcs 12/13 map
 *
 * Return: None
 */
void lim_update_he_mcs_12_13_map(struct wlan_objmgr_psoc *psoc,
				 uint8_t vdev_id, uint16_t he_mcs_12_13_map);

#ifdef WLAN_FEATURE_11BE_MLO
void lim_objmgr_update_emlsr_caps(struct wlan_objmgr_psoc *psoc,
				  uint8_t vdev_id, tpSirAssocRsp assoc_rsp);
#else
static inline
void lim_objmgr_update_emlsr_caps(struct wlan_objmgr_psoc *psoc,
				  uint8_t vdev_id, tpSirAssocRsp assoc_rsp)
{
}
#endif /* WLAN_FEATURE_11BE_MLO */
#endif /* __LIM_PROP_EXTS_UTILS_H */
