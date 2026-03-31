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
 * DOC: wlan_hdd_uhr.h
 *
 * WLAN Host Device Driver file for 802.11bn (Ultra High Reliability)
 * support.
 *
 */

#if !defined(WLAN_HDD_UHR_H)
#define WLAN_HDD_UHR_H
#include "wlan_osif_features.h"

struct hdd_context;
struct wma_tgt_cfg;
struct hdd_beacon_data;
struct sap_config;

#if defined(WLAN_FEATURE_11BN) && defined(CFG80211_FEATURE_11BN_SUPPORT)
/**
 * hdd_update_tgt_uhr_cap() - Update UHR related capabilities
 * @hdd_ctx: HDD context
 * @cfg: Target capabilities
 *
 * This function updates WNI CFG with Target capabilities received as part of
 * Default values present in WNI CFG are the values supported by FW/HW.
 * INI should be introduced if user control is required to control the value.
 *
 * Return: None
 */
void hdd_update_tgt_uhr_cap(struct hdd_context *hdd_ctx,
			    struct wma_tgt_cfg *cfg);

/**
 * hdd_update_wiphy_uhr_cap() - update the wiphy with uhr capabilities
 * @hdd_ctx: HDD context
 *
 * update wiphy with the uhr capabilities.
 *
 * Return: None
 */
void hdd_update_wiphy_uhr_cap(struct hdd_context *hdd_ctx);
#else
static inline void hdd_update_tgt_uhr_cap(struct hdd_context *hdd_ctx,
					  struct wma_tgt_cfg *cfg)
{
}

static inline void hdd_update_wiphy_uhr_cap(struct hdd_context *hdd_ctx)
{
}
#endif

#if defined(WLAN_FEATURE_11BN_TEST_SAP)
/**
 * wlan_hdd_check_11bn_support() - Check if 11bn supported
 * @beacon: Pointer to beacon data
 * @config: Pointer to SAP configuration
 *
 *
 * Return: None.
 */
void wlan_hdd_check_11bn_support(struct hdd_beacon_data *beacon,
				 struct sap_config *config);
#else
static inline void wlan_hdd_check_11bn_support(struct hdd_beacon_data *beacon,
					       struct sap_config *config)
{
}
#endif
#endif /* WLAN_HDD_UHR_H */

