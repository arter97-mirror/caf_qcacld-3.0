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
	struct wlan_mlme_uhr_caps uhr_cap_cfg;
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

	status = ucfg_mlme_cfg_get_uhr_caps(hdd_ctx->psoc, &uhr_cap_cfg);
	if (QDF_IS_STATUS_ERROR(status))
		return;

	if (band_2g) {
		iftype_sta = hdd_ctx->iftype_data_2g;
		iftype_ap = hdd_ctx->iftype_data_2g + 1;
		hdd_ctx->iftype_data_2g->types_mask =
			(BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP));
		band_2g->iftype_data = hdd_ctx->iftype_data_2g;

		hdd_ctx->iftype_data_2g->uhr_cap.has_uhr = uhr_cap_cfg.present;
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

		hdd_ctx->iftype_data_5g->uhr_cap.has_uhr = uhr_cap_cfg.present;
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
	hdd_update_wiphy_uhr_caps_6ghz(hdd_ctx, &uhr_cap_cfg);

	hdd_exit();
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
