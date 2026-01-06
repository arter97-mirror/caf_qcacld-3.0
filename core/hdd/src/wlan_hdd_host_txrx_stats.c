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
 * DOC: wlan_hdd_host_txrx_stats.c
 *
 * This file provide definitions for cfg80211 vendor command handler APIs
 * related to WLAN host TX/RX and IPA exception RX drop statistics.
 */

#include <qdf_list.h>
#include <qdf_status.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <wlan_cfg80211.h>
#include <wlan_osif_priv.h>
#include <osif_psoc_sync.h>
#include <qdf_mem.h>
#include <wlan_utility.h>
#include "wlan_hdd_main.h"
#include "cfg_ucfg_api.h"
#include <wlan_hdd_host_txrx_stats.h>

static int
__wlan_hdd_cfg80211_host_txrx_stats_handler(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data,
					 int data_len)
{
	int ret;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter;

	hdd_enter();

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		goto exit;

	if (!hdd_ctx->pdev) {
		hdd_err("pdev is NULL");
		ret = -EINVAL;
		goto exit;
	}

	if (hdd_get_conparam() == QDF_GLOBAL_FTM_MODE) {
		hdd_err("WLAN host txrx count command not allowed in FTM mode");
		ret = -EPERM;
		goto exit;
	}

	adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	if (!adapter) {
		hdd_err("Invalid adapter");
		ret = -EINVAL;
		goto exit;
	}
	if (wlan_hdd_validate_vdev_id(adapter->vdev_id)) {
		hdd_err("Invalid vdev_id: %d", adapter->vdev_id);
		ret = -EINVAL;
		goto exit;
	}

	ret = wlan_cfg80211_host_txrx_stats(wiphy, wdev,
					     hdd_ctx->pdev,
					     data, data_len);

exit:
	hdd_exit();
	return ret;
}

int wlan_hdd_cfg80211_host_txrx_stats_handler(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   const void *data, int data_len)
{
	struct osif_psoc_sync *psoc_sync;
	int errno;

	errno = osif_psoc_sync_op_start(wiphy_dev(wiphy), &psoc_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_host_txrx_stats_handler(wiphy,
							 wdev,
							 data,
							 data_len);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno;
}

