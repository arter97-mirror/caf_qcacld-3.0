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
#include <wlan_ipa_ucfg_api.h>
#include <wlan_hdd_host_txrx_stats.h>

/**
 * hdd_cfg80211_host_txrx_stats()
 * - Get wlan host TX/RX and IPA RX drop statistics
 * @wiphy:	  pointer to wiphy
 * @tx_pkts:	  aggregated TX packets
 * @tx_dropped:  aggregated TX dropped
 * @rx_pkts:	  aggregated RX packets
 * @rx_dropped:  aggregated RX dropped
 * @ipa_dropped: IPA RX internal drop count
 *
 * Return: 0 on success; negative errno on failure
 */
static int
hdd_cfg80211_host_txrx_stats(struct wiphy *wiphy,
			     uint32_t tx_pkts,
			     uint32_t tx_dropped,
			     uint32_t rx_pkts,
			     uint32_t rx_dropped,
			     uint64_t ipa_dropped)
{
	struct sk_buff *skb;
	int ret;

	skb = wlan_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
						       nla_total_size(sizeof(u32)) * 4 +
						       nla_total_size(sizeof(u64)));
	if (!skb) {
		hdd_err("Failed to allocate skb for IPA drop count reply");
		return -ENOMEM;
	}

	ret = nla_put_u32(skb,
			  QCA_WLAN_VENDOR_ATTR_HOST_TXRX_STATS_PARAM_TX_PKTS,
			  tx_pkts);
	if (ret)
		goto fail;

	ret = nla_put_u32(skb,
			  QCA_WLAN_VENDOR_ATTR_HOST_TXRX_STATS_PARAM_TX_DROPPED,
			  tx_dropped);
	if (ret)
		goto fail;

	ret = nla_put_u32(skb,
			  QCA_WLAN_VENDOR_ATTR_HOST_TXRX_STATS_PARAM_RX_PKTS,
			  rx_pkts);
	if (ret)
		goto fail;

	ret = nla_put_u32(skb,
			  QCA_WLAN_VENDOR_ATTR_HOST_TXRX_STATS_PARAM_RX_DROPPED,
			  rx_dropped);
	if (ret)
		goto fail;

	ret = nla_put(skb,
		      QCA_WLAN_VENDOR_ATTR_HOST_TXRX_STATS_PARAM_IPA_EXCEPTION_RX_DROPPED,
		      sizeof(u64),
		      &ipa_dropped);

	if (ret)
		goto fail;

	return wlan_cfg80211_vendor_cmd_reply(skb);

fail:
	hdd_err("Failed to put txrx stats attrs, ret=%d", ret);
	kfree_skb(skb);
	return ret;
}

static int
__wlan_hdd_cfg80211_host_txrx_stats_handler(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data,
					    int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter;
	struct hdd_tx_rx_stats *stats;
	struct wlan_ipa_priv *ipa_priv;
	uint32_t tx_pkts = 0, tx_dropped = 0;
	uint32_t rx_pkts = 0, rx_dropped = 0;
	uint64_t ipa_dropped = 0;
	int i, ret;

	hdd_enter();

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		goto exit;

	if (hdd_get_conparam() == QDF_GLOBAL_FTM_MODE) {
		hdd_err("WLAN host txrx count command not allowed in FTM mode");
		ret = -EPERM;
		goto exit;
	}

	adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	if (wlan_hdd_validate_vdev_id(adapter->vdev_id)) {
		ret = -EINVAL;
		goto exit;
	}

	stats = &adapter->hdd_stats.tx_rx_stats;

	for (i = 0; i < NUM_CPUS; i++) {
		tx_pkts     += stats->per_cpu[i].tx_called;
		tx_dropped  += stats->per_cpu[i].tx_dropped;
		rx_pkts     += stats->per_cpu[i].rx_packets;
		rx_dropped  += stats->per_cpu[i].rx_dropped;
	}

	ipa_priv = ipa_pdev_get_priv_obj(hdd_ctx->pdev);
	if (!ipa_priv)
		hdd_warn("IPA priv object is NULL, report IPA drop as 0");
	else
		ipa_dropped = ipa_priv->ipa_rx_internal_drop_count;

	ret = hdd_cfg80211_host_txrx_stats(wiphy,
					   tx_pkts,
					   tx_dropped,
					   rx_pkts,
					   rx_dropped,
					   ipa_dropped);
exit:
	hdd_exit();
	return ret;
}

int wlan_hdd_cfg80211_host_txrx_stats_handler(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      const void *data,
					      int data_len)
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

