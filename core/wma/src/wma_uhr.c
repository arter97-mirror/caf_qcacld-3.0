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
	uhr_cap->co_sr_support = WMI_UHRCAP_MAC_COSR_SUPPORT_GET(mac_cap);
	uhr_cap->mapc_enh_meas_support =
			WMI_UHRCAP_MAC_MAPC_ENH_MEAS_GET(mac_cap);

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
	aggr_uhr_cap->co_sr_support |= uhr_cap->co_sr_support;
	aggr_uhr_cap->mapc_enh_meas_support |= uhr_cap->mapc_enh_meas_support;

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
	wma_nofl_debug(" Co-BF 0x%01x Co-SR 0x%01x MAPC Enh Meas 0x%01x",
		       uhr_cap->cobf_support, uhr_cap->co_sr_support,
		       uhr_cap->mapc_enh_meas_support);
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
	struct wlan_uhr_op_ie *uhr_op = &params->uhr_op_ie;

	if (!params->uhr_capable)
		return;

	peer->uhr_flag = 1;
	peer->qos_flag = 1;

	uhrop_param = 0;
	if (uhr_op->present) {
		WMI_UHR_OPS_INFORMATION_PRESENT_SET(uhrop_param, 1);
		WMI_UHR_OPS_DPS_ENABLED_SET(uhrop_param, uhr_op->dps_enabled);
		WMI_UHR_OPS_NPCA_ENABLED_SET(uhrop_param, uhr_op->npca_enabled);
		WMI_UHR_OPS_DBE_ENABLED_SET(uhrop_param, uhr_op->dbe_enabled);
		WMI_UHR_OPS_PEDCA_ENABLED_SET(uhrop_param,
					      uhr_op->p_edca_enabled);
		WMI_UHR_OPS_DBE_BANDWIDTH_SET(uhrop_param,
					      uhr_op->dbe_bandwidth);
	}
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
	WMI_UHRCAP_MAC_COSR_SUPPORT_SET(mac_cap, uhr_cap->co_sr_support);
	WMI_UHRCAP_MAC_MAPC_ENH_MEAS_SET(mac_cap,
					 uhr_cap->mapc_enh_meas_support);

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

	if (params->npca_cap.npca_supp) {
		struct wlan_npca_caps *nc = &params->npca_cap;
		struct wmi_host_npca_param *np = &peer->npca_param;

		np->npca_enabled = nc->npca_supp;
		np->npca_pri_channel = nc->npca_pri_channel;
		np->npca_min_dur_threshold = nc->npca_min_dur_threshold;
		np->npca_switch_delay = nc->npca_switch_delay;
		np->npca_switch_back_delay = nc->npca_switch_back_delay;
		np->npca_qsrc = nc->npca_qsrc;
		np->npca_moplen = nc->npca_moplen;
		np->npca_disabled_subchan_bm_present =
					nc->npca_disabled_subchan_bm_present;
		np->npca_disabled_subchan_bm = nc->npca_disabled_subchan_bm;
	}

	peer->two_x_ldpc_flag = uhr_cap->two_x_ldpc_rx_support;

	wma_print_uhr_cap(uhr_cap);
	wma_debug("Peer UHR Capabilities:");
}

bool wma_get_bss_uhr_capable(struct bss_params *add_bss)
{
	return add_bss->uhr_capable;
}

/* MCS Based UHR rate table — identical values to EHT (same MCS/NSS/BW set) */
/* MCS parameters with Nss = 1 */
static const struct index_uhr_data_rate_type uhr_mcs_nss1[] = {
/* MCS,  {dcm0:0.8/1.6/3.2}, {dcm1:0.8/1.6/3.2} */
	{0,  {{86,   81,   73}, {0} }, /* UHR20 */
	     {{172,  163,  146}, {0} }, /* UHR40 */
	     {{360,  340,  306}, {0} }, /* UHR80 */
	     {{721,  681,  613}, {0} }, /* UHR160 */
	     {{1441,  1361,  1225}, {0} } }, /* UHR320 */
	{1,  {{172,  163,  146 }, {0} },
	     {{344,  325,  293 }, {0} },
	     {{721,  681,  613 }, {0} },
	     {{1441, 1361, 1225}, {0} },
	     {{2882, 2722, 2450}, {0} } },
	{2,  {{258,  244,  219 }, {0} },
	     {{516,  488,  439 }, {0} },
	     {{1081, 1021, 919 }, {0} },
	     {{2162, 2042, 1838}, {0} },
	     {{4324, 4083, 3675}, {0} } },
	{3,  {{344,  325,  293 }, {0} },
	     {{688,  650,  585 }, {0} },
	     {{1441, 1361, 1225}, {0} },
	     {{2882, 2722, 2450}, {0} },
	     {{5765, 5444, 4900}, {0} } },
	{4,  {{516,  488,  439 }, {0} },
	     {{1032, 975,  878 }, {0} },
	     {{2162, 2042, 1838}, {0} },
	     {{4324, 4083, 3675}, {0} },
	     {{8647, 8167, 7350}, {0} } },
	{5,  {{688,  650,  585 }, {0} },
	     {{1376, 1300, 1170}, {0} },
	     {{2882, 2722, 2450}, {0} },
	     {{5765, 5444, 4900}, {0} },
	     {{11529, 10889, 9800}, {0} } },
	{6,  {{774,  731,  658 }, {0} },
	     {{1549, 1463, 1316}, {0} },
	     {{3243, 3063, 2756}, {0} },
	     {{6485, 6125, 5513}, {0} },
	     {{12971, 12250, 11025}, {0} } },
	{7,  {{860,  813,  731 }, {0} },
	     {{1721, 1625, 1463}, {0} },
	     {{3603, 3403, 3063}, {0} },
	     {{7206, 6806, 6125}, {0} },
	     {{14412, 13611, 12250}, {0} } },
	{8,  {{1032, 975,  878 }, {0} },
	     {{2065, 1950, 1755}, {0} },
	     {{4324, 4083, 3675}, {0} },
	     {{8647, 8167, 7350}, {0} },
	     {{17294, 16333, 14700}, {0} } },
	{9,  {{1147, 1083, 975 }, {0} },
	     {{2294, 2167, 1950}, {0} },
	     {{4804, 4537, 4083}, {0} },
	     {{9608, 9074, 8167}, {0} },
	     {{19216, 18148, 16333}, {0} } },
	{10, {{1290, 1219, 1097}, {0} },
	     {{2581, 2438, 2194}, {0} },
	     {{5404, 5104, 4594}, {0} },
	     {{10809, 10208, 9188}, {0} },
	     {{21618, 20417, 18375}, {0} } },
	{11, {{1434, 1354, 1219}, {0} },
	     {{2868, 2708, 2438}, {0} },
	     {{6005, 5671, 5104}, {0} },
	     {{12010, 11342, 10208}, {0} },
	     {{24020, 22685, 20417}, {0} } },
	{12, {{1549, 1463, 1316}, {0} },
	     {{3097, 2925, 2633}, {0} },
	     {{6485, 6125, 5513}, {0} },
	     {{12971, 12250, 11025}, {0} },
	     {{25941, 24500, 22050}, {0} } },
	{13, {{1721, 1625, 1463}, {0} },
	     {{3441, 3250, 2925}, {0} },
	     {{7206, 6806, 6125}, {0} },
	     {{14412, 13611, 12250}, {0} },
	     {{28824, 27222, 24500}, {0} } },
};

/* MCS parameters with Nss = 2 */
static const struct index_uhr_data_rate_type uhr_mcs_nss2[] = {
/* MCS,  {dcm0:0.8/1.6/3.2}, {dcm1:0.8/1.6/3.2} */
	{0,  {{172,   162,   146 }, {0} }, /* UHR20 */
	     {{344,   326,   292 }, {0} }, /* UHR40 */
	     {{720,   680,   612 }, {0} }, /* UHR80 */
	     {{1442, 1362, 1226},   {0} }, /* UHR160 */
	     {{2882, 2722, 2450},   {0} } }, /* UHR320 */
	{1,  {{344,   326,   292 }, {0} },
	     {{688,   650,   586 }, {0} },
	     {{1442,  1362,  1226}, {0} },
	     {{2882, 2722, 2450},   {0} },
	     {{5764, 5444, 4900},   {0} } },
	{2,  {{516,   488,   438 }, {0} },
	     {{1032,  976,   878 }, {0} },
	     {{2162,  2042,  1838}, {0} },
	     {{4324, 4084, 3676}, {0} },
	     {{8648, 8166, 7350}, {0} } },
	{3,  {{688,   650,   586 }, {0} },
	     {{1376,  1300,  1170}, {0} },
	     {{2882,  2722,  2450}, {0} },
	     {{5764, 5444, 4900}, {0} },
	     {{11530, 10888, 9800}, {0} } },
	{4,  {{1032,  976,   878 }, {0} },
	     {{2064,  1950,  1756}, {0} },
	     {{4324,  4083,  36756}, {0} },
	     {{8648, 8166, 7350}, {0} },
	     {{17294, 16334, 14700}, {0} } },
	{5,  {{1376,  1300,  1170}, {0} },
	     {{2752,  2600,  2340}, {0} },
	     {{5764,  5444,  4900}, {0} },
	     {{11530, 10888, 9800}, {0} },
	     {{23058, 21778, 19600}, {0} } },
	{6,  {{1548,  1462,  1316}, {0} },
	     {{3098,  2926,  2632}, {0} },
	     {{6486,  6126,  5512}, {0} },
	     {{12970, 12250, 11026}, {0} },
	     {{25942, 24500, 22050}, {0} } },
	{7,  {{1720,  1626,  1462}, {0} },
	     {{3442,  3250,  2926}, {0} },
	     {{7206,  6806,  61256}, {0} },
	     {{14412, 13612, 12250}, {0} },
	     {{28824, 27222, 24500}, {0} } },
	{8,  {{2064,  1950,  1756}, {0} },
	     {{4130,  3900,  3510}, {0} },
	     {{8648,  8166,  7350}, {0} },
	     {{17294, 16334, 14700}, {0} },
	     {{34588, 32666, 29400}, {0} } },
	{9,  {{2294,  2166,  1950}, {0} },
	     {{4588,  4334,  3900}, {0} },
	     {{9608,  9074,  8166}, {0} },
	     {{19216, 18148, 16334}, {0} },
	     {{38432, 36296, 32666}, {0} } },
	{10, {{2580,  2438,  2194}, {0} },
	     {{5162,  4876,  4388}, {0} },
	     {{10808, 10208, 9188}, {0} },
	     {{21618, 20416, 18376}, {0} },
	     {{43236, 40834, 36750}, {0} } },
	{11, {{2868,  2708,  2438}, {0} },
	     {{5736,  5416,  4876}, {0} },
	     {{12010, 11342, 10208}, {0} },
	     {{24020, 22686, 20416}, {0} },
	     {{48040, 45370, 40834}, {0} } },
	{12, {{3098,  2926,  2632}, {0} },
	     {{6194,  5850,  5266}, {0} },
	     {{12970, 12250, 11026}, {0} },
	     {{25942, 24500, 22050}, {0} },
	     {{51882, 49000, 44100}, {0} } },
	{13, {{3442,  3250,  2926}, {0} },
	     {{6882,  6500,  5850}, {0} },
	     {{14412, 13611, 12250}, {0} },
	     {{28824, 27222, 24500}, {0} },
	     {{57648, 54444, 49000}, {0} } }
};

/* MCS parameters with Nss = 3 */
static const struct index_uhr_data_rate_type uhr_mcs_nss3[] = {
/* MCS,  {dcm0:0.8/1.6/3.2}, {dcm1:0.8/1.6/3.2} */
	{0,  {{258,   243,   219}, {0} }, /* UHR20 */
	     {{516,   489,   438}, {0} }, /* UHR40 */
	     {{1080,  1020,  918}, {0} }, /* UHR80 */
	     {{2163,  2043,  1839}, {0} }, /* UHR160 */
	     {{4323,  4083,  3675}, {0} } }, /* UHR320 */
	{1,  {{516,   489,   438}, {0} },
	     {{1032,  975,   879}, {0} },
	     {{2163,  2043,  1839}, {0} },
	     {{4323,  4083,  3675}, {0} },
	     {{8646,  8166,  7350}, {0} } },
	{2,  {{774,   732,   657}, {0} },
	     {{1548,  1464,  1317}, {0} },
	     {{3243,  3063,  2757}, {0} },
	     {{6486,  6126,  5514}, {0} },
	     {{12972, 12249, 11025}, {0} } },
	{3,  {{1032,  975,   879}, {0} },
	     {{2064,  1950,  1755}, {0} },
	     {{4323,  4083,  3675}, {0} },
	     {{8646,  8166,  7350}, {0} },
	     {{17295, 16332, 14700}, {0} } },
	{4,  {{1548,  1464,  1317}, {0} },
	     {{3096,  2925,  2634}, {0} },
	     {{6486,  6126,  5514}, {0} },
	     {{12972, 12249, 11025}, {0} },
	     {{25941, 24501, 22050}, {0} } },
	{5,  {{2064, 1950, 1755}, {0} },
	     {{4128, 3900, 3510}, {0} },
	     {{8646, 8166, 7350}, {0} },
	     {{17295, 16332, 14700}, {0} },
	     {{34587, 32667, 29400}, {0} } },
	{6,  {{2322, 2193, 1974}, {0} },
	     {{4647, 4389, 3948}, {0} },
	     {{9729, 9189, 8268}, {0} },
	     {{19455, 18375, 16539}, {0} },
	     {{38913, 36750, 33075}, {0} } },
	{7,  {{2580, 2439, 2193}, {0} },
	     {{5163, 4875, 4389}, {0} },
	     {{10809, 10209, 9189}, {0} },
	     {{21618, 20418, 18375}, {0} },
	     {{43236, 40833, 36750}, {0} } },
	{8,  {{3096, 2925, 2634}, {0} },
	     {{6195, 5850, 5265}, {0} },
	     {{12972, 12249, 11025}, {0} },
	     {{25941, 24501, 22050}, {0} },
	     {{51882, 48999, 44100}, {0} } },
	{9,  {{3441, 3249, 2925}, {0} },
	     {{6882, 6501, 5850}, {0} },
	     {{14412, 13611, 12249}, {0} },
	     {{28824, 27222, 24501}, {0} },
	     {{57648, 54444, 48999}, {0} } },
	{10, {{3870, 3657, 3291}, {0} },
	     {{7743, 7314, 6582}, {0} },
	     {{16212, 15312, 13782}, {0} },
	     {{32427, 30624, 27564}, {0} },
	     {{64854, 61251, 55125}, {0} } },
	{11, {{4302, 4062, 3657}, {0} },
	     {{8604, 8124, 7314}, {0} },
	     {{18015, 17013, 15312}, {0} },
	     {{36030, 34029, 30624}, {0} },
	     {{72060, 68055, 61251}, {0} } },
	{12, {{4647, 4389, 3948}, {0} },
	     {{9291, 8775, 7899}, {0} },
	     {{19455, 18375, 16539}, {0} },
	     {{38913, 36750, 33075}, {0} },
	     {{77823, 73500, 66150}, {0} } },
	{13, {{5163, 4875, 4389}, {0} },
	     {{10323, 9750, 8775}, {0} },
	     {{21618, 20418, 18375}, {0} },
	     {{43236, 40833, 36750}, {0} },
	     {{86472, 81666, 73500}, {0} } },
};

/* MCS parameters with Nss = 4 */
static const struct index_uhr_data_rate_type uhr_mcs_nss4[] = {
/* MCS,  {dcm0:0.8/1.6/3.2}, {dcm1:0.8/1.6/3.2} */
	{0,  {{344,   324,   292}, {0} }, /* UHR20 */
	     {{688,   652,   584}, {0} }, /* UHR40 */
	     {{1440,  1360,  1224}, {0} }, /* UHR80 */
	     {{2884,  2724,  2452}, {0} }, /* UHR160 */
	     {{5764,  5444,  4900}, {0} } }, /* UHR320 */
	{1,  {{688,   652,   584}, {0} },
	     {{1376,  1300,  1172}, {0} },
	     {{2884,  2724,  2452}, {0} },
	     {{5764,  5444,  4900}, {0} },
	     {{11528, 10888, 9800}, {0} } },
	{2,  {{1032,  976,   876}, {0} },
	     {{2064,  1952,  1756}, {0} },
	     {{4324,  4084,  3676}, {0} },
	     {{8648,  8168,  7352}, {0} },
	     {{17296, 16332, 14700}, {0} } },
	{3,  {{1376,  1300,  1172}, {0} },
	     {{2752,  2600,  2340}, {0} },
	     {{5764,  5444,  4900}, {0} },
	     {{11528, 10888, 9800}, {0} },
	     {{23060, 21776, 19600}, {0} } },
	{4,  {{2064,  1952,  1756}, {0} },
	     {{4128,  3900,  3512}, {0} },
	     {{8648,  8168,  7352}, {0} },
	     {{17296, 16332, 14700}, {0} },
	     {{34588, 32668, 29400}, {0} } },
	{5,  {{2752, 2600, 2340}, {0} },
	     {{5504, 5200, 4680}, {0} },
	     {{11528, 10888, 9800}, {0} },
	     {{23060, 21776, 19600}, {0} },
	     {{46116, 43556, 39200}, {0} } },
	{6,  {{3096, 2924, 2632}, {0} },
	     {{6196, 5852, 5264}, {0} },
	     {{12972, 12252, 11024}, {0} },
	     {{25940, 24500, 22052}, {0} },
	     {{51884, 49000, 44100}, {0} } },
	{7,  {{3440, 3250, 2924}, {0} },
	     {{6884, 6500, 5852}, {0} },
	     {{14412, 13612, 12252}, {0} },
	     {{28824, 27224, 24500}, {0} },
	     {{57648, 54444, 49000}, {0} } },
	{8,  {{4128, 3900, 3512}, {0} },
	     {{8260, 7800, 7020}, {0} },
	     {{17296, 16332, 14700}, {0} },
	     {{34588, 32668, 29400}, {0} },
	     {{69176, 65332, 58800}, {0} } },
	{9,  {{4588, 4332, 3900}, {0} },
	     {{9176, 8668, 7800}, {0} },
	     {{19216, 18148, 16332}, {0} },
	     {{38432, 36296, 32668}, {0} },
	     {{76864, 72592, 65332}, {0} } },
	{10, {{5160, 4876, 4388}, {0} },
	     {{10324, 9752, 8776}, {0} },
	     {{21616, 20416, 18376}, {0} },
	     {{43236, 40832, 36752}, {0} },
	     {{86472, 81668, 73500}, {0} } },
	{11, {{5736, 5416, 4876}, {0} },
	     {{11472, 10832, 9752}, {0} },
	     {{24020, 22684, 20416}, {0} },
	     {{48040, 45372, 40832}, {0} },
	     {{96080, 90740, 81668}, {0} } },
	{12, {{6196, 5852, 5264}, {0} },
	     {{12388, 11700, 10532}, {0} },
	     {{25940, 24500, 22052}, {0} },
	     {{51884, 49000, 44100}, {0} },
	     {{103764, 98000, 88200}, {0} } },
	{13, {{6884, 6500, 5852}, {0} },
	     {{13764, 13000, 11700}, {0} },
	     {{28824, 27224, 24500}, {0} },
	     {{57648, 54444, 49000}, {0} },
	     {{115296, 108888, 98000}, {0} } },
};

/**
 * wma_match_uhr_rate_320() - match raw_rate against the UHR320 rate set
 * @raw_rate: raw rate from fw
 * @index: MCS index into the UHR rate tables
 * @dcm_index: DCM index into the UHR rate tables
 * @nss: nss (output on match)
 * @guard_interval: guard interval (output on match)
 *
 * Return: matched rate if found, else 0
 */
static uint32_t wma_match_uhr_rate_320(uint16_t raw_rate, uint8_t index,
				       uint8_t dcm_index, uint8_t *nss,
				       enum txrate_gi *guard_interval)
{
	const uint32_t *nss_rate[4];

	nss_rate[0] = &uhr_mcs_nss1[index].supported_uhr320_rate[dcm_index][0];
	nss_rate[1] = &uhr_mcs_nss2[index].supported_uhr320_rate[dcm_index][0];
	nss_rate[2] = &uhr_mcs_nss3[index].supported_uhr320_rate[dcm_index][0];
	nss_rate[3] = &uhr_mcs_nss4[index].supported_uhr320_rate[dcm_index][0];

	return wma_mcs_rate_match(raw_rate, 1, nss_rate, nss, guard_interval);
}

/**
 * wma_match_uhr_rate_160() - match raw_rate against the UHR160 rate set
 * @raw_rate: raw rate from fw
 * @index: MCS index into the UHR rate tables
 * @dcm_index: DCM index into the UHR rate tables
 * @nss: nss (output on match)
 * @guard_interval: guard interval (output on match)
 *
 * Return: matched rate if found, else 0
 */
static uint32_t wma_match_uhr_rate_160(uint16_t raw_rate, uint8_t index,
				       uint8_t dcm_index, uint8_t *nss,
				       enum txrate_gi *guard_interval)
{
	const uint32_t *nss_rate[4];

	nss_rate[0] = &uhr_mcs_nss1[index].supported_uhr160_rate[dcm_index][0];
	nss_rate[1] = &uhr_mcs_nss2[index].supported_uhr160_rate[dcm_index][0];
	nss_rate[2] = &uhr_mcs_nss3[index].supported_uhr160_rate[dcm_index][0];
	nss_rate[3] = &uhr_mcs_nss4[index].supported_uhr160_rate[dcm_index][0];

	return wma_mcs_rate_match(raw_rate, 1, nss_rate, nss, guard_interval);
}

/**
 * wma_match_uhr_rate_80() - match raw_rate against the UHR80 rate set
 * @raw_rate: raw rate from fw
 * @index: MCS index into the UHR rate tables
 * @dcm_index: DCM index into the UHR rate tables
 * @nss: nss (output on match)
 * @guard_interval: guard interval (output on match)
 *
 * Return: matched rate if found, else 0
 */
static uint32_t wma_match_uhr_rate_80(uint16_t raw_rate, uint8_t index,
				      uint8_t dcm_index, uint8_t *nss,
				      enum txrate_gi *guard_interval)
{
	const uint32_t *nss_rate[4];

	nss_rate[0] = &uhr_mcs_nss1[index].supported_uhr80_rate[dcm_index][0];
	nss_rate[1] = &uhr_mcs_nss2[index].supported_uhr80_rate[dcm_index][0];
	nss_rate[2] = &uhr_mcs_nss3[index].supported_uhr80_rate[dcm_index][0];
	nss_rate[3] = &uhr_mcs_nss4[index].supported_uhr80_rate[dcm_index][0];

	return wma_mcs_rate_match(raw_rate, 1, nss_rate, nss, guard_interval);
}

/**
 * wma_match_uhr_rate_40() - match raw_rate against the UHR40 rate set
 * @raw_rate: raw rate from fw
 * @index: MCS index into the UHR rate tables
 * @dcm_index: DCM index into the UHR rate tables
 * @nss: nss (output on match)
 * @guard_interval: guard interval (output on match)
 *
 * Return: matched rate if found, else 0
 */
static uint32_t wma_match_uhr_rate_40(uint16_t raw_rate, uint8_t index,
				      uint8_t dcm_index, uint8_t *nss,
				      enum txrate_gi *guard_interval)
{
	const uint32_t *nss_rate[4];

	nss_rate[0] = &uhr_mcs_nss1[index].supported_uhr40_rate[dcm_index][0];
	nss_rate[1] = &uhr_mcs_nss2[index].supported_uhr40_rate[dcm_index][0];
	nss_rate[2] = &uhr_mcs_nss3[index].supported_uhr40_rate[dcm_index][0];
	nss_rate[3] = &uhr_mcs_nss4[index].supported_uhr40_rate[dcm_index][0];

	return wma_mcs_rate_match(raw_rate, 1, nss_rate, nss, guard_interval);
}

/**
 * wma_match_uhr_rate_20() - match raw_rate against the UHR20 rate set
 * @raw_rate: raw rate from fw
 * @index: MCS index into the UHR rate tables
 * @dcm_index: DCM index into the UHR rate tables
 * @nss: nss (output on match)
 * @guard_interval: guard interval (output on match)
 *
 * Return: matched rate if found, else 0
 */
static uint32_t wma_match_uhr_rate_20(uint16_t raw_rate, uint8_t index,
				      uint8_t dcm_index, uint8_t *nss,
				      enum txrate_gi *guard_interval)
{
	const uint32_t *nss_rate[4];

	nss_rate[0] = &uhr_mcs_nss1[index].supported_uhr20_rate[dcm_index][0];
	nss_rate[1] = &uhr_mcs_nss2[index].supported_uhr20_rate[dcm_index][0];
	nss_rate[2] = &uhr_mcs_nss3[index].supported_uhr20_rate[dcm_index][0];
	nss_rate[3] = &uhr_mcs_nss4[index].supported_uhr20_rate[dcm_index][0];

	return wma_mcs_rate_match(raw_rate, 1, nss_rate, nss, guard_interval);
}

uint32_t wma_match_uhr_rate(uint16_t raw_rate,
			    enum tx_rate_info rate_flags,
			    uint8_t *nss, uint8_t *dcm,
			    enum txrate_gi *guard_interval,
			    enum tx_rate_info *mcs_rate_flag,
			    uint8_t *p_index)
{
	uint8_t index;
	uint8_t dcm_index_max = 1;
	uint8_t dcm_index;
	uint32_t match_rate = 0;

	*p_index = 0;

	if (!(rate_flags & (TX_RATE_UHR320 | TX_RATE_UHR160 | TX_RATE_UHR80 |
			    TX_RATE_UHR40  | TX_RATE_UHR20)))
		return 0;

	for (index = 0; index < QDF_ARRAY_SIZE(uhr_mcs_nss1); index++) {
		dcm_index_max = IS_MCS_HAS_DCM_RATE(index) ? 2 : 1;
		for (dcm_index = 0; dcm_index < dcm_index_max; dcm_index++) {
			if (rate_flags & TX_RATE_UHR320) {
				match_rate = wma_match_uhr_rate_320(
						raw_rate, index, dcm_index,
						nss, guard_interval);
				if (match_rate)
					goto rate_found;
			}
			if (rate_flags & TX_RATE_UHR160) {
				match_rate = wma_match_uhr_rate_160(
						raw_rate, index, dcm_index,
						nss, guard_interval);
				if (match_rate)
					goto rate_found;
			}
			if (rate_flags & (TX_RATE_UHR80 | TX_RATE_UHR160)) {
				match_rate = wma_match_uhr_rate_80(
						raw_rate, index, dcm_index,
						nss, guard_interval);
				if (match_rate) {
					*mcs_rate_flag &= ~TX_RATE_UHR160;
					goto rate_found;
				}
			}
			if (rate_flags & (TX_RATE_UHR40 | TX_RATE_UHR80 |
					  TX_RATE_UHR160)) {
				match_rate = wma_match_uhr_rate_40(
						raw_rate, index, dcm_index,
						nss, guard_interval);
				if (match_rate) {
					*mcs_rate_flag &= ~(TX_RATE_UHR80 |
							    TX_RATE_UHR160);
					goto rate_found;
				}
			}
			if (rate_flags & (TX_RATE_UHR80 | TX_RATE_UHR40 |
					  TX_RATE_UHR20 | TX_RATE_UHR160)) {
				match_rate = wma_match_uhr_rate_20(
						raw_rate, index, dcm_index,
						nss, guard_interval);
				if (match_rate) {
					*mcs_rate_flag &= TX_RATE_UHR20;
					goto rate_found;
				}
			}
		}
	}

rate_found:
	if (match_rate) {
		if (dcm_index == 1)
			*dcm = 1;
		*p_index = index;
	}
	return match_rate;
}
#endif /* WLAN_FEATURE_11BN */
