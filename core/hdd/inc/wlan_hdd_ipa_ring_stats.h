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
 * DOC: wlan_hdd_ipa_ring_stats.h
 *
 * This header file provides declaration for cfg80211 vendor command handler
 * APIs related to IPA ring statistics.
 */

#ifndef __WLAN_HDD_IPA_RING_STATS_H__
#define __WLAN_HDD_IPA_RING_STATS_H__

#include <qdf_types.h>
#include <net/cfg80211.h>
#include <qca_vendor.h>

#ifdef WLAN_FEATURE_IPA_RING_STATS
#include <wlan_cfg80211_ipa_ring_stats.h>

#define FEATURE_IPA_RING_STATS_VENDOR_COMMANDS \
{ \
	.info.vendor_id = QCA_NL80211_VENDOR_ID, \
	.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_IPA_RING_STATS, \
	.flags = WIPHY_VENDOR_CMD_NEED_WDEV | \
		 WIPHY_VENDOR_CMD_NEED_NETDEV | \
		 WIPHY_VENDOR_CMD_NEED_RUNNING, \
	.doit = wlan_hdd_cfg80211_ipa_ring_stats_handler, \
	vendor_command_policy(wlan_cfg80211_ipa_ring_stats_policy, \
			      QCA_WLAN_VENDOR_ATTR_IPA_RING_STATS_PARAM_MAX) \
},

/**
 * wlan_hdd_cfg80211_ipa_ring_stats_handler() - handle IPA ring stats vendor command
 * @wiphy: Pointer to wiphy structure
 * @wdev: Pointer to wireless device structure
 * @data: Pointer to the data passed via vendor interface
 * @data_len: Length of the data passed via vendor interface
 *
 * This function is the cfg80211 vendor command handler for IPA ring stats.
 * It parses the userspace vendor command, converts it to the internal
 * IPA stats request, and forwards it to the lower layers (ucfg/tgt/fw)
 * via WMI interface.
 *
 * Return: 0 on success or negative errno on failure
 */
int wlan_hdd_cfg80211_ipa_ring_stats_handler(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int data_len);

#else /* WLAN_FEATURE_IPA_RING_STATS */

#define FEATURE_IPA_RING_STATS_VENDOR_COMMANDS

#endif /* WLAN_FEATURE_IPA_RING_STATS */

#endif /* __WLAN_HDD_IPA_RING_STATS_H__ */
