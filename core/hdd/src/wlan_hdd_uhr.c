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
 * DOC: wlan_hdd_uhr.c
 *
 * WLAN Host Device Driver file for 802.11bn (Ultra High Reliability)
 * support.
 *
 */

#include "wlan_hdd_main.h"
#include "wlan_hdd_uhr.h"
#include "osif_sync.h"
#include "wlan_utility.h"
#include "wlan_mlme_ucfg_api.h"
#include "qc_sap_ioctl.h"
#include "wma_api.h"
#include "wlan_osif_features.h"
#include "wlan_psoc_mlme_ucfg_api.h"
#include "wlan_cmn_ieee80211.h"
#include "wlan_psoc_mlme_api.h"

#if defined(WLAN_FEATURE_11BN) && defined(CFG80211_FEATURE_11BN_SUPPORT)
void hdd_update_tgt_uhr_cap(struct hdd_context *hdd_ctx,
			    struct wma_tgt_cfg *cfg)
{
	sme_update_tgt_uhr_cap(hdd_ctx->mac_handle, cfg);
	ucfg_mlme_update_tgt_uhr_cap(hdd_ctx->psoc, cfg);
}

static void
hdd_update_wiphy_uhr_caps_6ghz(struct hdd_context *hdd_ctx,
			       struct wlan_mlme_uhr_caps *uhr_cap)
{
	struct ieee80211_supported_band *band_6g =
		   hdd_ctx->wiphy->bands[HDD_NL80211_BAND_6GHZ];
	uint8_t *phy_info = &hdd_ctx->iftype_data_6g->uhr_cap.phy.cap;
	struct ieee80211_sband_iftype_data *iftype_sta;
	struct ieee80211_sband_iftype_data *iftype_ap;

	if (!band_6g || !phy_info) {
		hdd_debug("6ghz not supported in wiphy");
		return;
	}

	hdd_ctx->iftype_data_6g->types_mask =
		(BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP));
	band_6g->iftype_data = hdd_ctx->iftype_data_6g;
	iftype_sta = hdd_ctx->iftype_data_6g;
	iftype_ap = hdd_ctx->iftype_data_6g + 1;

	hdd_ctx->iftype_data_6g->uhr_cap.has_uhr = uhr_cap->present;
	if (!hdd_ctx->iftype_data_6g->uhr_cap.has_uhr) {
		hdd_debug("6 GHz caps not present");
		hdd_ctx->iftype_data_6g->uhr_cap.has_uhr = false;
		return;
	}

	qdf_mem_copy(iftype_ap, hdd_ctx->iftype_data_6g,
		     sizeof(struct ieee80211_sband_iftype_data));

	iftype_sta->types_mask = BIT(NL80211_IFTYPE_STATION);
	iftype_ap->types_mask = BIT(NL80211_IFTYPE_AP);
}

void hdd_update_wiphy_uhr_cap(struct hdd_context *hdd_ctx)
{
	struct wlan_mlme_uhr_caps uhr_cap_2g;
	struct wlan_mlme_uhr_caps uhr_cap_5g;
	struct ieee80211_supported_band *band_2g =
			hdd_ctx->wiphy->bands[HDD_NL80211_BAND_2GHZ];
	struct ieee80211_supported_band *band_5g =
			hdd_ctx->wiphy->bands[HDD_NL80211_BAND_5GHZ];
	QDF_STATUS status;
	bool uhr_capab;
	struct ieee80211_sband_iftype_data *iftype_sta;
	struct ieee80211_sband_iftype_data *iftype_ap;

	hdd_enter();

	wlan_psoc_mlme_get_11bn_capab(hdd_ctx->psoc, &uhr_capab);
	if (!uhr_capab)
		return;

	status = ucfg_mlme_cfg_get_uhr_caps_2g(hdd_ctx->psoc, &uhr_cap_2g);
	if (QDF_IS_STATUS_ERROR(status))
		return;

	status = ucfg_mlme_cfg_get_uhr_caps_5g(hdd_ctx->psoc, &uhr_cap_5g);
	if (QDF_IS_STATUS_ERROR(status))
		return;

	if (band_2g) {
		iftype_sta = hdd_ctx->iftype_data_2g;
		iftype_ap = hdd_ctx->iftype_data_2g + 1;
		hdd_ctx->iftype_data_2g->types_mask =
			(BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP));
		band_2g->iftype_data = hdd_ctx->iftype_data_2g;

		hdd_ctx->iftype_data_2g->uhr_cap.has_uhr = uhr_cap_2g.present;
		if (!hdd_ctx->iftype_data_2g->uhr_cap.has_uhr) {
			hdd_debug("2.4 GHz UHR caps not present");
			hdd_ctx->iftype_data_2g->uhr_cap.has_uhr = false;
			goto band_5ghz;
		}

		qdf_mem_copy(iftype_ap, hdd_ctx->iftype_data_2g,
			     sizeof(struct ieee80211_sband_iftype_data));

		iftype_sta->types_mask = BIT(NL80211_IFTYPE_STATION);
		iftype_ap->types_mask = BIT(NL80211_IFTYPE_AP);
	}

band_5ghz:
	if (band_5g) {
		iftype_sta = hdd_ctx->iftype_data_5g;
		iftype_ap = hdd_ctx->iftype_data_5g + 1;
		hdd_ctx->iftype_data_5g->types_mask =
			(BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP));
		band_5g->iftype_data = hdd_ctx->iftype_data_5g;

		hdd_ctx->iftype_data_5g->uhr_cap.has_uhr = uhr_cap_5g.present;
		if (!hdd_ctx->iftype_data_5g->uhr_cap.has_uhr) {
			hdd_debug("5 GHz UHR caps not present");
			hdd_ctx->iftype_data_5g->uhr_cap.has_uhr = false;
			goto band_6ghz;
		}

		qdf_mem_copy(iftype_ap, hdd_ctx->iftype_data_5g,
			     sizeof(struct ieee80211_sband_iftype_data));

		iftype_sta->types_mask = BIT(NL80211_IFTYPE_STATION);
		iftype_ap->types_mask = BIT(NL80211_IFTYPE_AP);
	}

band_6ghz:
	hdd_update_wiphy_uhr_caps_6ghz(hdd_ctx, &uhr_cap_5g);

	hdd_exit();
}
#endif

#ifdef WLAN_FEATURE_11BN
int hdd_set_11bn_rate_code(struct hdd_adapter *adapter, uint16_t rate_code)
{
	uint8_t preamble = 0, nss = 0, rix = 0;
	int ret;
	struct sap_config *sap_config = NULL;

	if (adapter->device_mode == QDF_SAP_MODE)
		sap_config = &adapter->deflink->session.ap.sap_config;

	if (!sap_config) {
		if (!sme_is_feature_supported_by_fw(DOT11BN)) {
			hdd_err_rl("Target does not support 11bn");
			return -EIO;
		}
	} else if (sap_config->SapHw_mode != eCSR_DOT11_MODE_11bn &&
		   sap_config->SapHw_mode != eCSR_DOT11_MODE_11bn_ONLY) {
		hdd_err_rl("Invalid hw mode, SAP hw_mode= 0x%x, ch_freq = %d",
			   sap_config->SapHw_mode, sap_config->chan_freq);
		return -EIO;
	}

	if ((rate_code >> 8) != WMI_RATE_PREAMBLE_UHR) {
		hdd_err_rl("Invalid input: %x", rate_code);
		return -EIO;
	}

	rix = RC_2_RATE_IDX_11BN(rate_code);
	preamble = rate_code >> 8;
	nss = HT_RC_2_STREAMS_11BN(rate_code);

	hdd_debug("SET_11BN_RATE rate_code %d rix %d preamble %x nss %d",
		  rate_code, rix, preamble, nss);

	ret = wma_cli_set_command(adapter->deflink->vdev_id,
				  wmi_vdev_param_fixed_rate,
				  rate_code, VDEV_CMD);

	return ret;
}

void wlan_hdd_fill_os_uhr_rateflags(struct rate_info *os_rate,
				    enum tx_rate_info rate_flags)
{
	enum tx_rate_info bw;

	if (!(rate_flags & (TX_RATE_UHR80 | TX_RATE_UHR40 |
	    TX_RATE_UHR20 | TX_RATE_UHR160 | TX_RATE_UHR320)))
		return;

	/* as fw not yet report ofdma to host, so don't
	 * fill RATE_INFO_BW_UHR_RU.
	 */
	if (rate_flags & TX_RATE_UHR320)
		bw = TX_RATE_UHR320;
	else if (rate_flags & TX_RATE_UHR160)
		bw = TX_RATE_UHR160;
	else if (rate_flags & TX_RATE_UHR80)
		bw = TX_RATE_UHR80;
	else if (rate_flags & TX_RATE_UHR40)
		bw = TX_RATE_UHR40;
	else
		bw = TX_RATE_UHR20;

	switch (bw) {
	case TX_RATE_UHR320:
		hdd_set_rate_bw(os_rate, HDD_RATE_BW_320);
		break;
	case TX_RATE_UHR160:
		hdd_set_rate_bw(os_rate, HDD_RATE_BW_160);
		break;
	case TX_RATE_UHR80:
		hdd_set_rate_bw(os_rate, HDD_RATE_BW_80);
		break;
	case TX_RATE_UHR40:
		hdd_set_rate_bw(os_rate, HDD_RATE_BW_40);
		break;
	default:
		break;
	}

	os_rate->flags |= RATE_INFO_FLAGS_UHR_MCS;
}

bool wlan_hdd_refill_os_uhr_rateflags(struct rate_info *os_rate,
				      uint8_t preamble)
{
	if (preamble == DOT11_BN) {
		os_rate->flags |= RATE_INFO_FLAGS_UHR_MCS;
		return true;
	}
	return false;
}
#endif

#if defined(WLAN_FEATURE_11BN_TEST_SAP)
void wlan_hdd_check_11bn_support(struct hdd_beacon_data *beacon,
				 struct sap_config *config)
{
	const uint8_t *ie;

	ie = wlan_get_ext_ie_ptr_from_ext_id(UHR_CAP_OUI_TYPE, UHR_CAP_OUI_SIZE,
					     beacon->tail, beacon->tail_len);
	if (ie)
		config->SapHw_mode = eCSR_DOT11_MODE_11bn;
}
#endif
