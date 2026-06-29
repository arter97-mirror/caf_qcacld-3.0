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
 * DOC: declares driver functions interfacing with linux kernel
 */

#ifndef _WLAN_CFG80211_COEX_H_
#define _WLAN_CFG80211_COEX_H_
#include <wlan_cfg80211.h>
#include <wlan_objmgr_cmn.h>

extern const struct nla_policy
	btc_chain_mode_policy
	[QCA_VENDOR_ATTR_BTC_CHAIN_MODE_MAX + 1];

#ifdef FEATURE_COEX
int wlan_cfg80211_coex_set_btc_chain_mode(struct wlan_objmgr_vdev *vdev,
					  const void *data, int data_len);
#else
static inline int
wlan_cfg80211_coex_set_btc_chain_mode(struct wlan_objmgr_vdev *vdev,
				      const void *data, int data_len)
{
	return -ENOTSUPP;
}
#endif

#ifdef FEATURE_N79_COEX
#include "qca_vendor.h"

extern const struct nla_policy
	n79_coex_policy[QCA_WLAN_VENDOR_ATTR_N79_COEX_CONFIG_MAX + 1];

/**
 * wlan_cfg80211_coex_n79_vendor_cmd() - handle N79 coex vendor command
 * @wiphy: pointer to wiphy (needed for GET reply)
 * @psoc: psoc object
 * @data: NL attribute buffer
 * @data_len: buffer length
 *
 * OP_TYPE is optional; absent means SET.
 * SET: calls ucfg_coex_psoc_set_n79_active().
 * GET: returns current active state via cfg80211_vendor_cmd_reply().
 *
 * Return: 0 on success, negative errno on failure
 */
int wlan_cfg80211_coex_n79_vendor_cmd(struct wiphy *wiphy,
				      struct wlan_objmgr_psoc *psoc,
				      const void *data, int data_len);
#else
static inline int
wlan_cfg80211_coex_n79_vendor_cmd(struct wiphy *wiphy,
				  struct wlan_objmgr_psoc *psoc,
				  const void *data, int data_len)
{
	return -ENOTSUPP;
}
#endif /* FEATURE_N79_COEX */
#endif
