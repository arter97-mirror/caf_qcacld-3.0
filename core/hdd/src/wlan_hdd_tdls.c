/*
 * Copyright (c) 2012-2020 The Linux Foundation. All rights reserved.
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
 * DOC: wlan_hdd_tdls.c
 *
 * WLAN Host Device Driver implementation for TDLS
 */

#include <wlan_hdd_includes.h>
#include <ani_global.h>
#include "osif_sync.h"
#include <wlan_hdd_hostapd.h>
#include <wlan_hdd_trace.h>
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>
#include "wlan_hdd_tdls.h"
#include "wlan_hdd_cfg80211.h"
#include "wlan_hdd_assoc.h"
#include "sme_api.h"
#include "cds_sched.h"
#include "wma_types.h"
#include "wlan_policy_mgr_api.h"
#include <qca_vendor.h>
#include "wlan_tdls_cfg_api.h"
#include "wlan_hdd_object_manager.h"
#include <wlan_reg_ucfg_api.h>
#include "wlan_tdls_api.h"
#include "wlan_policy_mgr_ucfg.h"
#include <wlan_tdls_stats_api.h>

/**
 * enum qca_wlan_vendor_tdls_trigger_mode_hdd_map: Maps the user space TDLS
 *	trigger mode in the host driver.
 * @WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT: TDLS Connection and
 *	disconnection handled by user space.
 * @WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT: TDLS connection and
 *	disconnection controlled by host driver based on data traffic.
 * @WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_EXTERNAL: TDLS connection and
 *	disconnection jointly controlled by user space and host driver.
 */
enum qca_wlan_vendor_tdls_trigger_mode_hdd_map {
	WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT =
		QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT,
	WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT =
		QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT,
	WLAN_HDD_VENDOR_TDLS_TRIGGER_MODE_EXTERNAL =
		((QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_EXPLICIT |
		  QCA_WLAN_VENDOR_TDLS_TRIGGER_MODE_IMPLICIT) << 1),
};

/**
 * wlan_hdd_tdls_get_all_peers() - dump all TDLS peer info into output string
 * @adapter: HDD adapter
 * @buf: output string buffer to hold the peer info
 * @buflen: the size of output string buffer
 *
 * Return: The size (in bytes) of the valid peer info in the output buffer
 */
int wlan_hdd_tdls_get_all_peers(struct hdd_adapter *adapter,
				char *buf, int buflen)
{
	int len;
	struct hdd_context *hdd_ctx;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_objmgr_vdev *link_vdev;
	int ret;

	hdd_enter();

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	if (0 != (wlan_hdd_validate_context(hdd_ctx))) {
		len = scnprintf(buf, buflen,
				"\nHDD context is not valid\n");
		return len;
	}

	if ((QDF_STA_MODE != adapter->device_mode) &&
	    (QDF_P2P_CLIENT_MODE != adapter->device_mode)) {
		len = scnprintf(buf, buflen,
				"\nNo TDLS support for this adapter\n");
		return len;
	}

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (!vdev) {
		len = scnprintf(buf, buflen, "\nVDEV is NULL\n");
		return len;
	}

	link_vdev = ucfg_tdls_get_tdls_link_vdev(vdev, WLAN_OSIF_TDLS_ID);
	if (link_vdev) {
		ret = wlan_cfg80211_tdls_get_all_peers(link_vdev, buf, buflen);
		ucfg_tdls_put_tdls_link_vdev(link_vdev, WLAN_OSIF_TDLS_ID);
	} else {
		ret = wlan_cfg80211_tdls_get_all_peers(vdev, buf, buflen);
	}

	hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);

	return ret;
}

const struct nla_policy
	wlan_hdd_tdls_disc_rsp_policy
	[QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_MAX + 1] = {
		[QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_TX_LINK] = {
						.type = NLA_U8},
};

#ifdef FEATURE_TDLS_STATS_VENDOR_EVENTS
const struct nla_policy
	wlan_hdd_tdls_stats_policy
	[QCA_WLAN_VENDOR_ATTR_TDLS_STATS_MAX + 1] = {
		[QCA_WLAN_VENDOR_ATTR_TDLS_STATS_CONFIG] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRIES] = {
						.type = NLA_NESTED},
};
#endif /* FEATURE_TDLS_STATS_VENDOR_EVENTS */

const struct nla_policy
	wlan_hdd_tdls_mode_configuration_policy
	[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX + 1] = {
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_STATS_PERIOD] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TX_THRESHOLD] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_DISCOVERY_PERIOD] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX_DISCOVERY_ATTEMPT] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_TIMEOUT] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_IDLE_PACKET_THRESHOLD] = {
						.type = NLA_U32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_SETUP_RSSI_THRESHOLD] = {
						.type = NLA_S32},
		[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TEARDOWN_RSSI_THRESHOLD] = {
						.type = NLA_S32},
};

bool wlan_hdd_is_tdls_allowed(struct hdd_context *hdd_ctx,
			      struct wlan_objmgr_vdev *vdev)
{
	bool tdls_support;
	struct wlan_hdd_link_info *link_info;

	if ((cfg_tdls_get_support_enable(hdd_ctx->psoc, &tdls_support) ==
		QDF_STATUS_SUCCESS) && !tdls_support) {
		hdd_debug("TDLS feature not Enabled or Not supported in FW");
		return false;
	}

	if (wlan_vdev_is_up(vdev) != QDF_STATUS_SUCCESS) {
		hdd_debug("vdev %d Failed due to Not associated",
			  wlan_vdev_get_id(vdev));
		return false;
	}

	if (wlan_cm_roaming_in_progress(hdd_ctx->pdev,
					wlan_vdev_get_id(vdev))) {
		hdd_debug("Failed due to Roaming is in progress");
		return false;
	}

	if (!ucfg_tdls_check_is_tdls_allowed(vdev)) {
		hdd_debug("TDLS is not allowed");
		return false;
	}

	if (ucfg_mlme_get_tdls_prohibited(vdev)) {
		hdd_debug("TDLS is prohobited by AP");
		return false;
	}

	link_info = hdd_get_link_info_by_vdev(hdd_ctx, wlan_vdev_get_id(vdev));
	if (wlan_hdd_is_link_switch_in_progress(link_info)) {
		hdd_debug("vdev:%d Link switch in progress",
			  wlan_vdev_get_id(vdev));
		return false;
	}

	return true;
}

static bool wlan_hdd_get_tdls_allowed(struct hdd_context *hdd_ctx,
				      struct hdd_adapter *adapter)
{
	struct wlan_hdd_link_info *link_info;
	struct wlan_objmgr_vdev *vdev;
	bool is_tdls_avail = false;

	hdd_adapter_for_each_active_link_info(adapter, link_info) {
		vdev = hdd_objmgr_get_vdev_by_user(link_info, WLAN_TDLS_NB_ID);
		if (!vdev)
			return false;

		is_tdls_avail = wlan_hdd_is_tdls_allowed(hdd_ctx, vdev);

		/* Return is_tdls_avail for non-MLO case */
		if (!wlan_vdev_mlme_is_mlo_vdev(vdev)) {
			hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);
			return is_tdls_avail;
		}

		hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);

		if (is_tdls_avail)
			return is_tdls_avail;
	}

	return false;
}

/**
 * __wlan_hdd_cfg80211_exttdls_get_status() - handle get status cfg80211 command
 * @wiphy: wiphy
 * @wdev: wireless dev
 * @data: netlink buffer with the mac address of the peer to get the status for
 * @data_len: length of data in bytes
 */
static int
__wlan_hdd_cfg80211_exttdls_get_status(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data,
					 int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct sk_buff *skb;
	uint32_t connected_peer_count = 0;
	int status;
	bool is_tdls_avail = true;
	int ret = 0;
	int attr;

	hdd_enter_dev(wdev->netdev);
	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	status = wlan_hdd_validate_context(hdd_ctx);
	if (status)
		return -EINVAL;

	skb = wlan_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
						sizeof(u32) + sizeof(bool) +
						NLMSG_HDRLEN);

	if (!skb) {
		hdd_err("wlan_cfg80211_vendor_cmd_alloc_reply_skb failed");
		return -ENOMEM;
	}

	if (adapter->device_mode != QDF_STA_MODE &&
	    adapter->device_mode != QDF_P2P_CLIENT_MODE) {
		hdd_debug("Failed to get TDLS info due to opmode:%d",
			  adapter->device_mode);
		ret = -EOPNOTSUPP;
		goto fail;
	}

	connected_peer_count = cfg_tdls_get_connected_peer_count(hdd_ctx->psoc);
	is_tdls_avail = wlan_hdd_get_tdls_allowed(hdd_ctx, adapter);

	if (connected_peer_count >=
			cfg_tdls_get_max_peer_count(hdd_ctx->psoc)) {
		hdd_debug("Failed due to max no. of connected peer:%d reached",
			  connected_peer_count);
		is_tdls_avail = false;
	}

	hdd_debug("Send TDLS_available: %d, no. of connected peer:%d to userspace",
		  is_tdls_avail, connected_peer_count);

	attr = QCA_WLAN_VENDOR_ATTR_TDLS_GET_STATUS_NUM_SESSIONS;
	if (nla_put_u32(skb, attr, connected_peer_count)) {
		hdd_err("nla put fail");
		ret = -EINVAL;
		goto fail;
	}

	attr = QCA_WLAN_VENDOR_ATTR_TDLS_GET_STATUS_AVAILABLE;
	if (is_tdls_avail && nla_put_flag(skb, attr)) {
		hdd_err("nla put fail");
		ret = -EINVAL;
		goto fail;
	}

	return wlan_cfg80211_vendor_cmd_reply(skb);
fail:
	wlan_cfg80211_vendor_free_skb(skb);
	return ret;
}

static int
__wlan_hdd_cfg80211_exttdls_set_link_id(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data,
					int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_MAX + 1];
	int ret;
	uint32_t link_id;

	hdd_enter_dev(dev);

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (0 != ret)
		return -EINVAL;

	if (!adapter)
		return -EINVAL;

	if (wlan_cfg80211_nla_parse(tb,
				    QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_MAX,
				    data, data_len,
				    wlan_hdd_tdls_disc_rsp_policy)) {
		hdd_err("Invalid attribute");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_TX_LINK]) {
		hdd_err("attr tdls link id failed");
		return -EINVAL;
	}

	link_id =
		nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_TDLS_DISC_RSP_EXT_TX_LINK]);
	hdd_debug("TDLS link id %d", link_id);

	ret = cfg_tdls_set_link_id(hdd_ctx->psoc, link_id);

	return ret;
}

#ifdef FEATURE_TDLS_STATS_VENDOR_EVENTS
static int
__wlan_hdd_cfg80211_get_tdls_stats(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   const void *data,
				   int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_TDLS_STATS_MAX + 1];
	int ret = 0;
	uint32_t id;
	enum qca_wlan_tdls_stats_config tdls_stats_enable;
	struct nlattr *tdls_stats_attr;
	QDF_STATUS status;

	hdd_enter_dev(dev);
	if (!adapter)
		return -EINVAL;

	if (adapter->device_mode != QDF_STA_MODE) {
		hdd_debug("Failed to get TDLS info due to opmode:%d",
			  adapter->device_mode);
		return -EOPNOTSUPP;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (0 != ret)
		return ret;

	if (wlan_cfg80211_nla_parse(tb, QCA_WLAN_VENDOR_ATTR_TDLS_STATS_MAX,
				    data, data_len,
				    wlan_hdd_tdls_stats_policy)) {
		hdd_err("Invalid attribute");
		return -EINVAL;
	}

	id = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_CONFIG;
	tdls_stats_attr = tb[id];

	if (!tdls_stats_attr) {
		hdd_err("TDLS stats enable/disable NOT specified");
		return -EINVAL;
	}

	tdls_stats_enable = nla_get_u32(tdls_stats_attr);
	hdd_debug("Userspace TDLS stats: %d", tdls_stats_enable);

	status = wlan_tdls_get_tdls_stats(hdd_ctx->psoc,
					  tdls_stats_enable);

	return qdf_status_to_os_return(status);
}
#endif /* FEATURE_TDLS_STATS_VENDOR_EVENTS */

/**
 * __wlan_hdd_cfg80211_configure_tdls_mode() - configure the tdls mode
 * @wiphy: wiphy
 * @wdev: wireless dev
 * @data: netlink buffer
 * @data_len: length of data in bytes
 *
 * Return 0 for success and error code for failure
 */
static int
__wlan_hdd_cfg80211_configure_tdls_mode(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data,
					 int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX + 1];
	int ret;
	uint32_t trigger_mode;
	struct wlan_objmgr_vdev *vdev;

	hdd_enter_dev(dev);

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (0 != ret)
		return -EINVAL;

	if (!adapter)
		return -EINVAL;

	if (wlan_cfg80211_nla_parse(tb, QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_MAX,
				    data, data_len,
				    wlan_hdd_tdls_mode_configuration_policy)) {
		hdd_err("Invalid attribute");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE]) {
		hdd_err("attr tdls trigger mode failed");
		return -EINVAL;
	}
	trigger_mode = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_TDLS_CONFIG_TRIGGER_MODE]);
	hdd_debug("TDLS trigger mode %d", trigger_mode);

	if (!hdd_ctx->tdls_umac_comp_active)
		return -EINVAL;

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (!vdev)
		return -EINVAL;

	ret = wlan_cfg80211_tdls_configure_mode(vdev, trigger_mode);
	hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
	return ret;
}

/**
 * wlan_hdd_cfg80211_configure_tdls_mode() - configure tdls mode
 * @wiphy:   pointer to wireless wiphy structure.
 * @wdev:    pointer to wireless_dev structure.
 * @data:    Pointer to the data to be passed via vendor interface
 * @data_len:Length of the data to be passed
 *
 * Return:   Return the Success or Failure code.
 */
int wlan_hdd_cfg80211_configure_tdls_mode(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data,
					int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_configure_tdls_mode(wiphy, wdev, data,
							data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_cfg80211_exttdls_get_status() - get ext tdls status
 * @wiphy:   pointer to wireless wiphy structure.
 * @wdev:    pointer to wireless_dev structure.
 * @data:    Pointer to the data to be passed via vendor interface
 * @data_len:Length of the data to be passed
 *
 * Return:   Return the Success or Failure code.
 */
int wlan_hdd_cfg80211_exttdls_get_status(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data,
					int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_exttdls_get_status(wiphy, wdev,
						       data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

int wlan_hdd_cfg80211_exttdls_set_link_id(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  const void *data,
					  int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_exttdls_set_link_id(wiphy, wdev,
							data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

#ifdef FEATURE_TDLS_STATS_VENDOR_EVENTS
int wlan_hdd_cfg80211_get_tdls_stats(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_get_tdls_stats(wiphy, wdev,
						   data, data_len);
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}
#endif /* FEATURE_TDLS_STATS_VENDOR_EVENTS */

static int wlan_hdd_tdls_enable(struct hdd_context *hdd_ctx,
				struct hdd_adapter *adapter)
{
	struct wlan_hdd_link_info *link_info;
	struct wlan_objmgr_vdev *vdev;
	bool tdls_chan_switch_prohibited;
	bool tdls_prohibited;

	hdd_adapter_for_each_active_link_info(adapter, link_info) {
		vdev = hdd_objmgr_get_vdev_by_user(link_info, WLAN_TDLS_NB_ID);
		if (!vdev)
			return -EINVAL;

		tdls_chan_switch_prohibited =
			ucfg_mlme_get_tdls_chan_switch_prohibited(vdev);
		tdls_prohibited = ucfg_mlme_get_tdls_prohibited(vdev);

		ucfg_tdls_set_user_tdls_enable(vdev, true);

		wlan_tdls_notify_sta_connect(wlan_vdev_get_id(vdev),
					     tdls_chan_switch_prohibited,
					     tdls_prohibited, vdev);
		if (!wlan_vdev_mlme_is_mlo_vdev(vdev)) {
			hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);
			return 0;
		}

		hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);
	}

	return 0;
}

/**
 * __wlan_hdd_cfg80211_exttdls_enable() - enable an externally controllable
 *                                      TDLS peer and set parameters
 * @wiphy: pointer to wireless wiphy structure.
 * @wdev: wireless dev pointer
 * @data: netlink buffer with peer MAC address and configuration parameters
 * @data_len: size of data in bytes
 *
 * This function sets channel, operation class, maximum latency and minimal
 * bandwidth parameters on a TDLS peer that's externally controllable.
 *
 * Return: 0 for success; negative errno otherwise
 */
static int
__wlan_hdd_cfg80211_exttdls_enable(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	int ret = 0;

	hdd_enter_dev(wdev->netdev);
	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (wlan_hdd_validate_context(hdd_ctx))
		return -EINVAL;

	if (adapter->device_mode != QDF_STA_MODE &&
	    adapter->device_mode != QDF_P2P_CLIENT_MODE) {
		hdd_debug("Failed to get TDLS info due to opmode:%d",
			  adapter->device_mode);
		return -EOPNOTSUPP;
	}

	ret = wlan_hdd_tdls_enable(hdd_ctx, adapter);

	return ret;
}

/**
 * wlan_hdd_cfg80211_exttdls_enable() - enable ext tdls
 * @wiphy:   pointer to wireless wiphy structure.
 * @wdev:    pointer to wireless_dev structure.
 * @data:    Pointer to the data to be passed via vendor interface
 * @data_len:Length of the data to be passed
 *
 * Return:   Return the Success or Failure code.
 */
int wlan_hdd_cfg80211_exttdls_enable(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data,
					int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_exttdls_enable(wiphy, wdev, data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

static int wlan_hdd_tdls_disable(struct hdd_context *hdd_ctx,
				 struct hdd_adapter *adapter)
{
	struct wlan_hdd_link_info *link_info;
	struct wlan_objmgr_vdev *vdev;

	hdd_adapter_for_each_active_link_info(adapter, link_info) {
		vdev = hdd_objmgr_get_vdev_by_user(link_info, WLAN_TDLS_NB_ID);
		if (!vdev)
			return -EINVAL;

		/*
		 * Record teardown stats for all connected TDLS peers before
		 * tearing down the links due to user-initiated TDLS disable.
		 */
		wlan_tdls_stats_record_peers_teardown(hdd_ctx->psoc,
						      wlan_vdev_get_id(vdev),
						      TDLS_STATS_REASON_GENERAL);

		ucfg_tdls_teardown_links(hdd_ctx->psoc);
		ucfg_tdls_set_user_tdls_enable(vdev, false);

		if (!wlan_vdev_mlme_is_mlo_vdev(vdev)) {
			hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);
			return 0;
		}

		hdd_objmgr_put_vdev_by_user(vdev, WLAN_TDLS_NB_ID);
	}

	return 0;
}

/**
 * __wlan_hdd_cfg80211_exttdls_disable() - disable an externally controllable
 *                                       TDLS peer
 * @wiphy: wiphy
 * @wdev: wireless dev pointer
 * @data: netlink buffer with peer MAC address
 * @data_len: size of data in bytes
 *
 * This function disables an externally controllable TDLS peer
 *
 * Return: 0 for success; negative errno otherwise
 */
static int __wlan_hdd_cfg80211_exttdls_disable(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	int ret = 0;

	hdd_enter_dev(wdev->netdev);
	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (wlan_hdd_validate_context(hdd_ctx))
		return -EINVAL;

	if (adapter->device_mode != QDF_STA_MODE &&
	    adapter->device_mode != QDF_P2P_CLIENT_MODE) {
		hdd_debug("Failed to get TDLS info due to opmode:%d",
			  adapter->device_mode);
		return -EOPNOTSUPP;
	}

	ret = wlan_hdd_tdls_disable(hdd_ctx, adapter);

	return ret;
}

/**
 * wlan_hdd_cfg80211_exttdls_disable() - disable ext tdls
 * @wiphy:   pointer to wireless wiphy structure.
 * @wdev:    pointer to wireless_dev structure.
 * @data:    Pointer to the data to be passed via vendor interface
 * @data_len:Length of the data to be passed
 *
 * Return:   Return the Success or Failure code.
 */
int wlan_hdd_cfg80211_exttdls_disable(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data,
					int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_exttdls_disable(wiphy, wdev,
						    data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

#ifdef TDLS_MGMT_VERSION5
static int wlan_hdd_get_tdls_link_id(struct hdd_context *hdd_ctx, int id)
{
	return id;
}
#else
static int wlan_hdd_get_tdls_link_id(struct hdd_context *hdd_ctx, int id)
{
	int link_id;

	link_id = cfg_tdls_get_link_id(hdd_ctx->psoc);

	return link_id;
}
#endif

#ifdef TDLS_MGMT_VERSION5
/**
 * __wlan_hdd_cfg80211_tdls_mgmt() - handle management actions on a given peer
 * @wiphy: wiphy
 * @dev: net device
 * @peer: MAC address of the TDLS peer
 * @action_code: action code
 * @dialog_token: dialog token
 * @status_code: status code
 * @peer_capability: peer capability
 * @initiator: tdls initiator flag
 * @buf: additional IE to include
 * @len: length of buf in bytes
 * @link_id: link id for mld device
 *
 * Return: 0 if success; negative errno otherwise
 */
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev, const uint8_t *peer,
				uint8_t action_code, uint8_t dialog_token,
				uint16_t status_code, uint32_t peer_capability,
				bool initiator, const uint8_t *buf,
				size_t len, int link_id)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)) || \
	defined(CFG80211_TDLS_MGMT_LINK_AWARE)
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev,
				const u8 *peer, int link_id,
				u8 action_code, u8 dialog_token,
				u16 status_code, u32 peer_capability,
				bool initiator, const u8 *buf,
				size_t len)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev, const uint8_t *peer,
				uint8_t action_code, uint8_t dialog_token,
				uint16_t status_code, uint32_t peer_capability,
				bool initiator, const uint8_t *buf,
				size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev, const uint8_t *peer,
				uint8_t action_code, uint8_t dialog_token,
				uint16_t status_code, uint32_t peer_capability,
				const uint8_t *buf, size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)) || defined(TDLS_MGMT_VERSION2)
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev, uint8_t *peer,
				uint8_t action_code, uint8_t dialog_token,
				uint16_t status_code, uint32_t peer_capability,
				const uint8_t *buf, size_t len)
#else
static int __wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
				struct net_device *dev, uint8_t *peer,
				uint8_t action_code, uint8_t dialog_token,
				uint16_t status_code, const uint8_t *buf,
				size_t len)
#endif
{
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	bool tdls_support;
#if !defined(TDLS_MGMT_VERSION5) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)) && \
	!defined(CFG80211_TDLS_MGMT_LINK_AWARE)
	int link_id = -1;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
#if !(TDLS_MGMT_VERSION2)
	u32 peer_capability;

	peer_capability = 0;
#endif
#endif

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EINVAL;
	}

	if (wlan_hdd_validate_vdev_id(adapter->deflink->vdev_id))
		return -EINVAL;

	qdf_mtrace(QDF_MODULE_ID_HDD, QDF_MODULE_ID_HDD,
		   TRACE_CODE_HDD_CFG80211_TDLS_MGMT,
		   adapter->deflink->vdev_id, action_code);

	if (wlan_hdd_validate_context(hdd_ctx))
		return -EINVAL;

	cfg_tdls_get_support_enable(hdd_ctx->psoc, &tdls_support);
	if (!tdls_support) {
		hdd_debug("TDLS Disabled in INI OR not enabled in FW. "
			"Cannot process TDLS commands");
		return -ENOTSUPP;
	}

	if (hdd_ctx->tdls_umac_comp_active) {
		int ret;
		bool is_dbs_target = false;
		struct wlan_objmgr_psoc *psoc = hdd_ctx->psoc;
		enum tdls_feature_mode mode;

		if (!psoc) {
			hdd_err("psoc is null");
			return -EINVAL;
		}
		mode = ucfg_tdls_get_current_mode(psoc);
		if (action_code != TDLS_TEARDOWN) {
			if (mode == TDLS_SUPPORT_DISABLED ||
			    mode == TDLS_SUPPORT_SUSPENDED) {
				hdd_debug_rl("TDLS mode is %d. action %d declined.",
					     mode,
					     action_code);
				return -ENOTSUPP;
			}
		}

		link_id = wlan_hdd_get_tdls_link_id(hdd_ctx, link_id);
		is_dbs_target = ucfg_policy_mgr_is_fw_supports_dbs(psoc);

		if (is_dbs_target) {
			ret = wlan_cfg80211_tdls_mgmt_mlo(adapter, peer,
							  action_code,
							  dialog_token,
							  status_code,
							  peer_capability,
							  buf, len, link_id);
		} else {
			ret = wlan_cfg80211_tdls_send_mgmt_on_active_link(
								adapter, peer,
								action_code,
								dialog_token,
								status_code,
								peer_capability,
								buf, len,
								link_id);
		}

		return ret;
	}

	return -EINVAL;
}

#ifdef TDLS_MGMT_VERSION5
/**
 * wlan_hdd_cfg80211_tdls_mgmt() - cfg80211 tdls mgmt handler function
 * @wiphy: Pointer to wiphy structure.
 * @dev: Pointer to net_device structure.
 * @peer: peer address
 * @action_code: action code
 * @dialog_token: dialog token
 * @status_code: status code
 * @peer_capability: peer capability
 * @initiator: tdls initiator flag
 * @buf: buffer
 * @len: Length of @buf
 * @link_id: link id for mld device
 *
 * This is the cfg80211 tdls mgmt handler function which invokes
 * the internal function @__wlan_hdd_cfg80211_tdls_mgmt with
 * SSR protection.
 *
 * Return: 0 for success, error number on failure.
 */
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					const u8 *peer, u8 action_code,
					u8 dialog_token, u16 status_code,
					u32 peer_capability, bool initiator,
					const u8 *buf, size_t len, int link_id)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)) || \
	defined(CFG80211_TDLS_MGMT_LINK_AWARE)
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					const u8 *peer, int link_id,
					u8 action_code, u8 dialog_token,
					u16 status_code, u32 peer_capability,
					bool initiator, const u8 *buf,
					size_t len)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)) || defined(WITH_BACKPORTS)
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					const u8 *peer, u8 action_code,
					u8 dialog_token, u16 status_code,
					u32 peer_capability, bool initiator,
					const u8 *buf, size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					const u8 *peer, u8 action_code,
					u8 dialog_token, u16 status_code,
					u32 peer_capability, const u8 *buf,
					size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)) || defined(TDLS_MGMT_VERSION2)
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					u8 *peer, u8 action_code,
					u8 dialog_token,
					u16 status_code, u32 peer_capability,
					const u8 *buf, size_t len)
#else
int wlan_hdd_cfg80211_tdls_mgmt(struct wiphy *wiphy,
					struct net_device *dev,
					u8 *peer, u8 action_code,
					u8 dialog_token,
					u16 status_code, const u8 *buf,
					size_t len)
#endif
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(dev, &vdev_sync);
	if (errno)
		return errno;

#ifdef TDLS_MGMT_VERSION5
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, initiator,
					      buf, len, link_id);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)) || \
	defined(CFG80211_TDLS_MGMT_LINK_AWARE)
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, link_id,
					      action_code, dialog_token,
					      status_code, peer_capability,
					      initiator, buf, len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)) || defined(WITH_BACKPORTS)
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, initiator,
					      buf, len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, buf, len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)) || defined(TDLS_MGMT_VERSION2)
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, buf, len);
#else
	errno = __wlan_hdd_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      buf, len);
#endif

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

static bool
hdd_is_sta_legacy(struct wlan_hdd_link_info *link_info)
{
	struct hdd_station_ctx *sta_ctx;

	sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(link_info);
	if (!sta_ctx)
		return false;

	if ((sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_AUTO) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11N) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11AC) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11N_ONLY) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11AC_ONLY) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11AX) ||
	    (sta_ctx->conn_info.dot11mode == eCSR_CFG_DOT11_MODE_11AX_ONLY))
		return false;

	return true;
}

uint16_t
hdd_get_tdls_connected_peer_count(struct wlan_hdd_link_info *link_info)
{
	struct wlan_objmgr_vdev *vdev;
	uint16_t peer_count;

	vdev = hdd_objmgr_get_vdev_by_user(link_info, WLAN_OSIF_TDLS_ID);
	if (!vdev) {
		hdd_err("Invalid vdev");
		return -EINVAL;
	}

	peer_count = ucfg_get_tdls_conn_peer_count(vdev);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_OSIF_TDLS_ID);

	return peer_count;
}

void
hdd_check_and_set_tdls_conn_params(struct wlan_objmgr_vdev *vdev)
{
	uint8_t vdev_id;
	enum hdd_dot11_mode selfdot11mode;
	struct wlan_hdd_link_info *link_info;
	struct wlan_objmgr_psoc *psoc;
	struct hdd_context *hdd_ctx;

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc)
		return;

	/*
	 * Only need to set this if STA link is in legacy mode
	 */
	vdev_id = wlan_vdev_get_id(vdev);
	link_info = wlan_hdd_get_link_info_from_vdev(psoc, vdev_id);
	if (!link_info || !hdd_is_sta_legacy(link_info))
		return;

	hdd_ctx = WLAN_HDD_GET_CTX(link_info->adapter);
	if (!hdd_ctx)
		return;

	selfdot11mode = hdd_ctx->config->dot11Mode;
	/*
	 * When STA connection is made in legacy mode (11a, 11b and 11g) and
	 * selfdot11Mode is either 11ax, 11ac or 11n, TDLS connection can be
	 * made upto supporting selfdot11mode. Since, TDLS shares same netdev
	 * that of STA, checksum/TSO will be disabled during STA connection.
	 * For better TDLS throughput, enable checksum/TSO which were already
	 * disabled during STA connection.
	 */
	if (selfdot11mode == eHDD_DOT11_MODE_AUTO ||
	    selfdot11mode == eHDD_DOT11_MODE_11ax ||
	    selfdot11mode == eHDD_DOT11_MODE_11ax_ONLY ||
	    selfdot11mode == eHDD_DOT11_MODE_11ac_ONLY ||
	    selfdot11mode == eHDD_DOT11_MODE_11ac ||
	    selfdot11mode == eHDD_DOT11_MODE_11n ||
	    selfdot11mode == eHDD_DOT11_MODE_11n_ONLY)
		hdd_cm_netif_queue_enable(link_info->adapter);
}

void
hdd_check_and_set_tdls_disconn_params(struct wlan_objmgr_vdev *vdev)
{
	uint8_t vdev_id;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_hdd_link_info *link_info;

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc)
		return;

	/*
	 * Only need to set this if STA link is in legacy mode
	 */
	vdev_id = wlan_vdev_get_id(vdev);
	link_info = wlan_hdd_get_link_info_from_vdev(psoc, vdev_id);
	if (!link_info || !hdd_is_sta_legacy(link_info))
		return;

	hdd_cm_netif_queue_enable(link_info->adapter);
}

/**
 * __wlan_hdd_cfg80211_tdls_oper() - helper function to handle cfg80211 operation
 *                                   on an TDLS peer
 * @wiphy: wiphy
 * @dev: net device
 * @peer: MAC address of the TDLS peer
 * @oper: cfg80211 TDLS operation
 *
 * Return: 0 on success; negative errno otherwise
 */
static int __wlan_hdd_cfg80211_tdls_oper(struct wiphy *wiphy,
					 struct net_device *dev,
					 const uint8_t *peer,
					 enum nl80211_tdls_operation oper)
{
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	int status;
	bool tdls_support;
	struct wlan_objmgr_vdev *vdev;

	hdd_enter();

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EINVAL;
	}

	if (wlan_hdd_validate_vdev_id(adapter->deflink->vdev_id))
		return -EINVAL;

	cfg_tdls_get_support_enable(hdd_ctx->psoc, &tdls_support);
	if (!tdls_support) {
		hdd_debug("TDLS Disabled in INI OR not enabled in FW. "
			"Cannot process TDLS commands");
		return -ENOTSUPP;
	}

	qdf_mtrace(QDF_MODULE_ID_HDD, QDF_MODULE_ID_HDD,
		   TRACE_CODE_HDD_CFG80211_TDLS_OPER,
		   adapter->deflink->vdev_id, oper);

	if (!peer) {
		hdd_err("Invalid arguments");
		return -EINVAL;
	}

	status = wlan_hdd_validate_context(hdd_ctx);

	if (0 != status)
		return status;

	if (!hdd_ctx->tdls_umac_comp_active) {
		status = -EINVAL;
		goto exit;
	}

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (!vdev)
		return -EINVAL;

	/*
	 * Allow TDLS teardown & disable link operation when CSA is in progress
	 * or disconnection is in progress this facilitates that the TDLS
	 * wakelock is released in these cases which was acquired during
	 * TDLS setup.
	 */
	if ((oper == NL80211_TDLS_TEARDOWN ||
	     oper == NL80211_TDLS_DISABLE_LINK))
		goto tdls_oper;

	if (!wlan_hdd_is_tdls_allowed(hdd_ctx, vdev)) {
		hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
		return -EINVAL;
	}

tdls_oper:
	status = wlan_cfg80211_tdls_oper(vdev, peer, oper);
	hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);

exit:
	hdd_exit();

	return status;
}

/**
 * wlan_hdd_cfg80211_tdls_oper() - handle cfg80211 operation on an TDLS peer
 * @wiphy: wiphy
 * @dev: net device
 * @peer: MAC address of the TDLS peer
 * @oper: cfg80211 TDLS operation
 *
 * Return: 0 on success; negative errno otherwise
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
int wlan_hdd_cfg80211_tdls_oper(struct wiphy *wiphy,
				struct net_device *dev,
				const uint8_t *peer,
				enum nl80211_tdls_operation oper)
#else
int wlan_hdd_cfg80211_tdls_oper(struct wiphy *wiphy,
				struct net_device *dev,
				uint8_t *peer,
				enum nl80211_tdls_operation oper)
#endif
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(dev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_tdls_oper(wiphy, dev, peer, oper);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

int hdd_set_tdls_offchannel(struct hdd_context *hdd_ctx,
			    struct hdd_adapter *adapter,
			    int offchannel)
{
	struct wlan_objmgr_vdev *vdev;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;

	if (!hdd_ctx->tdls_umac_comp_active)
		return qdf_status_to_os_return(status);

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (vdev) {
		status = ucfg_set_tdls_offchannel(vdev, offchannel);
		hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
	}
	return qdf_status_to_os_return(status);
}

int hdd_set_tdls_secoffchanneloffset(struct hdd_context *hdd_ctx,
				     struct hdd_adapter *adapter,
				     int offchanoffset)
{
	struct wlan_objmgr_vdev *vdev;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;

	if (!hdd_ctx->tdls_umac_comp_active)
		return qdf_status_to_os_return(status);

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (vdev) {
		status = ucfg_set_tdls_secoffchanneloffset(vdev, offchanoffset);
		hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
	}
	return qdf_status_to_os_return(status);
}

int hdd_set_tdls_offchannelmode(struct hdd_context *hdd_ctx,
				struct hdd_adapter *adapter,
				int offchanmode)
{
	struct wlan_objmgr_vdev *vdev;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;
	bool tdls_off_ch;

	if (cfg_tdls_get_off_channel_enable(
		hdd_ctx->psoc, &tdls_off_ch) !=
	    QDF_STATUS_SUCCESS) {
		hdd_err("cfg get tdls off ch failed");
		return qdf_status_to_os_return(status);
	}
	if (!tdls_off_ch) {
		hdd_debug("tdls off ch is false, do nothing");
		return qdf_status_to_os_return(status);
	}

	if (!hdd_ctx->tdls_umac_comp_active)
		return qdf_status_to_os_return(status);

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_OSIF_TDLS_ID);
	if (vdev) {
		status = ucfg_set_tdls_offchan_mode(vdev, offchanmode);
		hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
	}
	return qdf_status_to_os_return(status);
}

/**
 * hdd_set_tdls_scan_type - set scan during active tdls session
 * @hdd_ctx: ptr to hdd context.
 * @val: scan type value: 0 or 1.
 *
 * Set scan type during tdls session. If set to 1, that means driver
 * shall maintain tdls link and allow scan regardless if tdls peer is
 * buffer sta capable or not and/or if device is sleep sta capable or
 * not. If tdls peer is not buffer sta capable then during scan there
 * will be loss of Rx packets and Tx would stop when device moves away
 * from tdls channel. If set to 0, then driver shall teardown tdls link
 * before initiating scan if peer is not buffer sta capable and device
 * is not sleep sta capable. By default, scan type is set to 0.
 *
 * Return: success (0) or failure (errno value)
 */
int hdd_set_tdls_scan_type(struct hdd_context *hdd_ctx, int val)
{
	if ((val != 0) && (val != 1)) {
		hdd_err("Incorrect value of tdls scan type: %d", val);
		return -EINVAL;
	}

	cfg_tdls_set_scan_enable(hdd_ctx->psoc, (bool)val);

	return 0;
}

int wlan_hdd_tdls_antenna_switch(struct wlan_hdd_link_info *link_info,
				 uint32_t mode)
{
	int ret;
	struct wlan_objmgr_vdev *vdev;

	if (!link_info->adapter->hdd_ctx->tdls_umac_comp_active)
		return 0;

	vdev = hdd_objmgr_get_vdev_by_user(link_info, WLAN_OSIF_TDLS_ID);
	if (!vdev)
		return -EINVAL;

	ret = wlan_tdls_antenna_switch(vdev, mode);
	hdd_objmgr_put_vdev_by_user(vdev, WLAN_OSIF_TDLS_ID);
	return ret;
}

QDF_STATUS hdd_tdls_register_peer(void *userdata, uint32_t vdev_id,
				  const uint8_t *mac, uint8_t qos)
{
	struct hdd_context *hddctx;
	struct wlan_hdd_link_info *link_info;

	hddctx = userdata;
	if (!hddctx) {
		hdd_err("Invalid hddctx");
		return QDF_STATUS_E_INVAL;
	}

	link_info = hdd_get_link_info_by_vdev(hddctx, vdev_id);
	if (!link_info) {
		hdd_err("Invalid vdev");
		return QDF_STATUS_E_FAILURE;
	}

	return hdd_roam_register_tdlssta(link_info->adapter, mac, qos);
}

void hdd_init_tdls_config(struct tdls_start_params *tdls_cfg)
{
	tdls_cfg->tdls_send_mgmt_req = eWNI_SME_TDLS_SEND_MGMT_REQ;
	tdls_cfg->tdls_add_sta_req = eWNI_SME_TDLS_ADD_STA_REQ;
	tdls_cfg->tdls_del_sta_req = eWNI_SME_TDLS_DEL_STA_REQ;
	tdls_cfg->tdls_update_peer_state = WMA_UPDATE_TDLS_PEER_STATE;
	tdls_cfg->tdls_update_offchan_mode = WMA_UPDATE_TDLS_OFF_CHAN;
}

#ifdef FEATURE_TDLS_STATS_VENDOR_EVENTS
/**
 * tdls_stats_reason_to_qca() - Map internal TDLS stats reason code to
 *                              QCA vendor reason code.
 * @reason_code: Internal reason code (enum tdls_stats_reason_code).
 * @qca_reason:  Output QCA vendor reason code
 *               (enum qca_wlan_tdls_stats_reason_code).
 *
 * Return: true if the reason code has a valid QCA mapping and @qca_reason
 *         has been set; false if the reason code is TDLS_STATS_REASON_GENERAL
 *         or TDLS_STATS_REASON_UNKNOWN which have no corresponding value in
 *         enum qca_wlan_tdls_stats_reason_code and the REASON_CODE attribute
 *         should be omitted.
 */
static bool tdls_stats_reason_to_qca(uint8_t reason_code, uint8_t *qca_reason)
{
	switch (reason_code) {
	case TDLS_STATS_REASON_PEER_UNREACHABLE:
		*qca_reason = QCA_WLAN_TDLS_STATS_REASON_CODE_PEER_UNREACHABLE;
		return true;
	case TDLS_STATS_REASON_TEARDOWN_UNSPECIFIED:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_TEARDOWN_UNSPECIFIED;
		return true;
	case TDLS_STATS_REASON_INSUFFICIENT_TRAFFIC:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_INSUFFICIENT_TRAFFIC;
		return true;
	case TDLS_STATS_REASON_NO_TRAFFIC:
		*qca_reason = QCA_WLAN_TDLS_STATS_REASON_CODE_NO_TRAFFIC;
		return true;
	case TDLS_STATS_REASON_ROAMED:
		*qca_reason = QCA_WLAN_TDLS_STATS_REASON_CODE_ROAMED;
		return true;
	case TDLS_STATS_REASON_CONC_SAME_BAND:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_CONCURRENT_OP_SAME_BAND;
		return true;
	case TDLS_STATS_REASON_CONC_DIFF_BAND:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_CONCURRENT_OP_DIFF_BAND;
		return true;
	case TDLS_STATS_REASON_BT_COEX:
		*qca_reason = QCA_WLAN_TDLS_STATS_REASON_CODE_BT_COEX;
		return true;
	case TDLS_STATS_REASON_BSS_CHANNEL_SWITCH:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_BSS_CHANNEL_SWITCH;
		return true;
	case TDLS_STATS_REASON_DEAUTH_LEAVING:
		*qca_reason =
			QCA_WLAN_TDLS_STATS_REASON_CODE_DEAUTHENTICATED_LEAVING;
		return true;
	default:
		/* TDLS_STATS_REASON_GENERAL and TDLS_STATS_REASON_UNKNOWN
		 * have no QCA equivalent
		 */
		return false;
	}
}

/**
 * tdls_stats_role_to_qca() - Map internal TDLS stats is_sender flag to
 *                            QCA vendor role.
 * @is_sender: Internal sender flag (1 = local STA is the initiator).
 * @qca_role:  Output QCA vendor role (enum qca_wlan_tdls_stats_role).
 *
 * Return: true if the role has a valid QCA mapping and @qca_role has been
 *         set; false otherwise.
 */
static bool tdls_stats_role_to_qca(uint8_t is_sender, uint8_t *qca_role)
{
	if (is_sender) {
		*qca_role = QCA_WLAN_TDLS_STATS_ROLE_SENDER;
		return true;
	}

	*qca_role = QCA_WLAN_TDLS_STATS_ROLE_RECEIVER;
	return true;
}

/**
 * tdls_stats_type_to_qca() - Map internal TDLS stats type to
 *                            QCA vendor type.
 * @type:     Internal type (enum tdls_stats_type).
 * @qca_type: Output QCA vendor type (enum qca_wlan_tdls_stats_type).
 *
 * Return: true if the type has a valid QCA mapping and @qca_type has been
 *         set; false if the type is unknown and the TYPE attribute should
 *         be omitted.
 */
static bool tdls_stats_type_to_qca(uint8_t type, uint8_t *qca_type)
{
	switch (type) {
	case TDLS_STATS_IF_SETUP:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_IFACE_SETUP;
		return true;
	case TDLS_STATS_DISCOVERY:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_DISCOVERY;
		return true;
	case TDLS_STATS_SETUP:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_SETUP;
		return true;
	case TDLS_STATS_TEARDOWN:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_TEARDOWN;
		return true;
	case TDLS_STATS_STATE_CHANGED:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_CHANNEL_CHANGE;
		return true;
	case TDLS_STATS_DATA:
		*qca_type = QCA_WLAN_TDLS_STATS_TYPE_DATA;
		return true;
	default:
		return false;
	}
}

/**
 * tdls_stats_subtype_to_qca() - Map internal TDLS stats subtype to
 *                               QCA vendor subtype.
 * @subtype:     Internal subtype (enum tdls_stats_subtype).
 * @qca_subtype: Output QCA vendor subtype (enum qca_wlan_tdls_stats_subtype).
 *
 * Return: true if the subtype has a valid QCA mapping and @qca_subtype has
 *         been set; false if the subtype is TDLS_STATS_SUBTYPE_GENERAL which
 *         has no corresponding value in enum qca_wlan_tdls_stats_subtype and
 *         the SUBTYPE attribute should be omitted.
 */
static bool tdls_stats_subtype_to_qca(uint8_t subtype, uint8_t *qca_subtype)
{
	switch (subtype) {
	case TDLS_STATS_SUBTYPE_REQ:
		*qca_subtype = QCA_WLAN_TDLS_STATS_SUBTYPE_REQUEST;
		return true;
	case TDLS_STATS_SUBTYPE_RESP:
		*qca_subtype = QCA_WLAN_TDLS_STATS_SUBTYPE_RESPONSE;
		return true;
	case TDLS_STATS_SUBTYPE_CONFIRM:
		*qca_subtype = QCA_WLAN_TDLS_STATS_SUBTYPE_CONFIRM;
		return true;
	case TDLS_STATS_SUBTYPE_COMPLETE:
		*qca_subtype = QCA_WLAN_TDLS_STATS_SUBTYPE_COMPLETE;
		return true;
	default:
		/* TDLS_STATS_SUBTYPE_GENERAL has no QCA equivalent */
		return false;
	}
}

/**
 * hdd_tdls_stats_entry_skb_len() - Calculate the skb size needed to emit
 *                                  one TDLS stats entry as a vendor event.
 *
 * Return: Conservative upper-bound byte count for the vendor event skb.
 */
static uint32_t hdd_tdls_stats_entry_skb_len(void)
{
	return NLMSG_HDRLEN +
		nla_total_size(0) +                 /* ENTRIES nested */
		nla_total_size(0) +                 /* entry nested */
		nla_total_size(QDF_MAC_ADDR_SIZE) + /* DUT_MAC_ADDR */
		nla_total_size(QDF_MAC_ADDR_SIZE) + /* PEER_MAC_ADDR */
		nla_total_size(sizeof(uint8_t)) +   /* TYPE */
		nla_total_size(sizeof(uint8_t)) +   /* SUBTYPE */
		nla_total_size(0) +                 /* SUCCESS flag */
		nla_total_size(sizeof(uint8_t)) +   /* ROLE */
		nla_total_size(sizeof(uint32_t)) +  /* DATA_RATE */
		nla_total_size(sizeof(uint32_t)) +  /* TX_PACKETS */
		nla_total_size(sizeof(uint32_t)) +  /* TX_FAILURES */
		nla_total_size(sizeof(uint32_t)) +  /* RX_PACKETS */
		nla_total_size(sizeof(uint32_t)) +  /* RX_FAILURES */
		nla_total_size(sizeof(uint32_t)) +  /* OP_FREQ */
		nla_total_size(sizeof(uint8_t)) +   /* RSSI */
		nla_total_size(sizeof(uint8_t)) +   /* LINK_ID */
		nla_total_size(sizeof(uint64_t)) +  /* TIMESTAMP */
		nla_total_size(sizeof(uint8_t)) +   /* REASON_CODE */
		nla_total_size(0) +                 /* MCS_PKT_COUNT nested */
		TDLS_STATS_MAX_MCS_COUNTERS *
		(nla_total_size(0) +                /* per-MCS nested */
		 nla_total_size(sizeof(uint8_t)) +  /* MCS_INDEX */
		 nla_total_size(sizeof(uint64_t)) + /* TX_PACKET_COUNT */
		 nla_total_size(sizeof(uint64_t))); /* RX_PACKET_COUNT */
}

/**
 * hdd_tdls_stats_emit_cb() - OS-IF callback that emits a TDLS stats entry
 *                            as a QCA vendor event.
 * @psoc: Psoc context
 * @entry:   Stats entry to emit.
 *
 * This is the concrete implementation of the tdls_stats_emit_cb callback
 * type.  It is registered with the TDLS component via tdls_start_params
 * in hdd_update_tdls_config() so that the TDLS stats state machine can
 * forward entries to user space without any direct dependency on HDD
 * headers.
 *
 * Attribute layout emitted via QCA_NL80211_VENDOR_SUBCMD_TDLS_STATS:
 * QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRIES (nested)
 * [0] (nested entry)
 * DUT_MAC_ADDR, PEER_MAC_ADDR, TYPE, SUBTYPE, SUCCESS (flag),
 * ROLE, DATA_RATE, TX_PACKETS, TX_FAILURES, RX_PACKETS,
 * RX_FAILURES, OP_FREQ, RSSI, LINK_ID (MLO only), TIMESTAMP,
 * REASON_CODE (teardown/failure/channel-switch only),
 * MCS_PKT_COUNT (nested, Type-5 only)
 * [i] MCS_INDEX, TX_PACKET_COUNT, RX_PACKET_COUNT
 */
void hdd_tdls_stats_emit_cb(struct wlan_objmgr_psoc *psoc,
			    const struct tdls_stats_entry *entry)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *adapter;
	struct wlan_objmgr_vdev *link_vdev;
	struct sk_buff *skb;
	struct nlattr *entries_attr, *entry_attr, *mcs_attr, *mcs_entry;
	uint32_t i;
	uint32_t skb_len;
	uint8_t link_id = WLAN_INVALID_LINK_ID;
	uint8_t qca_type;
	uint8_t qca_subtype;
	uint8_t qca_role;
	uint8_t qca_reason;
	bool is_mlo = false;
	struct qdf_mac_addr dut_mac_addr;
	int attr;

	hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);

	if (!hdd_ctx || !entry)
		return;

	adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (!adapter)
		adapter = hdd_get_adapter(hdd_ctx, QDF_P2P_CLIENT_MODE);
	if (!adapter) {
		hdd_err("TDLS stats: no STA/P2P-client adapter found");
		return;
	}

	/*
	 * Determine the DUT MAC address to report:
	 *   - MLO session  : use the MLD address of the vdev
	 *   - non-MLO session: use the self (link) MAC address of the vdev
	 * Also capture link_id for MLO sessions so we can emit it later
	 * without a second vdev look-up.
	 */
	link_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
							 entry->session_id,
							 WLAN_OSIF_ID);
	if (link_vdev) {
		is_mlo = wlan_vdev_mlme_is_mlo_vdev(link_vdev);
		if (is_mlo) {
			qdf_mem_copy(dut_mac_addr.bytes,
				     wlan_vdev_mlme_get_mldaddr(link_vdev),
				     QDF_MAC_ADDR_SIZE);
			link_id = wlan_vdev_get_link_id(link_vdev);
		} else {
			qdf_mem_copy(dut_mac_addr.bytes,
				     wlan_vdev_mlme_get_macaddr(link_vdev),
				     QDF_MAC_ADDR_SIZE);
		}
		wlan_objmgr_vdev_release_ref(link_vdev, WLAN_OSIF_ID);
	} else {
		/* Fallback: vdev not found, use deflink address */
		qdf_mem_copy(dut_mac_addr.bytes,
			     adapter->deflink->link_addr.bytes,
			     QDF_MAC_ADDR_SIZE);
	}

	skb_len = hdd_tdls_stats_entry_skb_len();

	hdd_debug("TDLS stats emit: dut=" QDF_MAC_ADDR_FMT " peer=" QDF_MAC_ADDR_FMT " type=%u subtype=%u success=%u is_sender=%u data_rate=%u(x100Kbps) ch=%u rssi=%d link_id=%u ts_ms=%llu tx_pkts=%u tx_fail=%u rx_pkts=%u rx_fail=%u",
		  QDF_MAC_ADDR_REF(dut_mac_addr.bytes),
		  QDF_MAC_ADDR_REF(entry->peer_mac),
		  entry->type, entry->subtype,
		  entry->success, entry->is_sender,
		  (uint32_t)entry->data_rate * 5,
		  (uint32_t)entry->channel, (int8_t)entry->rssi, link_id,
		  entry->ts_ms,
		  entry->tx_ppdus_cumulative, entry->tx_ppdu_failures,
		  entry->rx_ppdus_cumulative, entry->rx_ppdu_failures);

	skb = wlan_cfg80211_vendor_event_alloc(
				hdd_ctx->wiphy,
				&adapter->wdev,
				skb_len,
				QCA_NL80211_VENDOR_SUBCMD_TDLS_STATS_INDEX,
				qdf_mem_malloc_flags());
	if (!skb) {
		hdd_err("TDLS stats: failed to alloc vendor event skb");
		return;
	}

	/* Wrap everything in ENTRIES nested attribute */
	entries_attr = nla_nest_start(skb,
				      QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRIES);
	if (!entries_attr)
		goto fail;

	/* Single entry nested attribute (index 0) */
	entry_attr = nla_nest_start(skb, 0);
	if (!entry_attr)
		goto fail;

	/*
	 * DUT MAC address:
	 *   MLO     -> MLD address
	 *   non-MLO -> self (link) MAC address
	 */
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_DUT_MAC_ADDR;
	if (nla_put(skb, attr, QDF_MAC_ADDR_SIZE, dut_mac_addr.bytes))
		goto fail;

	/* Peer MAC address */
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_PEER_MAC_ADDR;
	if (nla_put(skb, attr, QDF_MAC_ADDR_SIZE, entry->peer_mac))
		goto fail;

	/*
	 * Event type — Mandatory; convert internal type to QCA vendor type.
	 * TDLS_STATS_STATE_CHANGED maps to
	 * QCA_WLAN_TDLS_STATS_TYPE_CHANNEL_CHANGE.
	 */
	if (!tdls_stats_type_to_qca(entry->type, &qca_type))
		goto fail;
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_TYPE;
	if (nla_put_u8(skb, attr, qca_type))
		goto fail;

	/*
	 * Event subtype — Optional; only emit when the internal subtype maps
	 * to a valid QCA subtype.  TDLS_STATS_SUBTYPE_GENERAL has no
	 * corresponding value in enum qca_wlan_tdls_stats_subtype and is
	 * therefore skipped.
	 */
	if (tdls_stats_subtype_to_qca(entry->subtype, &qca_subtype)) {
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_SUBTYPE;
		if (nla_put_u8(skb, attr, qca_subtype))
			goto fail;
	}

	/*
	 * Success flag: present when success == 0 (success),
	 * absent when success == 1 (failure).
	 */
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_SUCCESS;
	if (entry->success == 0) {
		if (nla_put_flag(skb, attr))
			goto fail;
	}

	/*
	 * Sender/receiver role — Optional; not applicable for Type-5 (DATA)
	 * entries where is_sender is set to 0 and has no meaning.
	 */
	if (entry->type != TDLS_STATS_DATA) {
		if (tdls_stats_role_to_qca(entry->is_sender, &qca_role)) {
			attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_ROLE;
			if (nla_put_u8(skb, attr, qca_role))
				goto fail;
		}
	}

	/*
	 * Type-5 (DATA) periodic stats — Optional; only applicable for DATA
	 * entries.  All fields are set to 0 for non-DATA entries.
	 *
	 * Data rate: entry stores 0.5 Mbps units; vendor attr expects
	 * 100 Kbps units.  Multiply by 5 (0.5 Mbps = 500 Kbps = 5 × 100 Kbps).
	 */
	if (entry->type == TDLS_STATS_DATA) {
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_DATA_RATE;
		if (nla_put_u32(skb, attr, (uint32_t)entry->data_rate * 5))
			goto fail;

		/* TX PPDUs (cumulative) */
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_TX_PACKETS;
		if (nla_put_u32(skb, attr, entry->tx_ppdus_cumulative))
			goto fail;

		/* TX PPDU failures */
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_TX_FAILURES;
		if (nla_put_u32(skb, attr, entry->tx_ppdu_failures))
			goto fail;

		/* RX PPDUs (cumulative) */
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_RX_PACKETS;
		if (nla_put_u32(skb, attr, entry->rx_ppdus_cumulative))
			goto fail;

		/* RX PPDU failures */
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_RX_FAILURES;
		if (nla_put_u32(skb, attr, entry->rx_ppdu_failures))
			goto fail;
	}

	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_OP_FREQ;
	if (nla_put_u32(skb, attr, entry->channel))
		goto fail;

	/*
	 * RSSI — Optional; -128 is reserved to indicate RSSI not available.
	 * Skip the attribute when RSSI is not available.
	 * Cast to int8_t to clamp to s8 range, then reinterpret as uint8_t
	 * for nla_put_u8 (the kernel NLA layer treats the byte as-is).
	 */
	if (entry->rssi != -128) {
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_RSSI;
		if (nla_put_u8(skb, attr, (uint8_t)(int8_t)entry->rssi))
			goto fail;
	}

	/*
	 * Reason code — applicable for teardown, failure, and channel switch
	 * events.  Optional for all other event types.  Skip if the internal
	 * reason code has no QCA equivalent (GENERAL or UNKNOWN).
	 */
	if (entry->type == TDLS_STATS_TEARDOWN ||
	    entry->type == TDLS_STATS_STATE_CHANGED ||
	    entry->success != 0) {
		if (tdls_stats_reason_to_qca(entry->reason_code, &qca_reason)) {
			attr =
			QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_REASON_CODE;
			if (nla_put_u8(skb, attr, qca_reason))
				goto fail;
		}
	}

	/* MLO link ID — cached from the early vdev lookup above */
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_LINK_ID;
	if (nla_put_u8(skb, attr, link_id))
		goto fail;

	/*
	 * Timestamp: entry stores milliseconds since boot; vendor attr
	 * expects microseconds.  Multiply by 1000.
	 */
	attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_TIMESTAMP;
	if (nla_put_u64_64bit(skb, attr,
			      entry->ts_ms * 1000ULL,
			      QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_INVALID))
		goto fail;

	/*
	 * MCS histogram — only meaningful for Type-5 (DATA) entries.
	 * Emit only MCS indices that have non-zero TX or RX counts to
	 * keep the event compact.
	 */
	if (entry->type == TDLS_STATS_DATA) {
		attr = QCA_WLAN_VENDOR_ATTR_TDLS_STATS_ENTRY_MCS_PKT_COUNT;
		mcs_attr = nla_nest_start(skb, attr);
		if (!mcs_attr)
			goto fail;

		for (i = 0; i < TDLS_STATS_MAX_MCS_COUNTERS; i++) {
			if (!entry->tx_mcs_data_ppdu[i] &&
			    !entry->rx_mcs_data_ppdu[i])
				continue;

			hdd_debug("TDLS stats MCS[%u]: tx=%u rx=%u",
				  i, entry->tx_mcs_data_ppdu[i],
				  entry->rx_mcs_data_ppdu[i]);

			mcs_entry = nla_nest_start(skb, i);
			if (!mcs_entry)
				goto fail;

			if (nla_put_u8(
				skb,
				QCA_WLAN_VENDOR_ATTR_MCS_PKT_MCS_INDEX,
				(uint8_t)i))
				goto fail;

			if (nla_put_u64_64bit(
				skb,
				QCA_WLAN_VENDOR_ATTR_MCS_PKT_TX_PACKET_COUNT,
				entry->tx_mcs_data_ppdu[i],
				QCA_WLAN_VENDOR_ATTR_MCS_PKT_PAD))
				goto fail;

			if (nla_put_u64_64bit(
				skb,
				QCA_WLAN_VENDOR_ATTR_MCS_PKT_RX_PACKET_COUNT,
				entry->rx_mcs_data_ppdu[i],
				QCA_WLAN_VENDOR_ATTR_MCS_PKT_PAD))
				goto fail;

			nla_nest_end(skb, mcs_entry);
		}

		nla_nest_end(skb, mcs_attr);
	}

	nla_nest_end(skb, entry_attr);
	nla_nest_end(skb, entries_attr);

	wlan_cfg80211_vendor_event(skb, qdf_mem_malloc_flags());
	return;

fail:
	hdd_err("TDLS stats: failed to fill vendor event attributes");
	wlan_cfg80211_vendor_free_skb(skb);
}
#endif /* FEATURE_TDLS_STATS_VENDOR_EVENTS */

void hdd_config_tdls_with_band_switch(struct hdd_context *hdd_ctx)
{
	struct wlan_objmgr_vdev *tdls_obj_vdev;
	int offchmode;
	uint32_t current_band;
	bool tdls_off_ch;

	if (!hdd_ctx) {
		hdd_err("Invalid hdd_ctx");
		return;
	}

	if (ucfg_reg_get_band(hdd_ctx->pdev, &current_band) !=
	    QDF_STATUS_SUCCESS) {
		hdd_err("Failed to get current band config");
		return;
	}

	/**
	 * If all bands are supported, in below condition off channel enable
	 * orig is false and nothing is need to do
	 * 1. band switch does not happen.
	 * 2. band switch happens and it already restores
	 * 3. tdls off channel is disabled by default.
	 * If 2g or 5g is not supported. Disable tdls off channel only when
	 * tdls off channel is enabled currently.
	 */
	if ((current_band & BIT(REG_BAND_2G)) &&
	    (current_band & BIT(REG_BAND_5G))) {
		if (cfg_tdls_get_off_channel_enable_orig(
			hdd_ctx->psoc, &tdls_off_ch) !=
		    QDF_STATUS_SUCCESS) {
			hdd_err("cfg get tdls off ch orig failed");
			return;
		}
		if (!tdls_off_ch) {
			hdd_debug("tdls off ch orig is false, do nothing");
			return;
		}
		offchmode = ENABLE_CHANSWITCH;
		cfg_tdls_restore_off_channel_enable(hdd_ctx->psoc);
	} else {
		if (cfg_tdls_get_off_channel_enable(
			hdd_ctx->psoc, &tdls_off_ch) !=
		    QDF_STATUS_SUCCESS) {
			hdd_err("cfg get tdls off ch failed");
			return;
		}
		if (!tdls_off_ch) {
			hdd_debug("tdls off ch is false, do nothing");
			return;
		}
		offchmode = DISABLE_CHANSWITCH;
		cfg_tdls_store_off_channel_enable(hdd_ctx->psoc);
		cfg_tdls_set_off_channel_enable(hdd_ctx->psoc, false);
	}
	tdls_obj_vdev = ucfg_get_tdls_vdev(hdd_ctx->psoc, WLAN_TDLS_NB_ID);
	if (tdls_obj_vdev) {
		ucfg_set_tdls_offchan_mode(tdls_obj_vdev, offchmode);
		wlan_objmgr_vdev_release_ref(tdls_obj_vdev, WLAN_TDLS_NB_ID);
	}
}
