/*
 * Copyright (c) 2011-2021 The Linux Foundation. All rights reserved.
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
 * This file lim_prop_exts_utils.cc contains the utility functions
 * to populate, parse proprietary extensions required to
 * support ANI feature set.
 *
 * Author:        Chandra Modumudi
 * Date:          11/27/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 *
 */
#include "ani_global.h"
#include "wni_cfg.h"
#include "sir_common.h"
#include "sir_debug.h"
#include "utils_api.h"
#include "lim_api.h"
#include "lim_types.h"
#include "lim_utils.h"
#include "lim_assoc_utils.h"
#include "lim_prop_exts_utils.h"
#include "lim_ser_des_utils.h"
#include "lim_trace.h"
#include "lim_ft_defs.h"
#include "lim_session.h"
#include "wma.h"
#include "wlan_mlme_api.h"
#include "wlan_utility.h"
#include "wlan_mlo_mgr_sta.h"

#ifdef FEATURE_WLAN_ESE
/**
 * get_local_power_constraint_probe_response() - extracts local constraint
 * from probe response
 * @bcn_ies: Pointer to  beacon IEs
 * @local_constraint: local constraint pointer
 *
 * Return: None
 */
static inline
void get_local_power_constraint_probe_response(tDot11fBeaconIEs *bcn_ies,
					       int8_t *local_constraint)
{
	if (bcn_ies->ESEVersion.present)
		*local_constraint = bcn_ies->ESETxmitPower.power_limit;
}

/**
 * get_ese_version_ie_probe_response() - extracts ESE version IE
 * from probe response
 * @mac_ctx: MAC context
 * @session: A pointer to session entry.
 * @bcn_ies: Pointer to beacon IEs
 *
 * Return: None
 */
static inline
void get_ese_version_ie_probe_response(struct mac_context *mac_ctx,
				       struct pe_session *session,
				       tDot11fBeaconIEs *bcn_ies)
{
	if (mac_ctx->mlme_cfg->lfr.ese_enabled)
		session->is_ese_version_ie_present =
					bcn_ies->ESEVersion.present;
}
#else
static inline
void get_local_power_constraint_probe_response(tDot11fBeaconIEs *bcn_ies,
					       int8_t *local_constraint)
{

}

static inline
void get_ese_version_ie_probe_response(struct mac_context *mac_ctx,
				       struct pe_session *session,
				       tDot11fBeaconIEs *bcn_ies)
{
}
#endif

#ifdef WLAN_FEATURE_11AX
/**
 * lim_extract_he_op() - Extract and process HE Operation IE parameters
 * @mac: Pointer to MAC context
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function extracts HE Operation IE parameters from the AP's beacon and
 * updates the session's operating parameters accordingly. It handles special
 * processing for 6GHz band operations including:
 * - Channel width determination (20/40/80/160/80+80 MHz)
 * - Center frequency segment configuration
 * - AP power type extraction for 6GHz regulatory compliance
 * - Firmware capability validation and bandwidth adjustment
 *
 * For 6GHz band, the function extracts the 6GHz Operation Information field
 * which contains channel width, center frequencies, and regulatory information.
 * It validates the configuration against firmware capabilities and adjusts
 * bandwidth if necessary (e.g., downgrading from 160MHz to 80MHz if firmware
 * doesn't support wider bandwidths).
 *
 * Context: Called during AP capability extraction for HE-capable connections
 *
 * Return: None (void function - updates session structure with HE parameters)
 */
static void lim_extract_he_op(struct mac_context *mac,
			      struct pe_session *session,
			      tDot11fBeaconIEs *bcn_ies)
{
	enum phy_ch_width fw_vht_ch_wd, ap_bcon_ch_width;
	uint8_t center_freq_diff;
	uint32_t self_cb_mode;

	if (!session->he_capable || !bcn_ies->he_op.present)
		return;

	qdf_mem_copy(&session->he_op, &bcn_ies->he_op, sizeof(session->he_op));
	if (!session->he_6ghz_band)
		return;

	self_cb_mode = lim_get_cb_mode_for_freq(mac, session,
						session->curr_op_freq);
	if (self_cb_mode == WNI_CFG_CHANNEL_BONDING_MODE_DISABLE)
		return;

	if (!session->he_op.oper_info_6g_present) {
		session->ap_defined_power_type_6g = REG_CURRENT_MAX_AP_TYPE;
		return;
	}

	session->ch_width = session->he_op.oper_info_6g.info.ch_width;
	session->ch_center_freq_seg0 =
		session->he_op.oper_info_6g.info.center_freq_seg0;
	session->ch_center_freq_seg1 =
		session->he_op.oper_info_6g.info.center_freq_seg1;
	session->ap_defined_power_type_6g =
		session->he_op.oper_info_6g.info.reg_info;
	if (lim_is_ap_power_type_6g_invalid(session)) {
		session->ap_defined_power_type_6g = REG_CURRENT_MAX_AP_TYPE;
		pe_debug("AP power type invalid, defaulting to MAX_AP_TYPE");
	}

	pe_debug("6G op info: ch_wd %d cntr_freq_seg0 %d cntr_freq_seg1 %d",
		 session->ch_width, session->ch_center_freq_seg0,
		 session->ch_center_freq_seg1);

	fw_vht_ch_wd = mlme_get_vht_ch_width();

	/*
	 * Per IEEE 802.11ax, 6GHz Operation Information ch_width field:
	 *   0 = 20 MHz, 1 = 40 MHz, 2 = 80 MHz, 3 = 160 or 80+80 MHz
	 *
	 * For ch_width 0/1/2 (20/40/80 MHz), CCFS1 is always 0 and
	 * ch_width already maps directly to enum phy_ch_width.
	 *
	 * For ch_width 3 (160/80+80 MHz), use CCFS1 and center_freq_diff
	 * to distinguish between 160 MHz and 80+80 MHz.
	 */
	if (session->ch_width < CH_WIDTH_160MHZ) {
		/* 20/40/80 MHz: ch_width maps directly, CCFS1 unused */
		session->ch_center_freq_seg1 = 0;
	} else {
		/* ch_width == 3: 160 or 80+80 MHz, resolve via CCFS1 */
		if (!session->ch_center_freq_seg1) {
			/* No CCFS1 present, fall back to 80 MHz */
			session->ch_width = CH_WIDTH_80MHZ;
		} else {
			/*
			 * |CCFS1 - CCFS0| == 8 channels (40 MHz offset):
			 *   160 MHz (CCFS1 = center of 160 MHz channel)
			 * |CCFS1 - CCFS0| > 16 channels (>80 MHz offset):
			 *   80+80 MHz (CCFS1 = center of secondary 80 MHz)
			 */
			center_freq_diff = abs(session->ch_center_freq_seg1 -
					       session->ch_center_freq_seg0);
			if (center_freq_diff == 8) {
				ap_bcon_ch_width = CH_WIDTH_160MHZ;
			} else if (center_freq_diff > 16) {
				ap_bcon_ch_width = CH_WIDTH_80P80MHZ;
			} else {
				pe_debug("vdev %d: Invalid center freq diff %d with CCFS1 present, falling back to 80MHz",
					 session->vdev_id, center_freq_diff);
				ap_bcon_ch_width = CH_WIDTH_80MHZ;
				session->ch_center_freq_seg1 = 0;
			}
			session->ch_width = ap_bcon_ch_width;
		}
	}

	/* Cap ch_width to FW max supported BW */
	if (session->ch_width > fw_vht_ch_wd) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT " ch_width %d exceeds FW max %d, capping",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width, fw_vht_ch_wd);
		/*
		 * 80+80 MHz cannot be downgraded to any other width as the
		 * channel configuration differs; fall back to 80 MHz instead.
		 */
		if (session->ch_width == CH_WIDTH_80P80MHZ &&
		    fw_vht_ch_wd != CH_WIDTH_80P80MHZ)
			session->ch_width = CH_WIDTH_80MHZ;
		else
			session->ch_width = fw_vht_ch_wd;
		/* Clear CCFS1 only when capping to 80MHz or below;
		 * 160MHz still requires a valid CCFS1.
		 */
		if (session->ch_width <= CH_WIDTH_80MHZ)
			session->ch_center_freq_seg1 = 0;
	}

	/* Cap ch_width to AP's max supported BW */
	if (session->ap_ch_width != CH_WIDTH_INVALID &&
	    session->ch_width > session->ap_ch_width) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT " ch_width %d exceeds ap_ch_width %d, capping",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width, session->ap_ch_width);
		session->ch_width = session->ap_ch_width;
		/* Clear CCFS1 only when capping to 80MHz or below;
		 * 160MHz still requires a valid CCFS1.
		 */
		if (session->ap_ch_width <= CH_WIDTH_80MHZ)
			session->ch_center_freq_seg1 = 0;
	}
}

static bool lim_validate_he160_mcs_map(struct pe_session *session,
				       uint16_t peer_rx_mcs,
				       uint16_t peer_tx_mcs)
{
	uint16_t rx_he_mcs_map;
	uint16_t tx_he_mcs_map;
	uint16_t he_mcs_map;
	struct mac_context *mac_ctx = session->mac_ctx;

	he_mcs_map = *((uint16_t *)mac_ctx->mlme_cfg->he_caps.dot11_he_cap.
				tx_he_mcs_map_160);
	tx_he_mcs_map = HE_INTERSECT_MCS(peer_rx_mcs, he_mcs_map);

	he_mcs_map = *((uint16_t *)mac_ctx->mlme_cfg->he_caps.dot11_he_cap.
				rx_he_mcs_map_160);
	rx_he_mcs_map = HE_INTERSECT_MCS(peer_tx_mcs, he_mcs_map);

	rx_he_mcs_map |= HE_DISABLE_MCS_OVER_NSS(session->cap_rx_nss);
	tx_he_mcs_map |= HE_DISABLE_MCS_OVER_NSS(session->cap_tx_nss);

	return ((rx_he_mcs_map != HE_MCS_ALL_DISABLED) &&
		(tx_he_mcs_map != HE_MCS_ALL_DISABLED));
}

/**
 * lim_check_is_he_mcs_valid() - Validate HE MCS map compatibility
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function validates whether the AP's advertised HE MCS map is compatible
 * with the STA's transmit NSS capability. It checks if there is at least one
 * valid MCS rate available for communication in the <80MHz bandwidth.
 *
 * If the AP doesn't support HE or if all MCS rates are disabled for the STA's
 * NSS capability, the function downgrades the connection mode to VHT (11ac) or
 * HT (11n) depending on VHT capability.
 *
 * Context: Called during AP capability extraction to ensure HE mode viability
 *
 * Return: None (void function - updates session structure on validation failure)
 */
static void lim_check_is_he_mcs_valid(struct pe_session *session,
				      tDot11fBeaconIEs *bcn_ies)
{
	uint16_t mcs_map = HE_MCS_ALL_DISABLED;

	if (!session->he_capable)
		return;

	if (!bcn_ies->he_cap.present)
		goto downgrade_11ac;

	mcs_map = bcn_ies->he_cap.rx_he_mcs_map_lt_80;

	if (session->cap_tx_nss)
		mcs_map |= HE_DISABLE_MCS_OVER_NSS(session->cap_tx_nss);

	if (mcs_map != HE_MCS_ALL_DISABLED)
		return;

downgrade_11ac:
	session->he_capable = false;
	lim_update_session_eht_capable(session, false);
	if (session->vhtCapability)
		session->dot11mode = MLME_DOT11_MODE_11AC;
	else
		session->dot11mode = MLME_DOT11_MODE_11N;
	pe_err("vdev %d: Invalid LT80 MCS map 0x%x with NSS %d, fallback to dot11mode %d",
	       session->vdev_id, mcs_map, session->cap_tx_nss,
	       session->dot11mode);
}


/**
 * lim_process_he_capability_validation() - Process and validate HE capability
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function processes the HE Capability IE from the AP's beacon/probe
 * response and determines the AP's maximum supported HE channel width cap.
 * It validates the claimed bandwidth against the HE MCS maps per IEEE 802.11ax.
 *
 * HE Channel Width Bits per Band:
 * - 2.4 GHz: chan_width_0 for 40 MHz
 * - 5 GHz: chan_width_1 for 40/80 MHz, chan_width_2 for 160 MHz
 * - 6 GHz: chan_width_5 for 160 MHz, chan_width_6 for 80 MHz
 *
 * The function stores the validated maximum bandwidth in session->ap_ch_width.
 * Non-bandwidth-related parameters remain unchanged.
 *
 * Note: This function assumes HE capability and LDPC validation have already
 * been performed by lim_check_is_he_mcs_valid() and
 * lim_check_peer_ldpc_and_update().
 * It only determines the AP's maximum channel width capability.
 *
 * Context: Called during HE capability processing after initial validations
 *
 * Return: None (void function - updates session structure directly)
 */
static void lim_process_he_capability_validation(struct pe_session *session,
						 tDot11fBeaconIEs *bcn_ies)
{
	enum phy_ch_width ap_ch_width;
	tDot11fIEhe_cap *he_cap;

	if (!session->he_capable || !bcn_ies->he_cap.present)
		return;

	he_cap = &bcn_ies->he_cap;

	ap_ch_width = lim_get_he_max_ch_width(he_cap, session);
	if (ap_ch_width == CH_WIDTH_INVALID) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT " Invalid HE channel width, keeping existing value",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId));
		return;
	}

	session->ap_ch_width = ap_ch_width;
}

void lim_update_he_bw_cap_mcs(struct pe_session *session,
			      tDot11fBeaconIEs *bcn_ies)
{
	uint8_t is_80mhz;
	uint8_t sta_prefer_80mhz_over_160mhz;

	/* session->he_capable reflects the STA’s own HE capability */
	if (!session->he_capable)
		return;

	sta_prefer_80mhz_over_160mhz =
		session->mac_ctx->mlme_cfg->sta.sta_prefer_80mhz_over_160mhz;
	if ((session->opmode == QDF_STA_MODE ||
	     session->opmode == QDF_P2P_CLIENT_MODE) &&
	    bcn_ies && bcn_ies->he_cap.present) {
		if (!bcn_ies->he_cap.chan_width_2) {
			is_80mhz = 1;
		} else if (bcn_ies->he_cap.chan_width_2 &&
			   !lim_validate_he160_mcs_map(session,
			   *((uint16_t *)bcn_ies->he_cap.rx_he_mcs_map_160),
			   *((uint16_t *)bcn_ies->he_cap.tx_he_mcs_map_160))) {
			is_80mhz = 1;
			if (session->ch_width == CH_WIDTH_160MHZ) {
				pe_debug("HE160 Rx/Tx MCS is not valid, falling back to 80MHz");
				session->ch_width = CH_WIDTH_80MHZ;
			}
		} else if (sta_prefer_80mhz_over_160mhz ==
				STA_PREFER_BW_80MHZ) {
			is_80mhz = 1;
			if (session->ch_width == CH_WIDTH_160MHZ) {
				pe_debug("STA preferred HE80 over HE160, falling back to 80MHz");
				session->ch_width = CH_WIDTH_80MHZ;
			}
		} else {
			is_80mhz = 0;
		}
	} else {
		is_80mhz = 1;
	}

	if (session->ch_width <= CH_WIDTH_80MHZ && is_80mhz) {
		session->he_config.chan_width_2 = 0;
		session->he_config.chan_width_3 = 0;
	} else if (session->ch_width == CH_WIDTH_160MHZ) {
		session->he_config.chan_width_3 = 0;
	}
	/* Reset the > 20MHz caps for 20MHz connection */
	if (session->ch_width == CH_WIDTH_20MHZ) {
		session->he_config.chan_width_0 = 0;
		session->he_config.chan_width_1 = 0;
		session->he_config.chan_width_2 = 0;
		session->he_config.chan_width_3 = 0;
		session->he_config.chan_width_4 = 0;
		session->he_config.chan_width_5 = 0;
		session->he_config.chan_width_6 = 0;
		session->he_config.he_ppdu_20_in_40Mhz_2G = 0;
		session->he_config.he_ppdu_20_in_160_80p80Mhz = 0;
		session->he_config.he_ppdu_80_in_160_80p80Mhz = 0;
	}
	if (WLAN_REG_IS_24GHZ_CH_FREQ(session->curr_op_freq)) {
		session->he_config.chan_width_1 = 0;
		session->he_config.chan_width_2 = 0;
		session->he_config.chan_width_3 = 0;
		session->he_config.chan_width_5 = 0;
		session->he_config.chan_width_6 = 0;
	} else {
		session->he_config.chan_width_0 = 0;
		session->he_config.chan_width_4 = 0;
		session->he_config.chan_width_6 = 0;
	}
	if (!session->he_config.chan_width_2) {
		session->he_config.bfee_sts_gt_80 = 0;
		session->he_config.num_sounding_gt_80 = 0;
		session->he_config.he_ppdu_20_in_160_80p80Mhz = 0;
		session->he_config.he_ppdu_80_in_160_80p80Mhz = 0;
		*(uint16_t *)session->he_config.rx_he_mcs_map_160 =
							HE_MCS_ALL_DISABLED;
		*(uint16_t *)session->he_config.tx_he_mcs_map_160 =
							HE_MCS_ALL_DISABLED;
	}
	if (!session->he_config.chan_width_3) {
		*(uint16_t *)session->he_config.rx_he_mcs_map_80_80 =
							HE_MCS_ALL_DISABLED;
		*(uint16_t *)session->he_config.tx_he_mcs_map_80_80 =
							HE_MCS_ALL_DISABLED;
	}

	if (bcn_ies)
		pe_debug("Session width %d, AP: he_cap %d wd_2 %d is_80 %d",
			 session->ch_width, bcn_ies->he_cap.present,
			 bcn_ies->he_cap.chan_width_2, is_80mhz);
	lim_print_he_channel_widths(&session->he_config);
}

void lim_update_he_mcs_12_13_map(struct wlan_objmgr_psoc *psoc,
				 uint8_t vdev_id, uint16_t he_mcs_12_13_map)
{
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						    WLAN_LEGACY_MAC_ID);
	if (!vdev) {
		pe_err("vdev not found for id: %d", vdev_id);
		return;
	}
	wlan_vdev_obj_lock(vdev);
	wlan_vdev_mlme_set_he_mcs_12_13_map(vdev, he_mcs_12_13_map);
	wlan_vdev_obj_unlock(vdev);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_LEGACY_MAC_ID);
}
#else
static inline void lim_extract_he_op(struct mac_context *mac,
				     struct pe_session *session,
				     tDot11fBeaconIEs *bcn_ies)
{}

static inline void lim_check_is_he_mcs_valid(struct pe_session *session,
					     tDot11fBeaconIEs *bcn_ies)
{
}

void lim_update_he_mcs_12_13_map(struct wlan_objmgr_psoc *psoc,
				 uint8_t vdev_id, uint16_t he_mcs_12_13_map)
{
}
#endif

#ifdef WLAN_FEATURE_11BE
void lim_extract_eht_op(struct mac_context *mac,
			struct pe_session *session,
			tDot11fBeaconIEs *bcn_ies)
{
	uint32_t max_eht_bw;
	uint32_t self_cb_mode;
	enum phy_ch_width fw_max_ch_width;

	if (!session->eht_capable || !bcn_ies->eht_op.present ||
	    !bcn_ies->eht_op.eht_op_information_present)
		return;

	qdf_mem_copy(&session->eht_op, &bcn_ies->eht_op,
		     sizeof(session->eht_op));

	self_cb_mode = lim_get_cb_mode_for_freq(mac, session,
						session->curr_op_freq);
	if (self_cb_mode == WNI_CFG_CHANNEL_BONDING_MODE_DISABLE)
		return;

	max_eht_bw = wma_get_eht_ch_width();

	/* Step 1 : Get BW from EHT ops */
	if (session->eht_op.channel_width == WLAN_EHT_CHWIDTH_320) {
		if (max_eht_bw == WNI_CFG_EHT_CHANNEL_WIDTH_320MHZ) {
			session->ch_width = CH_WIDTH_320MHZ;
		} else if (max_eht_bw == WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ) {
			session->ch_width = CH_WIDTH_160MHZ;
		} else {
			session->ch_width = CH_WIDTH_80MHZ;
			session->ch_center_freq_seg1 = 0;
		}
	} else if (session->eht_op.channel_width == WLAN_EHT_CHWIDTH_160) {
		if (max_eht_bw >= WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ) {
			session->ch_width = CH_WIDTH_160MHZ;
		} else {
			session->ch_width = CH_WIDTH_80MHZ;
			session->ch_center_freq_seg1 = 0;
		}
	} else if (session->eht_op.channel_width == WLAN_EHT_CHWIDTH_80) {
		session->ch_width = CH_WIDTH_80MHZ;
		session->ch_center_freq_seg1 = 0;
	} else if (session->eht_op.channel_width == WLAN_EHT_CHWIDTH_40) {
		session->ch_width = CH_WIDTH_40MHZ;
		session->ch_center_freq_seg1 = 0;
	} else {
		session->ch_width = CH_WIDTH_20MHZ;
		session->ch_center_freq_seg1 = 0;
	}

	/*
	 * Step 2: Validate CCFS1 presence for >80 MHz
	 */
	if ((session->ch_width == CH_WIDTH_160MHZ ||
	     session->ch_width == CH_WIDTH_320MHZ) && !session->eht_op.ccfs1) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT" CCFS1 absent, downgrading ch_width %d to 80MHz",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width);
		session->ch_width = CH_WIDTH_80MHZ;
	}

	/* Step 3: Cap ch_width to FW max supported BW */
	fw_max_ch_width = wlan_mlme_get_max_bw();
	if (session->ch_width > fw_max_ch_width) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT" ch_width %d exceeds FW max %d, capping",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width, fw_max_ch_width);
		session->ch_width = fw_max_ch_width;
	}

	/* Step 4: Cap ch_width to AP's max supported BW */
	if (session->ap_ch_width != CH_WIDTH_INVALID &&
	    session->ch_width > session->ap_ch_width) {
		pe_debug("vdev %d: AP " QDF_MAC_ADDR_FMT" ch_width %d exceeds ap_ch_width %d, capping",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width, session->ap_ch_width);
		session->ch_width = session->ap_ch_width;
	}

	/* Step 4: Set CCFS0/1 as per final BW */
	session->ch_center_freq_seg0 = session->eht_op.ccfs0;
	if (session->ch_width > CH_WIDTH_80MHZ)
		session->ch_center_freq_seg1 = session->eht_op.ccfs1;
	else
		session->ch_center_freq_seg1 = 0;
}

void lim_update_eht_bw_cap_mcs(struct pe_session *session,
			       tDot11fBeaconIEs *bcn_ies)
{
	enum phy_ch_width ap_max_ch_width;
	tDot11fIEeht_cap *eht_cap;

	if (!session->eht_capable)
		return;

	if ((session->opmode == QDF_STA_MODE ||
	     session->opmode == QDF_P2P_CLIENT_MODE) &&
	    bcn_ies && bcn_ies->eht_cap.present) {
		if (!bcn_ies->eht_cap.support_320mhz_6ghz)
			session->eht_config.support_320mhz_6ghz = 0;
		if (!bcn_ies->eht_cap.support_320mhz_6ghz ||
		    !bcn_ies->eht_cap.su_beamformer)
			session->eht_config.num_sounding_dim_320mhz = 0;
	}

	if (!bcn_ies || !bcn_ies->eht_cap.present)
		return;

	eht_cap = &bcn_ies->eht_cap;

	ap_max_ch_width = lim_calculate_ap_max_eht_ch_width(session, eht_cap);
	/*
	 * Only update ap_ch_width if EHT baseline MCS validation passed.
	 * lim_calculate_ap_max_eht_ch_width() returns CH_WIDTH_INVALID when
	 * the baseline EHT MCS validation fails; writing that back would
	 * corrupt the session's ap_ch_width.
	 */
	if (ap_max_ch_width != CH_WIDTH_INVALID)
		session->ap_ch_width = ap_max_ch_width;
	else
		pe_debug("vdev %d: EHT MCS validation failed for AP "
			 QDF_MAC_ADDR_FMT ", not updating ap_ch_width",
			 session->vdev_id,
			 QDF_MAC_ADDR_REF(session->bssId));
}
#endif

#ifdef WLAN_FEATURE_11BE_MLO
void lim_objmgr_update_emlsr_caps(struct wlan_objmgr_psoc *psoc,
				  uint8_t vdev_id, tpSirAssocRsp assoc_rsp)
{
	struct wlan_objmgr_vdev *vdev;
	bool ap_emlsr_cap = false;
	struct wlan_objmgr_vdev *assoc_vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						    WLAN_LEGACY_MAC_ID);
	if (!vdev) {
		pe_err("vdev not found for id: %d", vdev_id);
		return;
	}

	/* Check for assoc link vdev to extract emlsr cap from assoc rsp */
	if (!wlan_vdev_mlme_is_mlo_link_vdev(vdev)) {
		ap_emlsr_cap =
			assoc_rsp->mlo_ie.mlo_ie.eml_capabilities_info.emlsr_support;

		if (!(wlan_vdev_mlme_cap_get(vdev, WLAN_VDEV_C_EMLSR_CAP) &&
		      ap_emlsr_cap)) {
			if (!wlan_vdev_mlme_cap_get(vdev, WLAN_VDEV_C_EMLSR_CAP)
			    && ap_emlsr_cap)
				pe_debug("No eMLSR STA supp but recvd EML caps in assc rsp");
			else
				pe_debug("EML caps not present in assoc rsp");
			wlan_vdev_obj_lock(vdev);
			wlan_vdev_mlme_cap_clear(vdev, WLAN_VDEV_C_EMLSR_CAP);
			wlan_vdev_obj_unlock(vdev);
		} else {
			pe_debug("EML caps present in assoc rsp");
		}
	} else {
		if (wlan_cm_is_vdev_active(vdev) ||
		    wlan_vdev_mlme_is_mlo_link_switch_in_progress(vdev)) {
			pe_debug("no change required for link vdev");
			goto rel_ref;
		}

		assoc_vdev = wlan_mlo_get_assoc_link_vdev(vdev);
		if (assoc_vdev) {
			if (!wlan_vdev_mlme_cap_get(
					assoc_vdev, WLAN_VDEV_C_EMLSR_CAP)) {
				wlan_vdev_obj_lock(vdev);
				wlan_vdev_mlme_cap_clear(
						vdev, WLAN_VDEV_C_EMLSR_CAP);
				wlan_vdev_obj_unlock(vdev);
				pe_debug("Cleared link vdev EML caps.");
			} else {
				pe_debug("no change required for link vdev");
			}
		}
	}

rel_ref:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_LEGACY_MAC_ID);
}
#endif

#ifdef WLAN_ADAPTIVE_11R
/**
 * lim_extract_adaptive_11r_cap() - check if the AP has adaptive 11r
 * IE
 * @ie: Pointer to the IE
 * @ie_len: ie Length
 *
 * Return: True if adaptive 11r IE is present
 */
static bool lim_extract_adaptive_11r_cap(uint8_t *ie, uint16_t ie_len)
{
	const uint8_t *adaptive_ie;
	uint8_t data;
	bool adaptive_11r;

	adaptive_ie = wlan_get_vendor_ie_ptr_from_oui(LIM_ADAPTIVE_11R_OUI,
						      LIM_ADAPTIVE_11R_OUI_SIZE,
						      ie, ie_len);
	if (!adaptive_ie)
		return false;

	if ((adaptive_ie[1] < (OUI_LENGTH + 1)) ||
	    (adaptive_ie[1] > MAX_ADAPTIVE_11R_IE_LEN))
		return false;

	data = *(adaptive_ie + OUI_LENGTH + 2);
	adaptive_11r = (data & 0x1) ? true : false;

	return adaptive_11r;
}

#else
static inline bool lim_extract_adaptive_11r_cap(uint8_t *ie, uint16_t ie_len)
{
	return false;
}
#endif

#ifdef WLAN_FEATURE_11AX
/**
 * lim_check_peer_ldpc_and_update() - Validate LDPC support for HE in 2.4GHz
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function performs LDPC (Low-Density Parity Check) validation for HE
 * connections in the 2.4GHz band. In 2.4GHz, if the AP supports HE with MCS
 * rates up to MCS 11 but does not advertise LDPC support, the connection is
 * downgraded to VHT (11ac) or HT (11n) mode.
 *
 * LDPC is a forward error correction technique that improves link reliability,
 * especially at higher MCS rates. This check ensures robust communication by
 * preventing HE mode when LDPC is not available for high MCS rates in 2.4GHz.
 *
 * Context: Called during AP capability extraction for 2.4GHz band connections
 *
 * Return: None (void function - updates session structure on validation failure)
 */
static void lim_check_peer_ldpc_and_update(struct pe_session *session,
					   tDot11fBeaconIEs *bcn_ies)
{
	/*
	 * In 2.4G if AP supports HE till MCS 0-9 we can associate
	 * with HE mode instead downgrading to 11ac
	 */
	if (session->he_capable &&
	    WLAN_REG_IS_24GHZ_CH_FREQ(session->curr_op_freq) &&
	    bcn_ies->he_cap.present &&
	    lim_check_he_80_mcs11_supp(session, &bcn_ies->he_cap) &&
	    !bcn_ies->he_cap.ldpc_coding) {
		session->he_capable = false;
		pe_err("LDPC check failed for HE operation");
		if (session->vhtCapability) {
			session->dot11mode = MLME_DOT11_MODE_11AC;
			pe_debug("Update dot11mode to 11ac");
		} else {
			session->dot11mode = MLME_DOT11_MODE_11N;
			pe_debug("Update dot11mode to 11N");
		}
	}
}
#else
static inline void lim_check_peer_ldpc_and_update(struct pe_session *session,
						  tDot11fBeaconIEs *bcn_ies)
{}
#endif

static
void lim_update_ch_width_for_p2p_client(struct mac_context *mac,
					struct pe_session *session,
					uint32_t ch_freq)
{
	struct ch_params ch_params = {0};

	if (session->dot11mode < MLME_DOT11_MODE_11AC)
		return;
	/*
	 * Some IOT AP's/P2P-GO's (e.g. make: Wireless-AC 9560160MHz as P2P GO),
	 * send beacon with 20mhz and assoc resp with 80mhz and
	 * after assoc resp, next beacon also has 80mhz.
	 * Connection is expected to happen in better possible
	 * bandwidth(80MHz in this case).
	 * Start the vdev with max supported ch_width in order to support this.
	 * It'll be downgraded to appropriate ch_width or the same would be
	 * continued based on assoc resp.
	 * Restricting this check for p2p client and 5G only and this may be
	 * extended to STA based on wider testing results with multiple AP's.
	 * Limit it to 80MHz as 80+80 is channel specific and 160MHz is not
	 * supported in p2p.
	 */
	ch_params.ch_width = CH_WIDTH_80MHZ;

	wlan_reg_set_channel_params_for_pwrmode(mac->pdev, ch_freq, 0,
						&ch_params,
						REG_CURRENT_PWR_MODE);
	if (ch_params.ch_width == CH_WIDTH_20MHZ)
		ch_params.sec_ch_offset = PHY_SINGLE_CHANNEL_CENTERED;

	session->htSupportedChannelWidthSet = ch_params.sec_ch_offset ? 1 : 0;
	session->htRecommendedTxWidthSet = session->htSupportedChannelWidthSet;
	session->htSecondaryChannelOffset = ch_params.sec_ch_offset;
	session->ch_width = ch_params.ch_width;
	session->ch_center_freq_seg0 = ch_params.center_freq_seg0;
	session->ch_center_freq_seg1 = ch_params.center_freq_seg1;
	pe_debug("Start P2P_CLI in ch freq %d max supported ch_width: %u cbmode: %u seg0: %u, seg1: %u",
		 ch_freq, ch_params.ch_width, ch_params.sec_ch_offset,
		 session->ch_center_freq_seg0, session->ch_center_freq_seg1);
}

#ifdef WLAN_FEATURE_11AX
/**
 * lim_store_sta_he_max_capability() - Store STA's HE maximum capability
 * @session: Pointer to PE session
 * @fw_vht_ch_wd: Firmware VHT channel width capability
 *
 * This function stores the STA's HE maximum bandwidth capability.
 * HE can support the same bandwidth as VHT in 5GHz, or 40MHz in 2.4GHz.
 *
 * Context: Called from lim_store_sta_max_capabilities() to handle
 *          HE-specific capability storage
 *
 * Return: None
 */
static void lim_store_sta_he_max_capability(struct pe_session *session,
					    uint8_t fw_vht_ch_wd)
{
	if (!session->he_capable)
		return;

	/* HE can support same as VHT in 5GHz, or 40MHz in 2.4GHz */
	if (WLAN_REG_IS_24GHZ_CH_FREQ(session->curr_op_freq)) {
		if (fw_vht_ch_wd >= WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ)
			session->sta_max_ch_width = CH_WIDTH_40MHZ;
	} else if (fw_vht_ch_wd >= WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ) {
		session->sta_max_ch_width = CH_WIDTH_160MHZ;
	}
}
#else
static inline void lim_store_sta_he_max_capability(struct pe_session *session,
						   uint8_t fw_vht_ch_wd)
{
}
#endif

#ifdef WLAN_FEATURE_11BE
/**
 * lim_store_sta_eht_max_capability() - Store STA's EHT maximum capability
 * @session: Pointer to PE session
 *
 * This function stores the STA's EHT maximum bandwidth capability.
 * EHT can support up to 320MHz in 6GHz band.
 *
 * Context: Called from lim_store_sta_max_capabilities() to handle
 *          EHT-specific capability storage
 *
 * Return: None
 */
static void lim_store_sta_eht_max_capability(struct pe_session *session)
{
	uint32_t fw_eht_ch_wd;

	if (!session->eht_capable)
		return;

	fw_eht_ch_wd = wma_get_eht_ch_width();

	/* EHT can support up to 320MHz in 6GHz */
	if (WLAN_REG_IS_6GHZ_CHAN_FREQ(session->curr_op_freq)) {
		if (fw_eht_ch_wd == WNI_CFG_EHT_CHANNEL_WIDTH_320MHZ)
			session->sta_max_ch_width = CH_WIDTH_320MHZ;
		else if (fw_eht_ch_wd >= WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ)
			session->sta_max_ch_width = CH_WIDTH_160MHZ;
	}
}
#else
static inline void lim_store_sta_eht_max_capability(struct pe_session *session)
{
}
#endif

/**
 * lim_store_sta_max_capabilities() - Store STA's maximum capabilities
 * @mac_ctx: Pointer to MAC context
 * @session: Pointer to PE session
 *
 * This function stores the STA's maximum bandwidth capabilities before
 * they are intersected with AP capabilities. These preserved values will
 * be used when constructing Association Request frame to advertise STA's
 * true maximum capabilities to the AP.
 *
 * The function queries firmware for maximum supported bandwidth and stores:
 * - VHT maximum capability
 * - HE maximum capability (via lim_store_sta_he_max_capability)
 * - EHT maximum capability (via lim_store_sta_eht_max_capability)
 * - Overall maximum channel width
 *
 * Context: Called before AP capability extraction to preserve STA's
 *          maximum capabilities
 *
 * Return: None
 */
static void lim_store_sta_max_capabilities(struct mac_context *mac_ctx,
					   struct pe_session *session)
{
	uint8_t fw_vht_ch_wd;

	/* Initialize to zero */
	session->sta_max_ch_width = CH_WIDTH_20MHZ;

	/* Get firmware VHT/HE capabilities */
	fw_vht_ch_wd = wma_get_vht_ch_width();

	/* Store VHT maximum capability */
	if (session->vhtCapability) {
		/* Convert to enum phy_ch_width */
		if (fw_vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ ||
		    fw_vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_80_PLUS_80MHZ)
			session->sta_max_ch_width = CH_WIDTH_160MHZ;
		else if (fw_vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ)
			session->sta_max_ch_width = CH_WIDTH_80MHZ;
		else if (fw_vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ)
			session->sta_max_ch_width = CH_WIDTH_40MHZ;
		else
			session->sta_max_ch_width = CH_WIDTH_20MHZ;
	}

	/* Store HE maximum capability */
	lim_store_sta_he_max_capability(session, fw_vht_ch_wd);

	/* Store EHT maximum capability */
	lim_store_sta_eht_max_capability(session);

	pe_debug("STA Max Capabilities: ch_width=%d freq=%d",
		 session->sta_max_ch_width, session->curr_op_freq);
}

/**
 * lim_configure_operating_mode_notification() - Configure Operating Mode
 *                                                Notification parameters
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * This function configures the Operating Mode Notification (OMN) parameters
 * that will be included in the Association Request frame sent to the AP.
 * The OMN IE allows the STA to inform the AP about its intended operating
 * channel width and number of spatial streams (NSS).
 *
 * The function performs the following operations:
 * 1. Checks if the AP supports Operating Mode Notification by examining the
 *    Extended Capability IE in the beacon
 * 2. Configures the operating channel width, capping it at 160MHz even if
 *    the session supports higher bandwidths
 * 3. Configures the RX NSS with special handling for WFA certification
 *    test scenarios
 *
 * Special WFA CERT Handling:
 * When the AP advertises beacon protection capability and operates in 1x1
 * NSS mode, the function uses the vdev's configured NSS (from INI settings)
 * instead of the session's capability NSS. This ensures proper behavior
 * during WFA certification tests where the STA needs to advertise its full
 * NSS capability even when connecting to a 1x1 AP.
 *
 * Normal Operation:
 * In standard scenarios, the function uses the session's RX NSS capability
 * to populate the OMN IE.
 *
 * Prerequisites:
 * - Session must have VHT capability enabled
 * - AP must advertise VHT capability in beacon
 * - Extended Capability IE must be present in beacon
 *
 * Context: This function is called during AP capability extraction phase
 *          when processing beacon/probe response from the target AP
 *
 * Return: None (void function - updates session structure directly)
 */
static void lim_configure_operating_mode_notification(
					struct pe_session *session,
					tDot11fBeaconIEs *bcn_ies)
{
	struct s_ext_cap *ext_cap;
	uint8_t self_tx_nss, self_rx_nss = session->cap_rx_nss;

	if (!session->vhtCapability || !session->vhtCapabilityPresentInBeacon ||
	    !bcn_ies->ExtCap.present)
		return;

	ext_cap = (struct s_ext_cap *)bcn_ies->ExtCap.bytes;
	session->gLimOperatingMode.present = ext_cap->oper_mode_notification;

	if (ext_cap->oper_mode_notification) {
		mlme_get_vdev_nss_by_freq_from_ini(session->vdev,
						   session->curr_op_freq,
						   &self_tx_nss,
						   &self_rx_nss);

		/* Configure channel width, capping at 160MHz */
		if (CH_WIDTH_160MHZ > session->ch_width)
			session->gLimOperatingMode.chanWidth =
					session->ch_width;
		else
			session->gLimOperatingMode.chanWidth =
				CH_WIDTH_160MHZ;

		/**
		 * Populate vdev NSS in OMN IE of assoc request for
		 * WFA CERT test scenario.
		 * Use vdev NSS when:
		 * - AP has beacon protection enabled
		 * - Operating in STA mode
		 * - NSS not forced to 1x1
		 * - AP supports only 1x1 NSS
		 */
		if (ext_cap->beacon_protection_enable &&
		    session->opmode == QDF_STA_MODE &&
		    !session->nss_forced_1x1 &&
		    lim_get_nss_supported_by_ap(&bcn_ies->VHTCaps,
					&bcn_ies->HTCaps,
					&bcn_ies->he_cap) == NSS_1x1_MODE)
			session->gLimOperatingMode.rxNSS = self_rx_nss - 1;
		else
			session->gLimOperatingMode.rxNSS =
					session->cap_rx_nss - 1;
	} else {
		pe_err("AP does not support op_mode rx");
	}
}

/**
 * lim_extract_ap_max_bw_from_vht_caps() - Extract AP max BW from VHT caps
 * @session: Pointer to PE session
 * @vht_caps: Pointer to VHT Capabilities IE
 *
 * Derives the AP's maximum supported channel bandwidth from the VHT
 * Capabilities IE (supportedChannelWidthSet field) and stores it in
 * session->ap_ch_width. Falls back to session->ch_width if the VHT
 * MCS maps are invalid (all spatial streams disabled).
 *
 * Return: None
 */
static void lim_extract_ap_max_bw_from_vht_caps(struct pe_session *session,
						 tDot11fIEVHTCaps *vht_caps)
{
	enum phy_ch_width ap_max_ch_width;

	ap_max_ch_width = lim_get_vht_ap_max_ch_width(vht_caps);
	session->ap_ch_width = (ap_max_ch_width != CH_WIDTH_INVALID) ?
				ap_max_ch_width : session->ch_width;
}

/**
 * lim_extract_vht_bw_params() - Extract VHT bandwidth-related session params
 * @mac_ctx: Pointer to Global MAC structure
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs
 * @chan_freq: Operating channel frequency in MHz
 *
 * This function extracts and updates the bandwidth-related session parameters
 * (ch_width, ap_ch_width, ch_center_freq_seg0, ch_center_freq_seg1) based on
 * the AP's VHT Operation IE and VHT Capabilities IE.
 *
 * Return: None
 */
static void lim_extract_vht_bw_params(struct mac_context *mac_ctx,
				       struct pe_session *session,
				       tDot11fBeaconIEs *bcn_ies,
				       uint32_t chan_freq)
{
	bool new_ch_width_dfn = false;
	tDot11fIEVHTOperation *vht_op;
	tDot11fIEVHTCaps *vht_caps;
	uint8_t fw_vht_ch_wd, vht_ch_wd, center_freq_diff;
	uint8_t channel, chan_center_freq_seg1, ap_bcon_ch_width;
	uint8_t sta_prefer_80mhz_over_160mhz;
	struct mlme_vht_capabilities_info *mlme_vht_cap;

	vht_op = &bcn_ies->VHTOperation;
	vht_caps = &bcn_ies->VHTCaps;

	/* Extract AP max bandwidth from VHT caps before VHT ops processing */
	lim_extract_ap_max_bw_from_vht_caps(session, vht_caps);

	sta_prefer_80mhz_over_160mhz =
		session->mac_ctx->mlme_cfg->sta.sta_prefer_80mhz_over_160mhz;
	mlme_vht_cap = &mac_ctx->mlme_cfg->vht_caps.vht_cap_info;

	/* If VHT is supported min 80 MHz support is must */
	ap_bcon_ch_width = vht_op->chanWidth;
	if (vht_caps->vht_extended_nss_bw_cap) {
		if (!vht_caps->extended_nss_bw_supp)
			chan_center_freq_seg1 =
				vht_op->chan_center_freq_seg1;
		else
			chan_center_freq_seg1 =
				bcn_ies->HTInfo.chan_center_freq_seg2;
	} else {
		chan_center_freq_seg1 = vht_op->chan_center_freq_seg1;
	}
	if (chan_center_freq_seg1 &&
	    (ap_bcon_ch_width == WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ)) {
		new_ch_width_dfn = true;
		if (chan_center_freq_seg1 >
				vht_op->chan_center_freq_seg0)
		    center_freq_diff = chan_center_freq_seg1 -
					vht_op->chan_center_freq_seg0;
		else
		    center_freq_diff = vht_op->chan_center_freq_seg0 -
					chan_center_freq_seg1;
		if (center_freq_diff == 8)
			ap_bcon_ch_width =
				WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ;
		else if (center_freq_diff > 16)
			ap_bcon_ch_width =
				WNI_CFG_VHT_CHANNEL_WIDTH_80_PLUS_80MHZ;
		else
			ap_bcon_ch_width =
				WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ;
	}

	fw_vht_ch_wd = wma_get_vht_ch_width();
	vht_ch_wd = QDF_MIN(fw_vht_ch_wd, ap_bcon_ch_width);

	if ((vht_ch_wd > WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ) &&
	    (ap_bcon_ch_width ==
	     WNI_CFG_VHT_CHANNEL_WIDTH_80_PLUS_80MHZ) &&
	    mlme_vht_cap->restricted_80p80_bw_supp) {
		if ((chan_center_freq_seg1 == 138 &&
		     vht_op->chan_center_freq_seg0 == 155) ||
		    (vht_op->chan_center_freq_seg0 == 138 &&
		     chan_center_freq_seg1 == 155))
			vht_ch_wd =
				WNI_CFG_VHT_CHANNEL_WIDTH_80_PLUS_80MHZ;
		else
			vht_ch_wd = WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ;
	}
	/*
	 * If the supported channel width is greater than 80MHz and
	 * AP supports Nss > 1 in 160MHz mode then connect the STA
	 * in 2x2 80MHz mode instead of connecting in 160MHz mode.
	 */
	if (vht_ch_wd > WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ) {
		if (sta_prefer_80mhz_over_160mhz == STA_PREFER_BW_80MHZ)
			vht_ch_wd = WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ;
		else if ((sta_prefer_80mhz_over_160mhz ==
					STA_PREFER_BW_VHT80MHZ) &&
		  (!(IS_VHT_NSS_1x1(bcn_ies->VHTCaps.txMCSMap)) &&
		    (!IS_VHT_NSS_1x1(bcn_ies->VHTCaps.rxMCSMap))))
			vht_ch_wd = WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ;
	}
	/*
	 * VHT OP IE old definition:
	 * vht_op->chan_center_freq_seg0: center freq of 80MHz/160MHz/
	 * primary 80 in 80+80MHz.
	 *
	 * vht_op->chan_center_freq_seg1: center freq of secondary 80
	 * in 80+80MHz.
	 *
	 * VHT OP IE NEW definition:
	 * vht_op->chan_center_freq_seg0: center freq of 80MHz/primary
	 * 80 in 80+80MHz/center freq of the 80 MHz channel segment
	 * that contains the primary channel in 160MHz mode.
	 *
	 * vht_op->chan_center_freq_seg1: center freq of secondary 80
	 * in 80+80MHz/center freq of 160MHz.
	 */
	session->ch_center_freq_seg0 = vht_op->chan_center_freq_seg0;
	session->ch_center_freq_seg1 = chan_center_freq_seg1;
	channel = wlan_reg_freq_to_chan(mac_ctx->pdev, chan_freq);
	if (vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ) {
		/* DUT or AP supports only 160MHz */
		if (ap_bcon_ch_width ==
				WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ) {
			/* AP is in 160MHz mode */
			if (!new_ch_width_dfn) {
				session->ch_center_freq_seg1 =
					vht_op->chan_center_freq_seg0;
				session->ch_center_freq_seg0 =
					lim_get_80Mhz_center_channel(channel);
			}
		} else {
			/* DUT supports only 160MHz and AP is
			 * in 80+80 mode
			 */
			vht_ch_wd = WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ;
			session->ch_center_freq_seg1 = 0;
			session->ch_center_freq_seg0 =
				lim_get_80Mhz_center_channel(channel);
		}
	} else if (vht_ch_wd == WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ) {
		/* DUT or AP supports only 80MHz */
		session->ch_center_freq_seg0 =
			lim_get_80Mhz_center_channel(channel);
		session->ch_center_freq_seg1 = 0;
	}
	session->ch_width = vht_ch_wd + 1;

	/*
	 * Sanity check: If CCFS1 is not present in VHT operation IE,
	 * ensure ch_width is capped to 80MHz
	 */
	if (!chan_center_freq_seg1 &&
	    session->ch_width > CH_WIDTH_80MHZ) {
		pe_debug("vdev %d AP " QDF_MAC_ADDR_FMT " CCFS1 not present, capping ch_width %d to 80MHz",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width);
		session->ch_width = CH_WIDTH_80MHZ;
		session->ch_center_freq_seg1 = 0;
	}

	/* Sanity check: Ensure ch_width does not exceed AP's max capability */
	if (session->ap_ch_width != CH_WIDTH_INVALID &&
	    session->ch_width > session->ap_ch_width) {
		pe_debug("vdev %d AP " QDF_MAC_ADDR_FMT " ch_width %d exceeds ap_ch_width %d, capping to ap_ch_width",
			 session->vdev_id, QDF_MAC_ADDR_REF(session->bssId),
			 session->ch_width, session->ap_ch_width);
		session->ch_width = session->ap_ch_width;
	}
}

/**
 * lim_configure_he_eht_params() - Configure HE and EHT parameters
 * @mac_ctx: Pointer to MAC context
 * @session: Pointer to PE session
 * @bcn_ies: Pointer to parsed beacon IEs from AP
 *
 * Processes HE/EHT capabilities and operations from AP beacon, validates
 * MCS maps, checks LDPC support, and updates session bandwidth capabilities.
 *
 * Return: None
 */
static void lim_configure_he_eht_params(struct mac_context *mac_ctx,
					struct pe_session *session,
					tDot11fBeaconIEs *bcn_ies)
{
	lim_check_is_he_mcs_valid(session, bcn_ies);
	lim_check_peer_ldpc_and_update(session, bcn_ies);
	/*
	 * Validate HE capability and set session->ap_ch_width before
	 * lim_extract_he_op() so that the AP BW cap in lim_extract_he_op()
	 * can use the correct ap_ch_width value.
	 */
	lim_process_he_capability_validation(session, bcn_ies);
	/*
	 * Validate EHT capability and update session->ap_ch_width before
	 * lim_extract_eht_op() so that the AP BW cap in lim_extract_eht_op()
	 * (Step 4) uses the EHT-validated ap_ch_width, not just the HE value.
	 */
	lim_update_eht_bw_cap_mcs(session, bcn_ies);
	lim_extract_he_op(mac_ctx, session, bcn_ies);
	lim_extract_eht_op(mac_ctx, session, bcn_ies);

	if (!mac_ctx->usr_eht_testbed_cfg)
		lim_update_he_bw_cap_mcs(session, bcn_ies);
}

QDF_STATUS lim_extract_ap_capability(struct mac_context *mac_ctx,
				     struct pe_session *session,
				     struct bss_description *bss_desc,
				     uint8_t *qos_cap, uint8_t *uapsd,
				     int8_t *local_constraint,
				     bool *is_pwr_constraint)
{
	tDot11fIEVHTOperation *vht_op;
	uint8_t *ie;
	uint16_t ie_len;
	tDot11fBeaconIEs *bcn_ies;

	*qos_cap = 0;
	*uapsd = 0;

	bcn_ies = &bss_desc->bcn_ies;
	ie = (uint8_t *)&bss_desc->ieFields[0];
	ie_len = wlan_get_ielen_from_bss_description(bss_desc);

	if (bcn_ies->WMMInfoAp.present || bcn_ies->WMMParams.present ||
	    bcn_ies->HTCaps.present)
		LIM_BSS_CAPS_SET(WME, *qos_cap);

	if (LIM_BSS_CAPS_GET(WME, *qos_cap) && bcn_ies->WMMCaps.present)
		LIM_BSS_CAPS_SET(WSM, *qos_cap);

	mac_ctx->lim.htCapabilityPresentInBeacon = bcn_ies->HTCaps.present;

	vht_op = &bcn_ies->VHTOperation;
	if (IS_BSS_VHT_CAPABLE(bcn_ies->VHTCaps) && vht_op->present &&
	    session->vhtCapability) {
		session->vhtCapabilityPresentInBeacon = 1;

		if (((bcn_ies->Vendor1IE.present &&
		      bcn_ies->vendor_vht_ie.present &&
		      bcn_ies->Vendor3IE.present)) &&
		      (((bcn_ies->VHTCaps.txMCSMap & VHT_MCS_3x3_MASK) ==
			VHT_MCS_3x3_MASK) &&
		      ((bcn_ies->VHTCaps.txMCSMap & VHT_MCS_2x2_MASK) !=
		       VHT_MCS_2x2_MASK)))
			session->vht_config.su_beam_formee = 0;
	} else {
		session->vhtCapabilityPresentInBeacon = 0;
	}

	/*
	 * Store STA's maximum capabilities before intersection with AP
	 * This allows STA to advertise its true maximum capability in
	 * Association Request, enabling AP to make informed bandwidth decisions
	 */
	lim_store_sta_max_capabilities(mac_ctx, session);

	if (bcn_ies->qcn_ie.present)
		session->qcn_ie_present_in_beacon = true;

	if (session->vhtCapabilityPresentInBeacon == 1 &&
	    !session->htSupportedChannelWidthSet) {
		if (!mac_ctx->mlme_cfg->vht_caps.vht_cap_info.enable_txbf_20mhz)
			session->vht_config.su_beam_formee = 0;

		if (session->opmode == QDF_P2P_CLIENT_MODE &&
		    !wlan_reg_is_24ghz_ch_freq(bss_desc->chan_freq) &&
		    mac_ctx->roam.configParam.channelBondingMode5GHz)
			lim_update_ch_width_for_p2p_client(mac_ctx, session,
							   bss_desc->chan_freq);
	} else if (session->vhtCapabilityPresentInBeacon && vht_op->chanWidth) {
		lim_extract_vht_bw_params(mac_ctx, session, bcn_ies,
					  bss_desc->chan_freq);
	}

	/* Configure Operating Mode Notification parameters */
	lim_configure_operating_mode_notification(session, bcn_ies);

	/* Configure HE and EHT parameters */
	lim_configure_he_eht_params(mac_ctx, session, bcn_ies);

	/* Extract the UAPSD flag from WMM Parameter element */
	if (bcn_ies->WMMParams.present)
		*uapsd = bcn_ies->WMMParams.qosInfo & LIM_QOS_AP_SUPPORTS_APSD;

	if (mac_ctx->mlme_cfg->sta.allow_tpc_from_ap) {
		if (bcn_ies->PowerConstraints.present) {
			*local_constraint =
				bcn_ies->PowerConstraints.localPowerConstraints;
			*is_pwr_constraint = true;
		} else {
			get_local_power_constraint_probe_response(bcn_ies,
								  local_constraint);
			*is_pwr_constraint = false;
		}
	}

	get_ese_version_ie_probe_response(mac_ctx, session, bcn_ies);

	session->country_info_present = false;
	/* Initializing before first use */
	if (bcn_ies->Country.present)
		session->country_info_present = true;
	/* Check if Extended caps are present in probe resp or not */
	if (bcn_ies->ExtCap.present)
		session->is_ext_caps_present = true;
	/* Update HS 2.0 Information Element */
	if (bcn_ies->hs20vendor_ie.present) {
		pe_debug("HS20 Indication Element Present, rel#: %u id: %u",
			 bcn_ies->hs20vendor_ie.release_num,
			 bcn_ies->hs20vendor_ie.hs_id_present);
		qdf_mem_copy(&session->hs20vendor_ie, &bcn_ies->hs20vendor_ie,
			     (sizeof(tDot11fIEhs20vendor_ie) -
			      sizeof(bcn_ies->hs20vendor_ie.hs_id)));
		if (bcn_ies->hs20vendor_ie.hs_id_present)
			qdf_mem_copy(&session->hs20vendor_ie.hs_id,
				     &bcn_ies->hs20vendor_ie.hs_id,
				     sizeof(bcn_ies->hs20vendor_ie.hs_id));
	}

	session->is_adaptive_11r_connection =
			lim_extract_adaptive_11r_cap(ie, ie_len);

	return QDF_STATUS_SUCCESS;
} /****** end lim_extract_ap_capability() ******/

/**
 * lim_get_htcb_state
 *
 ***FUNCTION:
 * This routing provides the translation of Airgo Enum to HT enum for determining
 * secondary channel offset.
 * Airgo Enum is required for backward compatibility purposes.
 *
 *
 ***NOTE:
 *
 * @param  mac - Pointer to Global MAC structure
 * @return The corresponding HT enumeration
 */
ePhyChanBondState lim_get_htcb_state(ePhyChanBondState aniCBMode)
{
	switch (aniCBMode) {
	case PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW:
	case PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_CENTERED:
	case PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH:
	case PHY_DOUBLE_CHANNEL_HIGH_PRIMARY:
		return PHY_DOUBLE_CHANNEL_HIGH_PRIMARY;
	case PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW:
	case PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_CENTERED:
	case PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH:
	case PHY_DOUBLE_CHANNEL_LOW_PRIMARY:
		return PHY_DOUBLE_CHANNEL_LOW_PRIMARY;
	case PHY_QUADRUPLE_CHANNEL_20MHZ_CENTERED_40MHZ_CENTERED:
		return PHY_SINGLE_CHANNEL_CENTERED;
	default:
		return PHY_SINGLE_CHANNEL_CENTERED;
	}
}
