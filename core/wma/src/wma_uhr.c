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

/**
 * DOC: wma_uhr.c
 *
 * WLAN Host Device Driver 802.11bn - Ultra High Reliability Implementation
 */

#include "wma_uhr.h"
#include "wmi_unified.h"
#include "service_ready_param.h"
#include "target_if.h"
#include "wma_internal.h"
#include "wlan_cmn_ieee80211.h"

#if defined(WLAN_FEATURE_11BN)
/**
 * wma_convert_uhr_cap() - convert UHR capabilities into wlan structure
 * @uhr_cap: pointer to wlan structure
 * @mac_cap: Received UHR MAC capability
 * @phy_cap: Received UHR PHY capability
 * @dbe_cap: Received UHR DBE capability parameters
 *
 * This function converts various UHR capability received as part of extended
 * service ready event into wlan structure.
 *
 * Return: None
 */
static void wma_convert_uhr_cap(struct wlan_uhr_cap_info *uhr_cap,
				uint32_t *mac_cap, uint32_t *phy_cap,
				uint32_t *dbe_cap)
{
	uhr_cap->present = true;

	/* UHR MAC capabilities */
	uhr_cap->dps_present = WMI_UHRCAP_MAC_DPS_GET(mac_cap);
	uhr_cap->dps_assist_support = WMI_UHRCAP_MAC_DPS_ASSIS_GET(mac_cap);
	uhr_cap->ap_static_hcm_support =
			WMI_UHRCAP_MAC_DPS_AP_HCM_GET(mac_cap);
	uhr_cap->npca_support = WMI_UHRCAP_MAC_NPCA_GET(mac_cap);
	uhr_cap->bsr_support = WMI_UHRCAP_MAC_BSR_GET(mac_cap);
	uhr_cap->addn_mapped_tid_support =
			WMI_UHRCAP_MAC_ADDITIONAL_TID_GET(mac_cap);
	uhr_cap->eotsp_support = WMI_UHRCAP_MAC_EOTSP_GET(mac_cap);
	uhr_cap->dso_support = WMI_UHRCAP_MAC_DSO_GET(mac_cap);
	uhr_cap->p_edca_support = WMI_UHRCAP_MAC_P_EDCA_GET(mac_cap);
	uhr_cap->dbe_support = WMI_UHRCAP_MAC_DBE_GET(mac_cap);
	uhr_cap->ul_lli_support = WMI_UHRCAP_MAC_UL_LLI_GET(mac_cap);
	uhr_cap->p2p_lli_support = WMI_UHRCAP_MAC_PEER_LLI_GET(mac_cap);
	uhr_cap->puo_support = WMI_UHRCAP_MAC_PUO_GET(mac_cap);
	uhr_cap->ap_puo_support = WMI_UHRCAP_MAC_AP_PUO_GET(mac_cap);
	uhr_cap->duo_support = WMI_UHRCAP_MAC_DUO_GET(mac_cap);
	uhr_cap->ul_mu_data_disable_rx_support =
			WMI_UHRCAP_MAC_OM_CTRL_UL_MU_DISABLE_RX_GET(mac_cap);
	uhr_cap->aom_support = WMI_UHRCAP_MAC_AOM_GET(mac_cap);
	uhr_cap->ifcs_support = WMI_UHRCAP_MAC_IFCS_LOC_GET(mac_cap);
	uhr_cap->uhr_trs_support = WMI_UHRCAP_MAC_UHR_TRS_GET(mac_cap);
	uhr_cap->txspg_support = WMI_UHRCAP_MAC_TXSPG_GET(mac_cap);
	uhr_cap->txop_return_support_intxspg =
			WMI_UHRCAP_MAC_TXOP_RETURN_GET(mac_cap);
	uhr_cap->uhr_op_mode_param_update_timeout =
			WMI_UHRCAP_MAC_UHR_OPMODE_TIMEOUT_GET(mac_cap);
	uhr_cap->param_update_adv_notify =
			WMI_UHRCAP_MAC_PARAM_UPDATE_ADV_GET(mac_cap);
	uhr_cap->update_ind_in_tim =
			WMI_UHRCAP_MAC_UPDATE_IND_TIM_GET(mac_cap);
	uhr_cap->bounded_ess = WMI_UHRCAP_MAC_BOUNDED_ESS_GET(mac_cap);
	uhr_cap->btm_assurance = WMI_UHRCAP_MAC_BTM_ASSURANCE_GET(mac_cap);
	uhr_cap->cobf_support = WMI_UHRCAP_MAC_COBF_SUPPORT_GET(mac_cap);

	/* UHR PHY capabilities */
	uhr_cap->max_nss_rx_ndp_sounding_80mhz =
		WMI_UHRCAP_PHY_MAX_NSS_RX_80_GET(phy_cap);
	uhr_cap->max_nss_rx_dl_mumimo_80mhz =
		WMI_UHRCAP_PHY_MAX_NSS_DL_MU_80_GET(phy_cap);
	uhr_cap->max_nss_rx_ndp_sounding_160mhz =
		WMI_UHRCAP_PHY_MAX_NSS_RX_160_GET(phy_cap);
	uhr_cap->max_nss_total_rx_dl_mumimo_160mhz =
		WMI_UHRCAP_PHY_MAX_NSS_DL_MU_160_GET(phy_cap);
	uhr_cap->max_nss_rx_ndp_sounding_320mhz =
		WMI_UHRCAP_PHY_MAX_NSS_RX_320_GET(phy_cap);
	uhr_cap->max_nss_total_rx_dl_mumimo_320mhz =
		WMI_UHRCAP_PHY_MAX_NSS_DL_MU_320_GET(phy_cap);
	uhr_cap->elr_rx_support = WMI_UHRCAP_PHY_ELR_RX_GET(phy_cap);
	uhr_cap->elr_tx_support = WMI_UHRCAP_PHY_ELR_TX_GET(phy_cap);
	uhr_cap->partial_bw_dl_mumimo_support =
		WMI_UHRCAP_PHY_PARTIAL_BW_DL_MU_MIMO_SUPPORT_GET(phy_cap);
	uhr_cap->partial_bw_ul_mumimo_support =
		WMI_UHRCAP_PHY_PARTIAL_BW_UL_MU_MIMO_SUPPORT_GET(phy_cap);
	uhr_cap->mcs15_support =
		WMI_UHRCAP_PHY_MCS_15_SUPPORT_GET(phy_cap);
	uhr_cap->two_x_ldpc_tx_support =
		WMI_UHRCAP_PHY_2XLDPC_TX_SUPPORT_GET(phy_cap);
	uhr_cap->two_x_ldpc_rx_support =
		WMI_UHRCAP_PHY_2XLDPC_RX_SUPPORT_GET(phy_cap);
	uhr_cap->ueqm_tx_support_max_nss_tx =
		WMI_UHRCAP_PHY_UEQM_TX_MAX_NSS_TX_SUPPORT_GET(phy_cap);
	uhr_cap->ueqm_rx_support_max_nss_rx =
		WMI_UHRCAP_PHY_UEQM_RX_MAX_NSS_RX_SUPPORT_GET(phy_cap);
	uhr_cap->cobf_joint_sounding_support =
		WMI_UHRCAP_PHY_COBF_JOINT_SOUNDING_SUPPORT_GET(phy_cap);
	uhr_cap->im_tx_support =
		WMI_UHRCAP_PHY_IM_PILOTS_TX_SUPPORT_GET(phy_cap);
	uhr_cap->im_rx_support =
		WMI_UHRCAP_PHY_IM_PILOTS_RX_SUPPORT_GET(phy_cap);
	uhr_cap->co_sr_mode1_support =
		WMI_UHRCAP_PHY_COSR_MODE_1_SUPPORT_GET(phy_cap);
	uhr_cap->co_sr_mode2_support =
		WMI_UHRCAP_PHY_COSR_MODE_2_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw20_pbw20_support =
		WMI_UHRCAP_PHY_DRU_DBW_20_PBW_20_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw40_pbw40_support =
		WMI_UHRCAP_PHY_DRU_DBW_40_PBW_40_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw80_pbw80_support =
		WMI_UHRCAP_PHY_DRU_DBW_80_PBW_80_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw80_pbw160_support =
		WMI_UHRCAP_PHY_DRU_DBW_80_PBW_160_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw80_pbw320_support =
		WMI_UHRCAP_PHY_DRU_DBW_80_PBW_320_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw20_pbw_ge80_support =
		WMI_UHRCAP_PHY_DRU_DBW_20_PBW_80_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw40_pbw_ge80_support =
		WMI_UHRCAP_PHY_DRU_DBW_40_PBW_80_SUPPORT_GET(phy_cap);
	uhr_cap->dru_dbw60_pbw_ge80_support =
		WMI_UHRCAP_PHY_DRU_DBW_60_PBW_80_SUPPORT_GET(phy_cap);
	uhr_cap->dru_rru_hybrid_support =
		WMI_UHRCAP_PHY_DRU_RRU_HYBRID_SUPPORT_GET(phy_cap);

	/* DBE Capability Parameters (conditional on dbe_support) */
	if (uhr_cap->dbe_support && dbe_cap) {
		uhr_cap->dbe_param[0] =
			(uint8_t)(WMI_UHRCAP_DBE_MAX_SUP_BW_GET(dbe_cap) |
			(WMI_UHRCAP_DBE_EHT_MCS_160_PRESENT_GET(dbe_cap) << 3) |
			(WMI_UHRCAP_DBE_EHT_MCS_320_PRESENT_GET(dbe_cap) << 4));
		if (WMI_UHRCAP_DBE_EHT_MCS_160_PRESENT_GET(dbe_cap)) {
			uint32_t map160 =
				WMI_UHRCAP_DBE_EHT_MCS_MAP_160_GET(dbe_cap);
			uhr_cap->dbe_param[1] = (uint8_t)(map160 & 0xFF);
			uhr_cap->dbe_param[2] = (uint8_t)((map160 >> 8) & 0xFF);
			uhr_cap->dbe_param[3] = (uint8_t)((map160 >> 16) & 0xFF);
		}
		if (WMI_UHRCAP_DBE_EHT_MCS_320_PRESENT_GET(dbe_cap)) {
			uint32_t map320 =
				WMI_UHRCAP_DBE_EHT_MCS_MAP_320_GET(dbe_cap);
			uhr_cap->dbe_param[4] = (uint8_t)(map320 & 0xFF);
			uhr_cap->dbe_param[5] = (uint8_t)((map320 >> 8) & 0xFF);
			uhr_cap->dbe_param[6] = (uint8_t)((map320 >> 16) & 0xFF);
		}
	}
}

static void wma_aggregate_uhr_cap(struct wlan_uhr_cap_info *aggr_uhr_cap,
				  struct wlan_uhr_cap_info *uhr_cap)
{
	if (!aggr_uhr_cap->present) {
		qdf_mem_copy(aggr_uhr_cap, uhr_cap,
			     sizeof(struct wlan_uhr_cap_info));
		return;
	}

	/* UHR MAC capabilities */
	aggr_uhr_cap->dps_present |= uhr_cap->dps_present;
	aggr_uhr_cap->dps_assist_support |= uhr_cap->dps_assist_support;
	aggr_uhr_cap->ap_static_hcm_support |= uhr_cap->ap_static_hcm_support;
	aggr_uhr_cap->npca_support |= uhr_cap->npca_support;
	aggr_uhr_cap->bsr_support |= uhr_cap->bsr_support;
	aggr_uhr_cap->addn_mapped_tid_support |=
					uhr_cap->addn_mapped_tid_support;
	aggr_uhr_cap->eotsp_support |= uhr_cap->eotsp_support;
	aggr_uhr_cap->dso_support |= uhr_cap->dso_support;
	aggr_uhr_cap->p_edca_support |= uhr_cap->p_edca_support;
	aggr_uhr_cap->dbe_support |= uhr_cap->dbe_support;
	aggr_uhr_cap->ul_lli_support |= uhr_cap->ul_lli_support;
	aggr_uhr_cap->p2p_lli_support |= uhr_cap->p2p_lli_support;
	aggr_uhr_cap->puo_support |= uhr_cap->puo_support;
	aggr_uhr_cap->ap_puo_support |= uhr_cap->ap_puo_support;
	aggr_uhr_cap->duo_support |= uhr_cap->duo_support;
	aggr_uhr_cap->ul_mu_data_disable_rx_support |=
					uhr_cap->ul_mu_data_disable_rx_support;
	aggr_uhr_cap->aom_support |= uhr_cap->aom_support;
	aggr_uhr_cap->ifcs_support |= uhr_cap->ifcs_support;
	aggr_uhr_cap->uhr_trs_support |= uhr_cap->uhr_trs_support;
	aggr_uhr_cap->txspg_support |= uhr_cap->txspg_support;
	aggr_uhr_cap->txop_return_support_intxspg |=
					uhr_cap->txop_return_support_intxspg;
	aggr_uhr_cap->uhr_op_mode_param_update_timeout |=
				uhr_cap->uhr_op_mode_param_update_timeout;
	aggr_uhr_cap->param_update_adv_notify |=
					uhr_cap->param_update_adv_notify;
	aggr_uhr_cap->update_ind_in_tim |= uhr_cap->update_ind_in_tim;
	aggr_uhr_cap->bounded_ess |= uhr_cap->bounded_ess;
	aggr_uhr_cap->btm_assurance |= uhr_cap->btm_assurance;
	aggr_uhr_cap->cobf_support |= uhr_cap->cobf_support;

	/* UHR PHY capabilities */
	aggr_uhr_cap->max_nss_rx_ndp_sounding_80mhz |=
				uhr_cap->max_nss_rx_ndp_sounding_80mhz;
	aggr_uhr_cap->max_nss_rx_dl_mumimo_80mhz |=
				uhr_cap->max_nss_rx_dl_mumimo_80mhz;
	aggr_uhr_cap->max_nss_rx_ndp_sounding_160mhz |=
				uhr_cap->max_nss_rx_ndp_sounding_160mhz;
	aggr_uhr_cap->max_nss_total_rx_dl_mumimo_160mhz |=
				uhr_cap->max_nss_total_rx_dl_mumimo_160mhz;
	aggr_uhr_cap->max_nss_rx_ndp_sounding_320mhz |=
				uhr_cap->max_nss_rx_ndp_sounding_320mhz;
	aggr_uhr_cap->max_nss_total_rx_dl_mumimo_320mhz |=
				uhr_cap->max_nss_total_rx_dl_mumimo_320mhz;
	aggr_uhr_cap->elr_rx_support |= uhr_cap->elr_rx_support;
	aggr_uhr_cap->elr_tx_support |= uhr_cap->elr_tx_support;
	aggr_uhr_cap->partial_bw_dl_mumimo_support |=
				uhr_cap->partial_bw_dl_mumimo_support;
	aggr_uhr_cap->partial_bw_ul_mumimo_support |=
				uhr_cap->partial_bw_ul_mumimo_support;
	aggr_uhr_cap->mcs15_support |= uhr_cap->mcs15_support;
	aggr_uhr_cap->two_x_ldpc_tx_support |= uhr_cap->two_x_ldpc_tx_support;
	aggr_uhr_cap->two_x_ldpc_rx_support |= uhr_cap->two_x_ldpc_rx_support;
	aggr_uhr_cap->ueqm_tx_support_max_nss_tx |=
				uhr_cap->ueqm_tx_support_max_nss_tx;
	aggr_uhr_cap->ueqm_rx_support_max_nss_rx |=
				uhr_cap->ueqm_rx_support_max_nss_rx;
	aggr_uhr_cap->cobf_joint_sounding_support |=
				uhr_cap->cobf_joint_sounding_support;
	aggr_uhr_cap->im_tx_support |= uhr_cap->im_tx_support;
	aggr_uhr_cap->im_rx_support |= uhr_cap->im_rx_support;
	aggr_uhr_cap->co_sr_mode1_support |= uhr_cap->co_sr_mode1_support;
	aggr_uhr_cap->co_sr_mode2_support |= uhr_cap->co_sr_mode2_support;
	aggr_uhr_cap->dru_dbw20_pbw20_support |= uhr_cap->dru_dbw20_pbw20_support;
	aggr_uhr_cap->dru_dbw40_pbw40_support |= uhr_cap->dru_dbw40_pbw40_support;
	aggr_uhr_cap->dru_dbw80_pbw80_support |= uhr_cap->dru_dbw80_pbw80_support;
	aggr_uhr_cap->dru_dbw80_pbw160_support |=
				uhr_cap->dru_dbw80_pbw160_support;
	aggr_uhr_cap->dru_dbw80_pbw320_support |=
				uhr_cap->dru_dbw80_pbw320_support;
	aggr_uhr_cap->dru_dbw20_pbw_ge80_support |=
				uhr_cap->dru_dbw20_pbw_ge80_support;
	aggr_uhr_cap->dru_dbw40_pbw_ge80_support |=
				uhr_cap->dru_dbw40_pbw_ge80_support;
	aggr_uhr_cap->dru_dbw60_pbw_ge80_support |=
				uhr_cap->dru_dbw60_pbw_ge80_support;
	aggr_uhr_cap->dru_rru_hybrid_support |= uhr_cap->dru_rru_hybrid_support;
}

static void wma_print_uhr_cap(struct wlan_uhr_cap_info *uhr_cap)
{
	if (!uhr_cap->present)
		return;

	wma_debug("UHR MAC Caps: DPS 0x%01x DPS Assist 0x%01x AP Static HCM 0x%01x NPCA 0x%01x Enhanced BSR 0x%01x Addn Mapped TID 0x%01x EOTSP 0x%01x",
		  uhr_cap->dps_present, uhr_cap->dps_assist_support,
		  uhr_cap->ap_static_hcm_support,
		  uhr_cap->npca_support, uhr_cap->bsr_support,
		  uhr_cap->addn_mapped_tid_support, uhr_cap->eotsp_support);
	wma_nofl_debug(" DSO 0x%01x P-EDCA 0x%01x DBE 0x%01x UL LLI 0x%01x P2P LLI 0x%01x PUO 0x%01x AP PUO 0x%01x DUO 0x%01x",
		       uhr_cap->dso_support, uhr_cap->p_edca_support,
		       uhr_cap->dbe_support, uhr_cap->ul_lli_support,
		       uhr_cap->p2p_lli_support, uhr_cap->puo_support,
		       uhr_cap->ap_puo_support, uhr_cap->duo_support);
	wma_nofl_debug(" OM Ctrl UL MU Data Disable RX 0x%01x AOM 0x%01x IFCS 0x%01x UHR TRS 0x%01x TXSPG 0x%01x TXOP Return in TXSPG 0x%01x",
		       uhr_cap->ul_mu_data_disable_rx_support,
		       uhr_cap->aom_support, uhr_cap->ifcs_support,
		       uhr_cap->uhr_trs_support, uhr_cap->txspg_support,
		       uhr_cap->txop_return_support_intxspg);
	wma_nofl_debug(" UHR Op Mode Param Update Timeout 0x%01x Param Update Adv Notify 0x%01x Update Ind in TIM 0x%01x Bounded ESS 0x%01x BTM Assurance 0x%01x",
		       uhr_cap->uhr_op_mode_param_update_timeout,
		       uhr_cap->param_update_adv_notify,
		       uhr_cap->update_ind_in_tim, uhr_cap->bounded_ess,
		       uhr_cap->btm_assurance);
	wma_nofl_debug(" Co-BF 0x%01x",
		       uhr_cap->cobf_support);
	wma_nofl_debug("UHR PHY Caps: Max NSS RX NDP Sounding: 80MHz 0x%01x 160MHz 0x%01x 320MHz 0x%01x",
		       uhr_cap->max_nss_rx_ndp_sounding_80mhz,
		       uhr_cap->max_nss_rx_ndp_sounding_160mhz,
		       uhr_cap->max_nss_rx_ndp_sounding_320mhz);
	wma_nofl_debug(" Max NSS Total RX DL MU-MIMO: 80MHz 0x%01x 160MHz 0x%01x 320MHz 0x%01x ELR: RX 0x%01x TX 0x%01x",
		       uhr_cap->max_nss_rx_dl_mumimo_80mhz,
		       uhr_cap->max_nss_total_rx_dl_mumimo_160mhz,
		       uhr_cap->max_nss_total_rx_dl_mumimo_320mhz,
		       uhr_cap->elr_rx_support, uhr_cap->elr_tx_support);
	wma_nofl_debug(" Partial BW DL MU-MIMO 0x%01x UL MU-MIMO 0x%01x MCS15 0x%01x 2xLDPC TX 0x%01x RX 0x%01x",
		       uhr_cap->partial_bw_dl_mumimo_support,
		       uhr_cap->partial_bw_ul_mumimo_support,
		       uhr_cap->mcs15_support,
		       uhr_cap->two_x_ldpc_tx_support,
		       uhr_cap->two_x_ldpc_rx_support);
	wma_nofl_debug(" UEQM TX 0x%01x RX 0x%01x CoBF Joint Sounding 0x%01x IM TX 0x%01x RX 0x%01x CoSR Mode1 0x%01x Mode2 0x%01x",
		       uhr_cap->ueqm_tx_support_max_nss_tx,
		       uhr_cap->ueqm_rx_support_max_nss_rx,
		       uhr_cap->cobf_joint_sounding_support,
		       uhr_cap->im_tx_support, uhr_cap->im_rx_support,
		       uhr_cap->co_sr_mode1_support,
		       uhr_cap->co_sr_mode2_support);
	wma_nofl_debug(" DRU DBW20/PBW20 0x%01x DBW40/PBW40 0x%01x DBW80/PBW80 0x%01x DBW80/PBW160 0x%01x DBW80/PBW320 0x%01x",
		       uhr_cap->dru_dbw20_pbw20_support,
		       uhr_cap->dru_dbw40_pbw40_support,
		       uhr_cap->dru_dbw80_pbw80_support,
		       uhr_cap->dru_dbw80_pbw160_support,
		       uhr_cap->dru_dbw80_pbw320_support);
	wma_nofl_debug(" DRU DBW20/PBW>=80 0x%01x DBW40/PBW>=80 0x%01x DBW60/PBW>=80 0x%01x DRU+RRU Hybrid 0x%01x",
		       uhr_cap->dru_dbw20_pbw_ge80_support,
		       uhr_cap->dru_dbw40_pbw_ge80_support,
		       uhr_cap->dru_dbw60_pbw_ge80_support,
		       uhr_cap->dru_rru_hybrid_support);
}

void wma_uhr_update_tgt_services(struct wmi_unified *wmi_handle,
				 struct wma_tgt_services *cfg)
{
	if (wmi_service_enabled(wmi_handle, wmi_service_11bn)) {
		cfg->en_11bn = true;
		wma_debug("11bn is enabled");
		wma_set_fw_wlan_feat_caps(DOT11BN);
	} else {
		cfg->en_11bn = false;
		wma_debug("11bn is not enabled");
	}
}

void wma_update_target_ext_uhr_cap(struct target_psoc_info *tgt_hdl,
				   struct wma_tgt_cfg *tgt_cfg)
{
	struct wlan_uhr_cap_info *uhr_cap = &tgt_cfg->uhr_cap;
	struct wlan_uhr_cap_info *uhr_cap_2g = &tgt_cfg->uhr_cap_2g;
	struct wlan_uhr_cap_info *uhr_cap_5g = &tgt_cfg->uhr_cap_5g;
	int i, num_hw_modes, total_mac_phy_cnt;
	struct wlan_uhr_cap_info uhr_cap_mac;
	struct wlan_psoc_host_mac_phy_caps_ext2 *mac_phy_cap, *mac_phy_caps2;
	struct wlan_psoc_host_mac_phy_caps *host_cap;
	uint32_t supported_bands;

	qdf_mem_zero(uhr_cap_2g, sizeof(struct wlan_uhr_cap_info));
	qdf_mem_zero(uhr_cap_5g, sizeof(struct wlan_uhr_cap_info));
	num_hw_modes = target_psoc_get_num_hw_modes(tgt_hdl);
	mac_phy_cap = target_psoc_get_mac_phy_cap_ext2(tgt_hdl);
	host_cap = target_psoc_get_mac_phy_cap(tgt_hdl);
	total_mac_phy_cnt = target_psoc_get_total_mac_phy_cnt(tgt_hdl);
	if (!mac_phy_cap || !host_cap) {
		wma_err("Invalid MAC PHY capabilities handle");
		uhr_cap->present = false;
		return;
	}

	if (!num_hw_modes) {
		wma_err("No extended UHR cap for current SOC");
		uhr_cap->present = false;
		return;
	}

	if (!tgt_cfg->services.en_11bn) {
		wma_info("Target does not support 11BN");
		uhr_cap->present = false;
		return;
	}

	for (i = 0; i < total_mac_phy_cnt; i++) {
		supported_bands = host_cap[i].supported_bands;
		mac_phy_caps2 = &mac_phy_cap[i];
		if (supported_bands & WLAN_2G_CAPABILITY) {
			qdf_mem_zero(&uhr_cap_mac,
				     sizeof(struct wlan_uhr_cap_info));
			wma_convert_uhr_cap(
					&uhr_cap_mac,
					mac_phy_caps2->uhr_cap_mac_info_2G,
					mac_phy_caps2->uhr_cap_phy_info_2G,
					mac_phy_caps2->uhr_cap_dbe_info_2G);
			wma_aggregate_uhr_cap(uhr_cap_2g, &uhr_cap_mac);
			wma_aggregate_uhr_cap(uhr_cap, &uhr_cap_mac);
			/* TODO: PPET */
		}

		if (supported_bands & WLAN_5G_CAPABILITY) {
			qdf_mem_zero(&uhr_cap_mac,
				     sizeof(struct wlan_uhr_cap_info));
			wma_convert_uhr_cap(
					&uhr_cap_mac,
					mac_phy_caps2->uhr_cap_mac_info_5G,
					mac_phy_caps2->uhr_cap_phy_info_5G,
					mac_phy_caps2->uhr_cap_dbe_info_5G);
			wma_aggregate_uhr_cap(uhr_cap_5g, &uhr_cap_mac);
			wma_aggregate_uhr_cap(uhr_cap, &uhr_cap_mac);
			/* TODO: PPET */
		}
	}

	wma_debug("Aggregated 2g/5g caps");
	wma_print_uhr_cap(uhr_cap);
	wma_debug("Aggregated 2g caps");
	wma_print_uhr_cap(uhr_cap_2g);
	wma_debug("Aggregated 5g caps");
	wma_print_uhr_cap(uhr_cap_5g);
}

void wma_populate_peer_uhr_cap(struct peer_assoc_params *peer,
			       tpAddStaParams params)
{
	struct wlan_uhr_cap_info *uhr_cap = &params->uhr_config;
	uint32_t *phy_cap = peer->peer_uhr_cap_phyinfo;
	uint32_t *mac_cap = peer->peer_uhr_cap_macinfo;
	uint32_t *dbe_cap = peer->peer_uhr_cap_dbeinfo;
	uint32_t uhrop_param;

	if (!params->uhr_capable)
		return;

	peer->uhr_flag = 1;
	peer->qos_flag = 1;

	uhrop_param = ((uint32_t *)(&params->uhr_op_ie))[1];
	peer->peer_uhr_ops = uhrop_param;

	/* UHR MAC Capabilities */
	WMI_UHRCAP_MAC_DPS_SET(mac_cap, uhr_cap->dps_present);
	WMI_UHRCAP_MAC_DPS_ASSIS_SET(mac_cap, uhr_cap->dps_assist_support);
	WMI_UHRCAP_MAC_DPS_AP_HCM_SET(
			mac_cap, uhr_cap->ap_static_hcm_support);
	WMI_UHRCAP_MAC_NPCA_SET(
			mac_cap, uhr_cap->npca_support);
	WMI_UHRCAP_MAC_BSR_SET(mac_cap, uhr_cap->bsr_support);
	WMI_UHRCAP_MAC_ADDITIONAL_TID_SET(
			mac_cap, uhr_cap->addn_mapped_tid_support);
	WMI_UHRCAP_MAC_EOTSP_SET(
			mac_cap, uhr_cap->eotsp_support);
	WMI_UHRCAP_MAC_DSO_SET(mac_cap, uhr_cap->dso_support);
	WMI_UHRCAP_MAC_P_EDCA_SET(mac_cap, uhr_cap->p_edca_support);
	WMI_UHRCAP_MAC_DBE_SET(mac_cap, uhr_cap->dbe_support);
	WMI_UHRCAP_MAC_UL_LLI_SET(mac_cap, uhr_cap->ul_lli_support);
	WMI_UHRCAP_MAC_PEER_LLI_SET(mac_cap, uhr_cap->p2p_lli_support);
	WMI_UHRCAP_MAC_PUO_SET(mac_cap, uhr_cap->puo_support);
	WMI_UHRCAP_MAC_AP_PUO_SET(mac_cap, uhr_cap->ap_puo_support);
	WMI_UHRCAP_MAC_DUO_SET(mac_cap, uhr_cap->duo_support);
	WMI_UHRCAP_MAC_OM_CTRL_UL_MU_DISABLE_RX_SET(
			mac_cap, uhr_cap->ul_mu_data_disable_rx_support);
	WMI_UHRCAP_MAC_AOM_SET(mac_cap, uhr_cap->aom_support);
	WMI_UHRCAP_MAC_IFCS_LOC_SET(mac_cap, uhr_cap->ifcs_support);
	WMI_UHRCAP_MAC_UHR_TRS_SET(mac_cap, uhr_cap->uhr_trs_support);
	WMI_UHRCAP_MAC_TXSPG_SET(mac_cap, uhr_cap->txspg_support);
	WMI_UHRCAP_MAC_TXOP_RETURN_SET(
			mac_cap, uhr_cap->txop_return_support_intxspg);
	WMI_UHRCAP_MAC_UHR_OPMODE_TIMEOUT_SET(
			mac_cap, uhr_cap->uhr_op_mode_param_update_timeout);
	WMI_UHRCAP_MAC_PARAM_UPDATE_ADV_SET(
			mac_cap, uhr_cap->param_update_adv_notify);
	WMI_UHRCAP_MAC_UPDATE_IND_TIM_SET(
			mac_cap, uhr_cap->update_ind_in_tim);
	WMI_UHRCAP_MAC_BOUNDED_ESS_SET(mac_cap, uhr_cap->bounded_ess);
	WMI_UHRCAP_MAC_BTM_ASSURANCE_SET(mac_cap, uhr_cap->btm_assurance);
	WMI_UHRCAP_MAC_COBF_SUPPORT_SET(mac_cap, uhr_cap->cobf_support);

	/* UHR PHY Capabilities */
	WMI_UHRCAP_PHY_MAX_NSS_RX_80_SET(
			phy_cap, uhr_cap->max_nss_rx_ndp_sounding_80mhz);
	WMI_UHRCAP_PHY_MAX_NSS_DL_MU_80_SET(
			phy_cap, uhr_cap->max_nss_rx_dl_mumimo_80mhz);
	WMI_UHRCAP_PHY_MAX_NSS_RX_160_SET(
			phy_cap, uhr_cap->max_nss_rx_ndp_sounding_160mhz);
	WMI_UHRCAP_PHY_MAX_NSS_DL_MU_160_SET(
			phy_cap, uhr_cap->max_nss_total_rx_dl_mumimo_160mhz);
	WMI_UHRCAP_PHY_MAX_NSS_RX_320_SET(
			phy_cap, uhr_cap->max_nss_rx_ndp_sounding_320mhz);
	WMI_UHRCAP_PHY_MAX_NSS_DL_MU_320_SET(
			phy_cap, uhr_cap->max_nss_total_rx_dl_mumimo_320mhz);
	WMI_UHRCAP_PHY_ELR_RX_SET(phy_cap, uhr_cap->elr_rx_support);
	WMI_UHRCAP_PHY_ELR_TX_SET(phy_cap, uhr_cap->elr_tx_support);
	WMI_UHRCAP_PHY_PARTIAL_BW_DL_MU_MIMO_SUPPORT_SET(
			phy_cap, uhr_cap->partial_bw_dl_mumimo_support);
	WMI_UHRCAP_PHY_PARTIAL_BW_UL_MU_MIMO_SUPPORT_SET(
			phy_cap, uhr_cap->partial_bw_ul_mumimo_support);
	WMI_UHRCAP_PHY_MCS_15_SUPPORT_SET(
			phy_cap, uhr_cap->mcs15_support);
	WMI_UHRCAP_PHY_2XLDPC_TX_SUPPORT_SET(
			phy_cap, uhr_cap->two_x_ldpc_tx_support);
	WMI_UHRCAP_PHY_2XLDPC_RX_SUPPORT_SET(
			phy_cap, uhr_cap->two_x_ldpc_rx_support);
	WMI_UHRCAP_PHY_UEQM_TX_MAX_NSS_TX_SUPPORT_SET(
			phy_cap, uhr_cap->ueqm_tx_support_max_nss_tx);
	WMI_UHRCAP_PHY_UEQM_RX_MAX_NSS_RX_SUPPORT_SET(
			phy_cap, uhr_cap->ueqm_rx_support_max_nss_rx);
	WMI_UHRCAP_PHY_COBF_JOINT_SOUNDING_SUPPORT_SET(
			phy_cap, uhr_cap->cobf_joint_sounding_support);
	WMI_UHRCAP_PHY_IM_PILOTS_TX_SUPPORT_SET(
			phy_cap, uhr_cap->im_tx_support);
	WMI_UHRCAP_PHY_IM_PILOTS_RX_SUPPORT_SET(
			phy_cap, uhr_cap->im_rx_support);
	WMI_UHRCAP_PHY_COSR_MODE_1_SUPPORT_SET(
			phy_cap, uhr_cap->co_sr_mode1_support);
	WMI_UHRCAP_PHY_COSR_MODE_2_SUPPORT_SET(
			phy_cap, uhr_cap->co_sr_mode2_support);
	WMI_UHRCAP_PHY_DRU_DBW_20_PBW_20_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw20_pbw20_support);
	WMI_UHRCAP_PHY_DRU_DBW_40_PBW_40_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw40_pbw40_support);
	WMI_UHRCAP_PHY_DRU_DBW_80_PBW_80_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw80_pbw80_support);
	WMI_UHRCAP_PHY_DRU_DBW_80_PBW_160_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw80_pbw160_support);
	WMI_UHRCAP_PHY_DRU_DBW_80_PBW_320_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw80_pbw320_support);
	WMI_UHRCAP_PHY_DRU_DBW_20_PBW_80_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw20_pbw_ge80_support);
	WMI_UHRCAP_PHY_DRU_DBW_40_PBW_80_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw40_pbw_ge80_support);
	WMI_UHRCAP_PHY_DRU_DBW_60_PBW_80_SUPPORT_SET(
			phy_cap, uhr_cap->dru_dbw60_pbw_ge80_support);
	WMI_UHRCAP_PHY_DRU_RRU_HYBRID_SUPPORT_SET(
			phy_cap, uhr_cap->dru_rru_hybrid_support);

	/* DBE Capability Parameters */
	qdf_mem_zero(dbe_cap,
		     WMI_HOST_MAX_UHRCAP_DBE_SIZE * sizeof(*dbe_cap));
	if (uhr_cap->dbe_support && uhr_cap->dbe_param[0]) {
		uint8_t mcs160_present = (uhr_cap->dbe_param[0] >> 3) & 0x1;
		uint8_t mcs320_present = (uhr_cap->dbe_param[0] >> 4) & 0x1;
		uint32_t map160 = 0, map320 = 0;

		WMI_UHRCAP_DBE_MAX_SUP_BW_SET(dbe_cap,
					      uhr_cap->dbe_param[0] & 0x7);
		WMI_UHRCAP_DBE_EHT_MCS_160_PRESENT_SET(dbe_cap,
						       mcs160_present);
		WMI_UHRCAP_DBE_EHT_MCS_320_PRESENT_SET(dbe_cap,
						       mcs320_present);
		if (mcs160_present) {
			map160 = uhr_cap->dbe_param[1] |
				 ((uint32_t)uhr_cap->dbe_param[2] << 8) |
				 ((uint32_t)uhr_cap->dbe_param[3] << 16);
			WMI_UHRCAP_DBE_EHT_MCS_MAP_160_SET(dbe_cap, map160);
		}
		if (mcs320_present) {
			map320 = uhr_cap->dbe_param[4] |
				 ((uint32_t)uhr_cap->dbe_param[5] << 8) |
				 ((uint32_t)uhr_cap->dbe_param[6] << 16);
			WMI_UHRCAP_DBE_EHT_MCS_MAP_320_SET(dbe_cap, map320);
		}
	}

	wma_print_uhr_cap(uhr_cap);
	wma_debug("Peer UHR Capabilities:");
}

bool wma_get_bss_uhr_capable(struct bss_params *add_bss)
{
	return add_bss->uhr_capable;
}
#endif /* WLAN_FEATURE_11BN */
