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
 * DOC: wlan_hdd_wifi_pos_pasn.c
 *
 * WLAN Host Device Driver WIFI POSITION PASN authentication APIs implementation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wireless.h>
#include "osif_sync.h"
#include <wlan_hdd_includes.h>
#include <net/cfg80211.h>
#include "qdf_trace.h"
#include "qdf_types.h"
#include "wlan_hdd_wifi_pos_pasn.h"
#include "os_if_wifi_pos.h"
#include "wifi_pos_pasn_api.h"
#include "wifi_pos_ucfg_i.h"
#include "wlan_crypto_global_api.h"
#include "wlan_nl_to_crypto_params.h"
#include "wlan_mlo_mgr_sta.h"
#include "wifi_pos_api.h"
#include "wifi_pos_utils_i.h"
#include "wmi_unified_param.h"
#include "wlan_cfg80211.h"
#include "wlan_objmgr_vdev_obj.h"
#include "wma.h"
#include "wlan_p2p_api.h"
#include "wlan_policy_mgr_api.h"
#include "wlan_mlme_ucfg_api.h"
#include "scheduler_api.h"

const struct nla_policy
wifi_pos_pasn_auth_status_policy[QCA_WLAN_VENDOR_ATTR_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_PASN_ACTION] = {.type = NLA_U32},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEERS] = {.type = NLA_NESTED},
};

const struct nla_policy
wifi_pos_pasn_auth_policy[QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_SRC_ADDR] = VENDOR_NLA_POLICY_MAC_ADDR,
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAC_ADDR] = VENDOR_NLA_POLICY_MAC_ADDR,
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_STATUS_SUCCESS] = {.type = NLA_FLAG},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_LTF_KEYSEED_REQUIRED] = {
							.type = NLA_FLAG},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_COMEBACK_AFTER] = {.type = NLA_U16},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_COOKIE] = {.type = NLA_BINARY,
					.len = WLAN_PASN_MAX_COOKIE_LEN},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_AKM] = {.type = NLA_U32},
	[QCA_WLAN_VENDOR_ATTR_PASN_PEER_CIPHER] = {.type = NLA_U32},
};

const struct nla_policy
wifi_pos_pasn_set_ranging_ctx_policy[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_ACTION] = {
					.type = NLA_U32},
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SRC_ADDR] =
					VENDOR_NLA_POLICY_MAC_ADDR,
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR] =
					VENDOR_NLA_POLICY_MAC_ADDR,
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SHA_TYPE] = {
					.type = NLA_U32},
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_TK] = {
					.type = NLA_BINARY, .len = MAX_PMK_LEN},
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_CIPHER] = {
					.type = NLA_U32},
	[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_LTF_KEYSEED] = {
					.type = NLA_BINARY, .len = MAX_PMK_LEN},
};

static void
wlan_hdd_fill_comeback_params(struct wlan_pasn_auth_status_peer_info
			      *auth_status, struct nlattr *tb2[])
{
	enum qca_wlan_vendor_attr_pasn_peer comeback_after =
		QCA_WLAN_VENDOR_ATTR_PASN_PEER_COMEBACK_AFTER;
	enum qca_wlan_vendor_attr_pasn_peer peer_cookie =
			QCA_WLAN_VENDOR_ATTR_PASN_PEER_COOKIE;
	auth_status->status = WLAN_PASN_AUTH_STATUS_PEER_COMEBACK;

	if (!tb2[comeback_after] || !tb2[peer_cookie]) {
		hdd_debug("%s is not present", !tb2[comeback_after] ?
			  "comeback_after" : "peer_cookie");
		auth_status->status = WLAN_PASN_AUTH_STATUS_PASN_FAILED;
		return;
	}

	auth_status->comeback_after = nla_get_u16(
		tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_COMEBACK_AFTER]);
	auth_status->cookie_len =
		nla_len(tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_COOKIE]);
	nla_memcpy(&auth_status->cookie,
		   tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_COOKIE],
		   auth_status->cookie_len);
	hdd_debug("comeback_after:%d cookie:%s cookie_len:%d",
		  auth_status->comeback_after, auth_status->cookie,
		  auth_status->cookie_len);
}

static int
wlan_hdd_cfg80211_send_pasn_auth_status(struct wiphy *wiphy,
					struct net_device *dev,
					const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct wlan_pasn_auth_status *pasn_data;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_MAX + 1];
	struct nlattr *tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAX + 1];
	struct nlattr *curr_attr;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	bool is_pasn_success = false;
	int ret, i = 0, rem;
	uint32_t akm = 0;

	if (hdd_get_conparam() == QDF_GLOBAL_FTM_MODE) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (wlan_hdd_validate_vdev_id(adapter->deflink->vdev_id))
		return -EINVAL;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (wlan_cfg80211_nla_parse(tb, QCA_WLAN_VENDOR_ATTR_MAX,
				    data, data_len,
				    wifi_pos_pasn_auth_status_policy)) {
		hdd_err_rl("Invalid PASN auth status attributes");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_PASN_PEERS]) {
		hdd_err_rl("No PASN peer");
		return -EINVAL;
	}

	pasn_data = qdf_mem_malloc(sizeof(*pasn_data));
	if (!pasn_data)
		return -ENOMEM;

	pasn_data->vdev_id = adapter->deflink->vdev_id;
	nla_for_each_nested(curr_attr, tb[QCA_WLAN_VENDOR_ATTR_PASN_PEERS],
			    rem) {
		if (wlan_cfg80211_nla_parse_nested(
			tb2, QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAX, curr_attr,
			wifi_pos_pasn_auth_policy)) {
			hdd_err_rl("nla_parse failed");
			qdf_mem_free(pasn_data);
			return -EINVAL;
		}

		is_pasn_success = nla_get_flag(
			tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_STATUS_SUCCESS]);
		if (!is_pasn_success)
			wlan_hdd_fill_comeback_params(
					&pasn_data->auth_status[i], tb2);

		hdd_debug("PASN auth status:%d",
			  pasn_data->auth_status[i].status);

		if (tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAC_ADDR]) {
			nla_memcpy(pasn_data->auth_status[i].peer_mac.bytes,
				   tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_MAC_ADDR],
				   QDF_MAC_ADDR_SIZE);
			hdd_debug("Peer mac[%d]: " QDF_MAC_ADDR_FMT, i,
				  QDF_MAC_ADDR_REF(
				  pasn_data->auth_status[i].peer_mac.bytes));
		}

		if (tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_SRC_ADDR]) {
			nla_memcpy(pasn_data->auth_status[i].self_mac.bytes,
				   tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_SRC_ADDR],
				   QDF_MAC_ADDR_SIZE);
			hdd_debug("Src addr[%d]: " QDF_MAC_ADDR_FMT, i,
				  QDF_MAC_ADDR_REF(
				  pasn_data->auth_status[i].self_mac.bytes));
		}

		if (tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_AKM]) {
			akm =
			nla_get_u32(tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_AKM]);
			pasn_data->auth_status[i].akm =
						osif_nl_to_crypto_akm_type(akm);
			hdd_debug("akm:0x%x ", pasn_data->auth_status[i].akm);
		}

		if (tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_CIPHER]) {
			pasn_data->auth_status[i].cipher =
			nla_get_u32(tb2[QCA_WLAN_VENDOR_ATTR_PASN_PEER_CIPHER]);
			hdd_debug("cipher:0x%x ",
				  pasn_data->auth_status[i].cipher);
		}

		i++;
		pasn_data->num_peers++;
		if (pasn_data->num_peers >= WLAN_MAX_11AZ_PEERS) {
			hdd_err_rl("Invalid num_peers:%d",
				   pasn_data->num_peers);
			qdf_mem_free(pasn_data);
			return -EINVAL;
		}
	}

	status = wifi_pos_send_pasn_auth_status(hdd_ctx->psoc, pasn_data);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Send pasn auth status failed");

	qdf_mem_free(pasn_data);
	ret = qdf_status_to_os_return(status);

	return ret;
}

int wlan_hdd_wifi_pos_send_pasn_auth_status(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data, int data_len)
{
	struct osif_vdev_sync *vdev_sync;
	int errno;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_cfg80211_send_pasn_auth_status(wiphy, wdev->netdev,
							data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

#define WLAN_PASN_AUTH_KEY_INDEX 0

static int wlan_cfg80211_set_pasn_key(struct hdd_adapter *adapter,
				      struct nlattr **tb)
{
	struct wlan_crypto_key *crypto_key;
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_objmgr_peer *peer;
	struct wlan_objmgr_psoc *psoc =
			adapter->hdd_ctx->psoc;
	struct qdf_mac_addr peer_mac = {0};
	struct wlan_pasn_auth_status *pasn_status;
	bool is_ltf_keyseed_required;
	QDF_STATUS status;
	int ret = 0;
	int cipher_len;
	uint32_t cipher;
	uint8_t vdev_id;

	crypto_key = qdf_mem_malloc(sizeof(*crypto_key));
	if (!crypto_key)
		return -ENOMEM;

	if (!tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_CIPHER]) {
		qdf_mem_free(crypto_key);
		return -EINVAL;
	}

	cipher = nla_get_u32(
		tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_CIPHER]);
	crypto_key->cipher_type = osif_nl_to_crypto_cipher_type(cipher);

	cipher_len = osif_nl_to_crypto_cipher_len(cipher);
	crypto_key->keylen =
		nla_len(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_TK]);
	if (cipher_len < 0 || crypto_key->keylen < cipher_len ||
	    (crypto_key->keylen >
	     (WLAN_CRYPTO_KEYBUF_SIZE + WLAN_CRYPTO_MICBUF_SIZE))) {
		hdd_err_rl("Invalid key length %d", crypto_key->keylen);
		qdf_mem_free(crypto_key);
		return -EINVAL;
	}

	crypto_key->keyix = WLAN_PASN_AUTH_KEY_INDEX;
	qdf_mem_copy(&crypto_key->keyval[0],
		     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_TK]),
		     crypto_key->keylen);

	if (!tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]) {
		hdd_err_rl("BSSID is not present");
		qdf_mem_free(crypto_key);
		return -EINVAL;
	}

	qdf_mem_copy(crypto_key->macaddr,
		     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]),
		     QDF_MAC_ADDR_SIZE);
	qdf_mem_copy(peer_mac.bytes,
		     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]),
		     QDF_MAC_ADDR_SIZE);

	peer = wlan_objmgr_get_peer_by_mac(psoc, peer_mac.bytes,
					   WLAN_WIFI_POS_CORE_ID);
	if (!peer) {
		hdd_err("PASN peer is not found");
		qdf_mem_free(crypto_key);
		return -EFAULT;
	}

	vdev = wlan_peer_get_vdev(peer);
	if (!vdev) {
		hdd_err("Vdev is NULL for PASN peer");
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_CORE_ID);
		qdf_mem_free(crypto_key);
		return -EINVAL;
	}

	status = wlan_objmgr_vdev_try_get_ref(vdev, WLAN_WIFI_POS_CORE_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to get vdev reference");
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_CORE_ID);
		qdf_mem_free(crypto_key);
		return -EFAULT;
	}

	vdev_id = wlan_vdev_get_id(vdev);
	hdd_debug("PASN unicast key opmode %d, key_len %d, vdev_id %d",
		  vdev->vdev_mlme.vdev_opmode,
		  crypto_key->keylen,
		  vdev_id);

	status = ucfg_crypto_set_key_req(vdev, crypto_key,
					 WLAN_CRYPTO_KEY_TYPE_UNICAST);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("PASN set_key failed");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_WIFI_POS_CORE_ID);
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_CORE_ID);
		qdf_mem_free(crypto_key);
		return -EFAULT;
	}
	qdf_mem_free(crypto_key);

	/*
	 * If LTF key seed is not required for the peer, then update
	 * the source mac address for that peer by sending PASN auth
	 * status command.
	 * If LTF keyseed is required, then PASN Auth status command
	 * will be sent after LTF keyseed command.
	 */
	is_ltf_keyseed_required =
			ucfg_wifi_pos_is_ltf_keyseed_required_for_peer(peer);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_WIFI_POS_CORE_ID);
	wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_CORE_ID);
	if (is_ltf_keyseed_required)
		return 0;

	pasn_status = qdf_mem_malloc(sizeof(*pasn_status));
	if (!pasn_status)
		return -ENOMEM;

	pasn_status->vdev_id = vdev_id;
	pasn_status->num_peers = 1;

	qdf_mem_copy(pasn_status->auth_status[0].peer_mac.bytes,
		     peer_mac.bytes, QDF_MAC_ADDR_SIZE);

	if (tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SRC_ADDR])
		qdf_mem_copy(pasn_status->auth_status[0].self_mac.bytes,
			     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SRC_ADDR]),
			     QDF_MAC_ADDR_SIZE);

	status = wifi_pos_send_pasn_auth_status(psoc, pasn_status);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Send PASN auth status failed");

	ret = qdf_status_to_os_return(status);

	qdf_mem_free(pasn_status);

	return ret;
}

#define MLO_ALL_VDEV_LINK_ID -1

#ifdef WLAN_FEATURE_11BE_MLO
static QDF_STATUS
wlan_hdd_cfg80211_send_set_ltf_keyseed_mlo_vdev(struct hdd_context *hdd_ctx,
						struct wlan_objmgr_vdev *vdev,
						struct hdd_adapter *adapter,
						struct wlan_crypto_ltf_keyseed_data *data,
						int link_id)
{
	struct wlan_objmgr_vdev *link_vdev;
	struct wlan_objmgr_peer *peer;
	uint16_t link, vdev_count = 0;
	struct qdf_mac_addr peer_link_mac;
	struct qdf_mac_addr original_mac;
	struct wlan_objmgr_vdev *wlan_vdev_list[WLAN_UMAC_MLO_MAX_VDEVS] = {0};
	QDF_STATUS status;
	uint8_t vdev_id;
	struct wlan_hdd_link_info *link_info;

	if (!wlan_vdev_mlme_is_mlo_vdev(vdev))
		return QDF_STATUS_SUCCESS;

	qdf_copy_macaddr(&peer_link_mac, &data->peer_mac_addr);
	qdf_copy_macaddr(&original_mac, &data->peer_mac_addr);
	mlo_sta_get_vdev_list(vdev, &vdev_count, wlan_vdev_list);

	for (link = 0; link < vdev_count; link++) {
		link_vdev = wlan_vdev_list[link];
		vdev_id = wlan_vdev_get_id(link_vdev);

		link_info = hdd_get_link_info_by_vdev(hdd_ctx, vdev_id);
		if (!link_info) {
			mlo_release_vdev_ref(link_vdev);
			continue;
		}

		peer = NULL;
		switch (adapter->device_mode) {
		case QDF_SAP_MODE:
			if (wlan_vdev_mlme_is_mlo_vdev(link_vdev))
				peer = wlan_hdd_ml_sap_get_peer(
						link_vdev,
						peer_link_mac.bytes);
			break;
		case QDF_STA_MODE:
		default:
			peer = wlan_objmgr_vdev_try_get_bsspeer(link_vdev,
								WLAN_OSIF_ID);
			break;
		}

		if (peer) {
			qdf_mem_copy(peer_link_mac.bytes,
				     wlan_peer_get_macaddr(peer),
				     QDF_MAC_ADDR_SIZE);
			wlan_objmgr_peer_release_ref(peer, WLAN_OSIF_ID);

		} else if (wlan_vdev_mlme_is_mlo_link_vdev(link_vdev) &&
			   adapter->device_mode == QDF_STA_MODE) {
			status = wlan_hdd_mlo_copy_partner_addr_from_mlie(
						link_vdev, &peer_link_mac);
			if (QDF_IS_STATUS_ERROR(status)) {
				hdd_err("Failed to get peer address from ML IEs");
				mlo_release_vdev_ref(link_vdev);
				continue;
			}
		} else {
			hdd_err("Peer is null");
			mlo_release_vdev_ref(link_vdev);
			continue;
		}

		qdf_copy_macaddr(&data->peer_mac_addr, &peer_link_mac);
		data->vdev_id = wlan_vdev_get_id(link_vdev);
		hdd_debug("vdev:%d Peer_mac: " QDF_MAC_ADDR_FMT " key_seed_len:%d",
			  data->vdev_id,
			  QDF_MAC_ADDR_REF(data->peer_mac_addr.bytes),
			  data->key_seed_len);
		status = wlan_crypto_set_ltf_keyseed(hdd_ctx->psoc, data);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_err("Set LTF Keyseed failed vdev:%d for peer: "
				QDF_MAC_ADDR_FMT, data->vdev_id,
				QDF_MAC_ADDR_REF(data->peer_mac_addr.bytes));
			mlo_release_vdev_ref(link_vdev);
			continue;
		}

		mlo_release_vdev_ref(link_vdev);
	}
	qdf_copy_macaddr(&data->peer_mac_addr, &original_mac);

	return QDF_STATUS_SUCCESS;
}
#else
static inline QDF_STATUS
wlan_hdd_cfg80211_send_set_ltf_keyseed_mlo_vdev(struct hdd_context *hdd_ctx,
						struct wlan_objmgr_vdev *vdev,
						struct hdd_adapter *adapter,
						struct wlan_crypto_ltf_keyseed_data *data,
						int link_id)
{
	return QDF_STATUS_SUCCESS;
}
#endif

static int
wlan_hdd_cfg80211_send_set_ltf_keyseed(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct nlattr **tb)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct wlan_pasn_auth_status *pasn_auth_status;
	struct wlan_objmgr_peer *peer;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_crypto_ltf_keyseed_data *data;
	struct qdf_mac_addr bss_peer_addr = {0};
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	bool is_ltf_keyseed_required, is_keyseed_for_assoc_bss = false;
	enum wlan_peer_type peer_type;
	int ret;

	hdd_enter_dev(dev);
	if (hdd_get_conparam() == QDF_GLOBAL_FTM_MODE) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (wlan_hdd_validate_vdev_id(adapter->deflink->vdev_id))
		return -EINVAL;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	data = qdf_mem_malloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	if (!tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]) {
		hdd_err_rl("BSSID is not present");
		ret = -EINVAL;
		goto err;
	}

	qdf_mem_copy(data->peer_mac_addr.bytes,
		     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]),
		     QDF_MAC_ADDR_SIZE);

	if (tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SRC_ADDR])
		qdf_mem_copy(data->src_mac_addr.bytes,
			     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_SRC_ADDR]),
			     QDF_MAC_ADDR_SIZE);

	peer = wlan_objmgr_get_peer_by_mac(hdd_ctx->psoc,
					   data->peer_mac_addr.bytes,
					   WLAN_WIFI_POS_OSIF_ID);
	if (!peer) {
		hdd_debug("PASN peer is not found");
		ret = 0;
		goto err;
	}

	vdev = wlan_peer_get_vdev(peer);
	if (!vdev) {
		hdd_err_rl("Vdev is NULL for PASN peer");
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_OSIF_ID);
		ret = -EINVAL;
		goto err;
	}

	status = wlan_objmgr_vdev_try_get_ref(vdev, WLAN_WIFI_POS_OSIF_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err_rl("Failed to get vdev reference");
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_OSIF_ID);
		ret = -EFAULT;
		goto err;
	}

	data->vdev_id = wlan_vdev_get_id(vdev);
	wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_OSIF_ID);

	data->key_seed_len =
		nla_len(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_LTF_KEYSEED]);
	if (!data->key_seed_len ||
	    data->key_seed_len < WLAN_MIN_SECURE_LTF_KEYSEED_LEN ||
	    data->key_seed_len > WLAN_MAX_SECURE_LTF_KEYSEED_LEN) {
		wlan_objmgr_vdev_release_ref(vdev, WLAN_WIFI_POS_OSIF_ID);
		hdd_err_rl("Invalid key seed length:%d", data->key_seed_len);
		ret = -EINVAL;
		goto err;
	}

	qdf_mem_copy(data->key_seed,
		     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_LTF_KEYSEED]),
		     data->key_seed_len);

	/*
	 * For MLO vdev send set LTF keyseed command on each link for the
	 * associated BSS link peer addresses similar to install key command
	 */
	status = wlan_vdev_get_bss_peer_mac(vdev, &bss_peer_addr);
	if (QDF_IS_STATUS_SUCCESS(status) &&
	    qdf_is_macaddr_equal(&bss_peer_addr, &data->peer_mac_addr))
		is_keyseed_for_assoc_bss = true;

	if (wlan_vdev_mlme_is_mlo_vdev(vdev) && is_keyseed_for_assoc_bss)
		status = wlan_hdd_cfg80211_send_set_ltf_keyseed_mlo_vdev(
						hdd_ctx, vdev, adapter,
						data, MLO_ALL_VDEV_LINK_ID);
	else
		status = wlan_crypto_set_ltf_keyseed(hdd_ctx->psoc, data);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_WIFI_POS_OSIF_ID);

	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Set LTF Keyseed failed vdev_id:%d", data->vdev_id);
		ret = qdf_status_to_os_return(status);
		goto err;
	}

	peer = wlan_objmgr_get_peer_by_mac(hdd_ctx->psoc,
					   data->peer_mac_addr.bytes,
					   WLAN_WIFI_POS_OSIF_ID);
	if (!peer) {
		/*
		 * Auth status need not be sent for the BSS PASN
		 * peer. So, return if peer is not found
		 */
		hdd_err_rl("PASN peer is not found after LTF keyseed");
		ret = 0;
		goto err;
	}

	/*
	 * PASN auth status command need not be sent for associated peer.
	 * It should be sent only for PASN peer type.
	 */
	peer_type = wlan_peer_get_peer_type(peer);
	if (peer_type != WLAN_PEER_RTT_PASN) {
		wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_OSIF_ID);
		ret = 0;
		goto err;
	}

	/*
	 * If LTF key seed is not required for the peer, then update
	 * the source mac address for that peer by sending PASN auth
	 * status command.
	 * If LTF keyseed is required, then PASN Auth status command
	 * will be sent after LTF keyseed command.
	 */
	is_ltf_keyseed_required =
			ucfg_wifi_pos_is_ltf_keyseed_required_for_peer(peer);
	wlan_objmgr_peer_release_ref(peer, WLAN_WIFI_POS_OSIF_ID);

	if (!is_ltf_keyseed_required) {
		ret = 0;
		goto err;
	}

	/*
	 * Send PASN Auth status followed by SET LTF keyseed command to
	 * set the peer as authorized at firmware and firmware will start
	 * ranging after this.
	 */
	pasn_auth_status = qdf_mem_malloc(sizeof(*pasn_auth_status));
	if (!pasn_auth_status) {
		ret = -ENOMEM;
		goto err;
	}

	pasn_auth_status->vdev_id = data->vdev_id;
	pasn_auth_status->num_peers = 1;
	qdf_mem_copy(pasn_auth_status->auth_status[0].peer_mac.bytes,
		     data->peer_mac_addr.bytes, QDF_MAC_ADDR_SIZE);
	qdf_mem_copy(pasn_auth_status->auth_status[0].self_mac.bytes,
		     data->src_mac_addr.bytes, QDF_MAC_ADDR_SIZE);

	hdd_debug("vdev:%d Send pasn auth status", pasn_auth_status->vdev_id);
	status = wifi_pos_send_pasn_auth_status(hdd_ctx->psoc,
						pasn_auth_status);
	qdf_mem_free(pasn_auth_status);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Send PASN auth status failed");

	ret = qdf_status_to_os_return(status);
err:
	qdf_mem_free(data);
	hdd_exit();

	return ret;
}

static int
__wlan_hdd_cfg80211_set_secure_ranging_context(struct wiphy *wiphy,
					       struct wireless_dev *wdev,
					       const void *data, int data_len)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(wdev->netdev);
	int errno = 0;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_MAX + 1];
	struct qdf_mac_addr peer_mac;

	hdd_enter();

	if (wlan_cfg80211_nla_parse(tb,
				    QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_MAX,
				    data, data_len,
				    wifi_pos_pasn_set_ranging_ctx_policy)) {
		hdd_err_rl("Invalid PASN auth status attributes");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_ACTION]) {
		hdd_err_rl("Action attribute is missing");
		return -EINVAL;
	}

	if (nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_ACTION]) ==
			QCA_WLAN_VENDOR_SECURE_RANGING_CTX_ACTION_ADD) {
		if (tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_TK] &&
		    nla_len(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_TK])) {
			hdd_debug("Sec ranging CTX TK");
			errno = wlan_cfg80211_set_pasn_key(adapter, tb);
			if (errno)
				return errno;
		}

		if (tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_LTF_KEYSEED] &&
		    nla_len(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_LTF_KEYSEED])) {
			hdd_debug("Set LTF keyseed");
			errno = wlan_hdd_cfg80211_send_set_ltf_keyseed(wiphy,
								       wdev->netdev, tb);
			if (errno)
				return errno;
		}
	} else if (nla_get_u32(
			tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_ACTION]) ==
			QCA_WLAN_VENDOR_SECURE_RANGING_CTX_ACTION_DELETE) {
		if (!tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]) {
			hdd_err_rl("Peer mac address attribute is missing");
			return -EINVAL;
		}

		qdf_mem_copy(peer_mac.bytes,
			     nla_data(tb[QCA_WLAN_VENDOR_ATTR_SECURE_RANGING_CTX_PEER_MAC_ADDR]),
			     QDF_MAC_ADDR_SIZE);
		hdd_debug("Delete PASN peer" QDF_MAC_ADDR_FMT,
			  QDF_MAC_ADDR_REF(peer_mac.bytes));
		wifi_pos_send_pasn_peer_deauth(hdd_ctx->psoc, &peer_mac);
	}

	hdd_exit();

	return errno;
}

int
wlan_hdd_cfg80211_set_secure_ranging_context(struct wiphy *wiphy,
					     struct wireless_dev *wdev,
					     const void *data, int data_len)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;

	errno = osif_vdev_sync_op_start(wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_set_secure_ranging_context(wiphy,
							       wdev,
							       data, data_len);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

#if defined(WLAN_FEATURE_RTT_11AZ_SUPPORT) && defined(CFG80211_PD_SUPPORT)
static enum mlme_dot11_mode
hdd_pmsr_preamble_to_dot11_mode(enum nl80211_preamble preamble)
{
	switch (preamble) {
	case NL80211_PREAMBLE_HT:
		return MLME_DOT11_MODE_11N;
	case NL80211_PREAMBLE_VHT:
		return MLME_DOT11_MODE_11AC;
	case NL80211_PREAMBLE_HE:
		return MLME_DOT11_MODE_11AX;
	default:
		return MLME_DOT11_MODE_11N;
	}
}

/**
 * hdd_pmsr_preamble_to_wmi() - Map NL80211 preamble to WMI preamble
 * @preamble: NL80211 preamble value
 *
 * Return: WMI_HOST_RATE_PREAMBLE value
 */
static u32 hdd_pmsr_preamble_to_wmi(enum nl80211_preamble preamble)
{
	switch (preamble) {
	case NL80211_PREAMBLE_HT:
		return WMI_HOST_RATE_PREAMBLE_HT;
	case NL80211_PREAMBLE_VHT:
		return WMI_HOST_RATE_PREAMBLE_VHT;
	case NL80211_PREAMBLE_HE:
		return WMI_HOST_RATE_PREAMBLE_HE;
	default:
		return WMI_HOST_RATE_PREAMBLE_OFDM;
	}
}

/**
 * struct wifi_pos_rtt_meas_req_msg - scheduler message body for posting
 * WMI_RTT_PEER_MEAS_REQ_CMDID to the scheduler thread
 * @psoc: Pointer to PSOC object
 * @params: RTT peer measurement request params, owned by this message and
 * freed by wlan_hdd_flush_rtt_meas_req_msg()
 */
struct wifi_pos_rtt_meas_req_msg {
	struct wlan_objmgr_psoc *psoc;
	struct wmi_rtt_peer_meas_req_cmd_params *params;
};

/**
 * wlan_hdd_flush_rtt_meas_req_msg() - Free the RTT peer meas req msg
 * @msg: Pointer to the scheduler message
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
wlan_hdd_flush_rtt_meas_req_msg(struct scheduler_msg *msg)
{
	struct wifi_pos_rtt_meas_req_msg *req;

	if (!msg || !msg->bodyptr) {
		hdd_err("RTT meas req msg is NULL");
		return QDF_STATUS_E_INVAL;
	}

	req = msg->bodyptr;
	qdf_mem_free(req->params->peers);
	qdf_mem_free(req->params);
	qdf_mem_free(req);

	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_hdd_process_rtt_meas_req_msg() - Scheduler thread callback to send
 * WMI_RTT_PEER_MEAS_REQ_CMDID to firmware
 * @msg: Pointer to the scheduler message
 *
 * Runs in scheduler thread context so this send is ordered against other
 * OS_IF-queue commands (e.g. the FW MAC-filter clear) posted ahead of it.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
wlan_hdd_process_rtt_meas_req_msg(struct scheduler_msg *msg)
{
	struct wifi_pos_rtt_meas_req_msg *req;
	QDF_STATUS status;

	if (!msg || !msg->bodyptr) {
		hdd_err("RTT meas req msg is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	req = msg->bodyptr;
	status = wifi_pos_send_rtt_peer_meas_req(req->psoc, req->params);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Failed to send RTT peer meas req: %d", status);

	wlan_hdd_flush_rtt_meas_req_msg(msg);

	return status;
}

/* Sentinel cookie for PMSR's hold on the shared P2P random-mac entry.
 * Distinct from idr-allocated ROC/tx cookies, which start at
 * QDF_IDR_START (0x100).
 */
#define WIFI_POS_PMSR_RAND_MAC_COOKIE 0xFFFFFFFF00000001ULL

/**
 * wlan_hdd_pmsr_release_rand_mac() - release PMSR's protected random mac
 *  cookie, if held
 * @psoc: psoc object
 * @pd_adapter: PD adapter holding the pmsr_req state
 *
 * Return: void
 */
static void wlan_hdd_pmsr_release_rand_mac(struct wlan_objmgr_psoc *psoc,
					   struct hdd_adapter *pd_adapter)
{
	if (!pd_adapter->pmsr_req.rand_mac_registered)
		return;

	wlan_p2p_del_random_mac(psoc, pd_adapter->pmsr_req.vdev_id,
				WIFI_POS_PMSR_RAND_MAC_COOKIE);
	pd_adapter->pmsr_req.rand_mac_registered = false;
}

/**
 * __wlan_hdd_cfg80211_start_pmsr() - Start peer measurement request (inner)
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Builds WMI_RTT_PEER_MEAS_REQ_CMDID and posts it to the scheduler thread.
 *
 * Return: 0 on success, negative errno on failure
 */
static int __wlan_hdd_cfg80211_start_pmsr(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  struct cfg80211_pmsr_request *req)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	struct hdd_adapter *pd_adapter = NULL;
	struct wmi_rtt_peer_meas_req_cmd_params *params;
	struct wifi_pos_rtt_meas_req_msg *rtt_msg;
	struct scheduler_msg msg = {0};
	struct wlan_objmgr_vdev *vdev;
	u32 n_peers = req->n_peers;
	u32 i;
	int ret;
	QDF_STATUS status;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!adapter->deflink)
		return -EINVAL;

	if (policy_mgr_get_connection_count_with_mlo(hdd_ctx->psoc) > 1) {
		wifi_pos_err("PMSR not allowed when concurrency exists");
		return -EAGAIN;
	}

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(hdd_ctx->psoc,
						    adapter->deflink->vdev_id,
						    WLAN_WIFI_POS_OSIF_ID);
	if (vdev) {
		if (ucfg_mlme_is_chan_switch_in_progress(vdev)) {
			wifi_pos_err("channel switch is in progress");
			wlan_objmgr_vdev_release_ref(
				vdev, WLAN_WIFI_POS_OSIF_ID);
			return -EAGAIN;
		}
		wlan_objmgr_vdev_release_ref(vdev,
					     WLAN_WIFI_POS_OSIF_ID);
	}

	if (!n_peers)
		return -EINVAL;

	pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
	if (!pd_adapter)
		return -EINVAL;

	pd_adapter->pmsr_req.cookie = req->cookie;
	pd_adapter->pmsr_req.vdev_id = adapter->deflink->vdev_id;
	pd_adapter->pmsr_req.nl_port_id = req->nl_portid;
	pd_adapter->pmsr_req.req_id = (u32)(req->cookie & 0xFFFFFFFF);

	params = qdf_mem_malloc(sizeof(*params));
	if (!params) {
		qdf_mem_zero(&pd_adapter->pmsr_req,
			     sizeof(pd_adapter->pmsr_req));
		return -ENOMEM;
	}

	params->peers = qdf_mem_malloc(n_peers * sizeof(*params->peers));
	if (!params->peers) {
		qdf_mem_free(params);
		qdf_mem_zero(&pd_adapter->pmsr_req,
			     sizeof(pd_adapter->pmsr_req));
		return -ENOMEM;
	}

	params->req_id = (u32)(req->cookie & 0xFFFFFFFF);
	params->vdev_id = adapter->deflink->vdev_id;
	params->timeout = 0; /* no timeout */
	params->n_peers = n_peers;

	if (!qdf_is_macaddr_zero((struct qdf_mac_addr *)req->mac_addr_mask)) {
		u8 r_mac[QDF_MAC_ADDR_SIZE];

		params->mac_addr_randomization = true;

		qdf_get_random_bytes(r_mac, QDF_MAC_ADDR_SIZE);
		r_mac[0] = (r_mac[0] & 0xfe) | 0x02;

		for (i = 0; i < QDF_MAC_ADDR_SIZE; i++) {
			params->random_mac_addr[i] =
				(req->mac_addr[i] & req->mac_addr_mask[i]) |
				(r_mac[i] & ~req->mac_addr_mask[i]);
		}
	} else {
		params->mac_addr_randomization = false;
	}

	wifi_pos_debug("req_id:%d vdev:%d timeout:%d n_peers:%d randomization:%d",
		       params->req_id, params->vdev_id, params->timeout,
		       params->n_peers, params->mac_addr_randomization);

	for (i = 0; i < n_peers; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];
		struct wmi_rtt_peer_meas_req_peer_params *p = &params->peers[i];
		const struct cfg80211_pmsr_ftm_request_peer *ftm = &peer->ftm;
		enum phy_ch_width ch_width;
		enum mlme_dot11_mode dot11_mode;

		qdf_mem_copy(p->dest_mac, peer->addr, QDF_MAC_ADDR_SIZE);

		/* Channel parameters */
		if (!peer->chandef.chan) {
			wifi_pos_err("NULL chan for peer %d", i);
			ret = -EINVAL;
			goto free_params;
		}
		p->ch_freq = peer->chandef.chan->center_freq;
		p->ch_freq_seg1 = peer->chandef.center_freq1;
		p->ch_freq_seg2 = peer->chandef.center_freq2;
		ch_width = wlan_cfg80211_get_phy_ch_width(peer->chandef.width);
		p->ch_width = ch_width;
		dot11_mode = hdd_pmsr_preamble_to_dot11_mode(ftm->preamble);
		p->ch_phymode =
			wma_chan_phy_mode(peer->chandef.chan->center_freq,
					  ch_width, dot11_mode);

		p->preamble = hdd_pmsr_preamble_to_wmi(ftm->preamble);
		p->burst_period = ftm->burst_period;
		p->min_time_between_measurements =
			ftm->min_time_between_measurements;
		p->max_time_between_measurements =
			ftm->max_time_between_measurements;

		p->report_ap_tsf = 0;
		p->pd_request =
			(ftm->request_type == NL80211_PMSR_FTM_REQ_TYPE_PD);

		p->ftm_requested = ftm->requested;
		p->asap_mode = ftm->asap;
		p->lci_req = ftm->request_lci;
		p->loc_civic_req = ftm->request_civicloc;
		p->tb_ranging = ftm->trigger_based;
		p->ntb_ranging = ftm->non_trigger_based;
		p->i2r_lmr_feedback = ftm->lmr_feedback;
		p->rsta_role = ftm->rsta;

		p->num_burst_exp = ftm->num_bursts_exp;
		p->burst_duration = ftm->burst_duration;
		p->ftms_per_burst = ftm->ftms_per_burst;
		p->ftmr_retries = ftm->ftmr_retries;

		p->nominal_time = ftm->nominal_time;
		p->measurements_per_aw = ftm->ftms_per_burst;
		p->aw_duration = ftm->availability_window;
		p->suppress_range_results = ftm->pd_suppress_range_results;

		wifi_pos_debug("freq:%d cfreq1:%d cfreq2:%d ch_width:%d dot11_mode:%d phy_mode:%d preamble:%d burst_period:%d min_time:%d max_time:%d, report_tsf:%d pd_req:%d",
			       p->ch_freq, p->ch_freq_seg1, p->ch_freq_seg2,
			       ch_width, dot11_mode, p->ch_phymode,
			       p->preamble, p->burst_period,
			       p->min_time_between_measurements,
			       p->max_time_between_measurements,
			       p->report_ap_tsf, p->pd_request);
		wifi_pos_debug("ftm_requested:%d asap_mode:%d lci_req:%d loc_civic_req:%d tb_ranging:%d ntb:%d i2r_lmr:%d rsta_role:%d num_burst:%d burst_duration:%d ftms_per_burst:%d ftmr_retries:%d",
			       p->ftm_requested, p->asap_mode, p->lci_req,
			       p->loc_civic_req, p->tb_ranging, p->ntb_ranging,
			       p->i2r_lmr_feedback, p->rsta_role,
			       p->num_burst_exp, p->burst_duration,
			       p->ftms_per_burst, p->ftmr_retries);
		wifi_pos_debug("nominal_time:%d meas_per_aw:%d aw_dur:%d suppress_results:%d",
			       p->nominal_time, p->measurements_per_aw,
			       p->aw_duration, p->suppress_range_results);
	}

	if (params->mac_addr_randomization) {
		status = wlan_p2p_add_protected_random_mac(
				hdd_ctx->psoc, adapter->deflink->vdev_id,
				params->random_mac_addr,
				params->peers[0].ch_freq,
				WIFI_POS_PMSR_RAND_MAC_COOKIE);
		if (QDF_IS_STATUS_SUCCESS(status) ||
		    status == QDF_STATUS_E_EXISTS)
			pd_adapter->pmsr_req.rand_mac_registered = true;
		else
			wifi_pos_err("Failed to protect PMSR random mac: %d",
				     status);
	}

	rtt_msg = qdf_mem_malloc(sizeof(*rtt_msg));
	if (!rtt_msg) {
		qdf_mem_free(params->peers);
		qdf_mem_free(params);
		wlan_hdd_pmsr_release_rand_mac(hdd_ctx->psoc, pd_adapter);
		qdf_mem_zero(&pd_adapter->pmsr_req,
			     sizeof(pd_adapter->pmsr_req));
		return -ENOMEM;
	}

	rtt_msg->psoc = hdd_ctx->psoc;
	rtt_msg->params = params;

	msg.bodyptr = rtt_msg;
	msg.callback = wlan_hdd_process_rtt_meas_req_msg;
	msg.flush_callback = wlan_hdd_flush_rtt_meas_req_msg;

	status = scheduler_post_message(QDF_MODULE_ID_HDD, QDF_MODULE_ID_HDD,
					QDF_MODULE_ID_OS_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to post RTT peer meas req msg: %d", status);
		wlan_hdd_flush_rtt_meas_req_msg(&msg);
		wlan_hdd_pmsr_release_rand_mac(hdd_ctx->psoc, pd_adapter);
		qdf_mem_zero(&pd_adapter->pmsr_req,
			     sizeof(pd_adapter->pmsr_req));
		return qdf_status_to_os_return(status);
	}

	/* Mark valid only after the request was accepted for posting */
	pd_adapter->pmsr_req.is_valid = true;

	return 0;

free_params:
	qdf_mem_free(params->peers);
	qdf_mem_free(params);
	qdf_mem_zero(&pd_adapter->pmsr_req, sizeof(pd_adapter->pmsr_req));

	return ret;
}

/**
 * wlan_hdd_cfg80211_start_pmsr() - Start peer measurement request
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Return: 0 on success, negative errno on failure
 */
int wlan_hdd_cfg80211_start_pmsr(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 struct cfg80211_pmsr_request *req)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *sta_adapter;
	struct wireless_dev *sta_wdev = wdev;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return -EINVAL;
	}

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (!sta_adapter || !sta_adapter->wdev.netdev) {
		hdd_err("No Sta adapter");
		return -EINVAL;
	}

	sta_wdev = &sta_adapter->wdev;

	errno = osif_vdev_sync_op_start(sta_wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_start_pmsr(wiphy, sta_wdev, req);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * struct wifi_pos_rtt_meas_cancel_msg - scheduler message body for posting
 * WMI_RTT_PEER_MEAS_CANCEL_CMDID to the scheduler thread
 * @psoc: Pointer to PSOC object; a ref is held on this psoc until the
 * message is processed or flushed
 * @req_id: Request id of the pmsr request to cancel
 */
struct wifi_pos_rtt_meas_cancel_msg {
	struct wlan_objmgr_psoc *psoc;
	uint32_t req_id;
};

/**
 * wlan_hdd_flush_rtt_meas_cancel_msg() - Free the RTT peer meas cancel msg
 * @msg: Pointer to the scheduler message
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
wlan_hdd_flush_rtt_meas_cancel_msg(struct scheduler_msg *msg)
{
	struct wifi_pos_rtt_meas_cancel_msg *req;

	if (!msg || !msg->bodyptr) {
		hdd_err("RTT meas cancel msg is NULL");
		return QDF_STATUS_E_INVAL;
	}

	req = msg->bodyptr;
	wlan_objmgr_psoc_release_ref(req->psoc, WLAN_WIFI_POS_OSIF_ID);
	qdf_mem_free(req);

	return QDF_STATUS_SUCCESS;
}

/**
 * wlan_hdd_process_rtt_meas_cancel_msg() - Scheduler thread callback to send
 * WMI_RTT_PEER_MEAS_CANCEL_CMDID to firmware
 * @msg: Pointer to the scheduler message
 *
 * Runs in scheduler thread context so this send is ordered against other
 * OS_IF-queue commands posted ahead of it.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
wlan_hdd_process_rtt_meas_cancel_msg(struct scheduler_msg *msg)
{
	struct wifi_pos_rtt_meas_cancel_msg *req;
	QDF_STATUS status;

	if (!msg || !msg->bodyptr) {
		hdd_err("RTT meas cancel msg is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	req = msg->bodyptr;
	status = wifi_pos_send_rtt_peer_meas_cancel(req->psoc, req->req_id);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Failed to send RTT cancel cmd: %d", status);

	wlan_hdd_flush_rtt_meas_cancel_msg(msg);

	return status;
}

/**
 * __wlan_hdd_cfg80211_abort_pmsr() - Abort peer measurement request (inner)
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Posts WMI_RTT_PEER_MEAS_CANCEL_CMDID to the scheduler thread so it is
 * ordered against other OS_IF-queue commands (e.g. p2p random MAC filter
 * clear) instead of racing them from the calling thread, and frees the
 * active pmsr_req.
 */
static void __wlan_hdd_cfg80211_abort_pmsr(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   struct cfg80211_pmsr_request *req)
{
	struct wlan_objmgr_psoc *psoc = wifi_pos_get_psoc();
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *pd_adapter;
	struct wifi_pos_rtt_meas_cancel_msg *cancel_msg;
	struct scheduler_msg msg = {0};
	QDF_STATUS status;

	if (!psoc) {
		wifi_pos_err("null psoc");
		return;
	}

	if (hdd_ctx) {
		pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
		if (pd_adapter) {
			if (pd_adapter->pmsr_req.is_valid) {
				wlan_hdd_pmsr_release_rand_mac(psoc,
							       pd_adapter);
				qdf_mem_zero(&pd_adapter->pmsr_req,
					     sizeof(pd_adapter->pmsr_req));
			}
		} else {
			hdd_err("No PD adapter");
		}
	} else {
		hdd_err("hdd_ctx is NULL");
	}

	wlan_objmgr_psoc_get_ref(psoc, WLAN_WIFI_POS_OSIF_ID);

	cancel_msg = qdf_mem_malloc(sizeof(*cancel_msg));
	if (!cancel_msg) {
		wlan_objmgr_psoc_release_ref(psoc, WLAN_WIFI_POS_OSIF_ID);
		return;
	}

	cancel_msg->psoc = psoc;
	cancel_msg->req_id = (u32)(req->cookie & 0xFFFFFFFF);

	msg.bodyptr = cancel_msg;
	msg.callback = wlan_hdd_process_rtt_meas_cancel_msg;
	msg.flush_callback = wlan_hdd_flush_rtt_meas_cancel_msg;

	status = scheduler_post_message(QDF_MODULE_ID_HDD, QDF_MODULE_ID_HDD,
					QDF_MODULE_ID_OS_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to post RTT peer meas cancel msg: %d", status);
		wlan_hdd_flush_rtt_meas_cancel_msg(&msg);
	}
}

/**
 * wlan_hdd_cfg80211_abort_pmsr() - Abort peer measurement request
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 */
void wlan_hdd_cfg80211_abort_pmsr(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  struct cfg80211_pmsr_request *req)
{
	struct osif_vdev_sync *vdev_sync;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *sta_adapter;
	struct wireless_dev *sta_wdev = wdev;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return;
	}

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (!sta_adapter || !sta_adapter->wdev.netdev) {
		hdd_err("No Sta adapter");
		return;
	}

	sta_wdev = &sta_adapter->wdev;
	if (osif_vdev_sync_op_start(sta_wdev->netdev, &vdev_sync))
		return;

	__wlan_hdd_cfg80211_abort_pmsr(wiphy, sta_wdev, req);

	osif_vdev_sync_op_stop(vdev_sync);
}

/**
 * wlan_hdd_wifi_pos_get_pmsr_req() - get PMSR req
 * @wiphy: Pointer to wiphy
 * @req: Pointer to PMSR request to fill
 *
 * Return: wireless_dev pointer on success, NULL on failure
 */
struct wireless_dev *wlan_hdd_wifi_pos_get_pmsr_req(
					struct wiphy *wiphy,
					struct cfg80211_pmsr_request *req)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *pd_adapter;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return NULL;
	}

	pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
	if (!pd_adapter || !pd_adapter->pmsr_req.is_valid) {
		hdd_debug("No active pmsr_req (concurrency teardown or no session)");
		return NULL;
	}

	req->cookie = pd_adapter->pmsr_req.cookie;
	req->nl_portid = pd_adapter->pmsr_req.nl_port_id;

	return &pd_adapter->wdev;
}

/**
 * wlan_hdd_wifi_pos_pmsr_complete() - Wrapper function to complete PMSR
 * @hdd_ctx: Pointer to hdd_context
 *
 * Retrieves the PD adapter, constructs the cfg80211_pmsr_request from its
 * saved state, and calls cfg80211_pmsr_complete() to notify the kernel.
 * Called on SSR (QDF_PD_MODE stop) or when concurrency prevents continuation.
 */
void wlan_hdd_wifi_pos_pmsr_complete(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *pd_adapter;
	struct wlan_objmgr_psoc *psoc;
	struct cfg80211_pmsr_request req = {0};
	u32 req_id;
	QDF_STATUS status;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return;
	}

	pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
	if (!pd_adapter || !pd_adapter->pmsr_req.is_valid)
		return;

	hdd_debug("Completing PMSR req_id:%u cookie:0x%llx vdev:%u",
		  pd_adapter->pmsr_req.req_id,
		  pd_adapter->pmsr_req.cookie,
		  pd_adapter->pmsr_req.vdev_id);

	req.cookie = pd_adapter->pmsr_req.cookie;
	req.nl_portid = pd_adapter->pmsr_req.nl_port_id;
	req_id = pd_adapter->pmsr_req.req_id;

	/* Invalidate the req before completing to avoid re-entry */
	pd_adapter->pmsr_req.is_valid = false;

	psoc = wifi_pos_get_psoc();
	if (!cds_is_driver_recovering()) {
		if (psoc) {
			wlan_objmgr_psoc_get_ref(psoc, WLAN_WIFI_POS_OSIF_ID);
			status = wifi_pos_send_rtt_peer_meas_cancel(
					psoc, req_id);
			if (QDF_IS_STATUS_ERROR(status))
				hdd_err("Failed to send RTT cancel cmd: %d",
					status);
			wlan_objmgr_psoc_release_ref(psoc,
						     WLAN_WIFI_POS_OSIF_ID);
		} else {
			hdd_warn("wifi_pos psoc not available, skipping RTT cancel");
		}
	}

	if (psoc)
		wlan_hdd_pmsr_release_rand_mac(psoc, pd_adapter);

	cfg80211_pmsr_complete(&pd_adapter->wdev, &req, GFP_KERNEL);
	/* Zero out remaining fields; is_valid was already cleared above */
	qdf_mem_zero(&pd_adapter->pmsr_req, sizeof(pd_adapter->pmsr_req));
}

static void wlan_hdd_wifi_pos_pmsr_req_clear(struct wiphy *wiphy)
{
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *pd_adapter;
	struct wlan_objmgr_psoc *psoc;

	if (!hdd_ctx)
		return;

	pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
	if (!pd_adapter)
		return;

	hdd_debug("Clearing PMSR req_id:%u cookie:0x%llx vdev:%u on final result",
		  pd_adapter->pmsr_req.req_id,
		  pd_adapter->pmsr_req.cookie,
		  pd_adapter->pmsr_req.vdev_id);

	psoc = wifi_pos_get_psoc();
	if (psoc)
		wlan_hdd_pmsr_release_rand_mac(psoc, pd_adapter);
	else
		hdd_warn("wifi_pos psoc not available, skipping rand mac release");

	qdf_mem_zero(&pd_adapter->pmsr_req, sizeof(pd_adapter->pmsr_req));
}

static void
wlan_hdd_wifi_pos_pmsr_complete_on_concurrency(struct wlan_objmgr_psoc *psoc)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);

	wlan_hdd_wifi_pos_pmsr_complete(hdd_ctx);
}

static struct osif_wifi_pos_legacy_ops hdd_wifi_pos_legacy_ops = {
	.get_pmsr_req_legacy_cb = wlan_hdd_wifi_pos_get_pmsr_req,
	.pmsr_complete_cb = wlan_hdd_wifi_pos_pmsr_complete_on_concurrency,
	.pmsr_req_clear_cb = wlan_hdd_wifi_pos_pmsr_req_clear,
};

void wlan_hdd_wifi_pos_register_legacy_cb(void)
{
	osif_wifi_pos_set_legacy_cb(&hdd_wifi_pos_legacy_ops);
}

void wlan_hdd_wifi_pos_unregister_legacy_cb(void)
{
	osif_wifi_pos_reset_legacy_cb();
}
#endif

#if defined(CFG80211_PD_SUPPORT) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)
bool wlan_hdd_is_pd_iface(struct wireless_dev *wdev)
{
	return (wdev->iftype == NL80211_IFTYPE_PD);
}
#endif
