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

/**
 * DOC: wlan_hdd_n79_coex.c
 *
 * HDD vendor command dispatch for N79 5 GHz antenna-sharing coexistence.
 */

#ifdef FEATURE_N79_COEX
#include "wlan_hdd_main.h"
#include <osif_sync.h>
#include "wlan_hdd_n79_coex.h"

/**
 * wlan_hdd_cfg80211_n79_coex() - vendor cmd dispatch wrapper for N79 coex
 * @wiphy: pointer to wiphy
 * @wdev: pointer to wdev
 * @data: NL attribute buffer
 * @data_len: buffer length
 *
 * Acquires psoc-level sync (not vdev-level) because N79 activates/deactivates
 * across ALL connected 5 GHz vdevs via wlan_objmgr_iterate_obj_list.  Using
 * osif_vdev_sync would only protect the single wdev that received the command.
 *
 * Return: 0 on success, negative errno on failure.
 */
int wlan_hdd_cfg80211_n79_coex(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct osif_psoc_sync *psoc_sync;
	int errno;

	errno = osif_psoc_sync_op_start(hdd_ctx->parent_dev, &psoc_sync);
	if (errno)
		return errno;

	errno = wlan_cfg80211_coex_n79_vendor_cmd(wiphy, hdd_ctx->psoc,
						  data, data_len);

	osif_psoc_sync_op_stop(psoc_sync);
	return errno;
}
#endif /* FEATURE_N79_COEX */
