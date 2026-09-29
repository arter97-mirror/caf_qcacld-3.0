/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_cqm.c
 *
 * WLAN adaptive-hysteresis RSSI CQM functions.
 *
 * Implements the standard Linux NL80211_CMD_SET_CQM single-threshold
 * configuration on top of the WMI_RSSI_ADAPTIVE_BREACH_MONITOR_CONFIG_CMDID /
 * WMI_RSSI_ADAPTIVE_BREACH_EVENTID WMI interface. This feature is
 * independent of, and does not modify, the legacy QCA vendor RSSI
 * monitoring interface implemented in wlan_hdd_rssi_monitor.c.
 */

#include "osif_sync.h"
#include <wlan_hdd_includes.h>
#include <linux/netdevice.h>
#include <wlan_hdd_cqm.h>
#include "wlan_hdd_cm_api.h"
#include <wlan_mlme_ucfg_api.h>

/**
 * __wlan_hdd_cfg80211_set_cqm_rssi_config() - set cqm rssi config
 * @wiphy: Pointer to wireless phy
 * @dev: Pointer to network device
 * @rssi_thold: RSSI threshold in dBm; 0 disables monitoring
 * @rssi_hyst: RSSI hysteresis in dBm
 *
 * Return: 0 on success, negative errno on failure
 */
static int __wlan_hdd_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
						   struct net_device *dev,
						   s32 rssi_thold,
						   u32 rssi_hyst)
{
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx;
	struct wlan_hdd_link_info *link_info = adapter->deflink;
	struct cqm_rssi_config_param req = {0};
	QDF_STATUS status;
	int ret;

	hdd_enter_dev(dev);

	if (hdd_get_conparam() == QDF_GLOBAL_FTM_MODE) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (wlan_hdd_validate_vdev_id(link_info->vdev_id))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!hdd_cm_is_vdev_associated(link_info)) {
		hdd_err("Not in Connected state!");
		return -ENOTCONN;
	}

	req.vdev_id = link_info->vdev_id;
	req.request_id = link_info->cqm_rssi_state.request_id + 1;

	if (rssi_thold) {
		/*
		 * Only a single RSSI threshold is supported currently, hence
		 * BIT(0). This can be extended to BIT(1)/BIT(2) etc. if
		 * multiple threshold support is added in the future.
		 */
		req.enabled_bitmap = BIT(0);
		req.rssi_threshold[0] = rssi_thold;
		req.rssi_hysteresis = rssi_hyst;
	}

	hdd_debug("vdev id: %d req_id: %u thold: %d hyst: %u enabled: 0x%x",
		  req.vdev_id, req.request_id, rssi_thold, rssi_hyst,
		  req.enabled_bitmap);

	status = ucfg_mlme_set_cqm_rssi_config(hdd_ctx->psoc, &req);
	if (!QDF_IS_STATUS_SUCCESS(status)) {
		hdd_err("ucfg_mlme_set_cqm_rssi_config failed(err=%d)", status);
		return -EIO;
	}

	/*
	 * Only commit the new request_id/state once the WMI send has
	 * succeeded, so a failed send cannot invalidate the request_id of
	 * the previously active (successfully sent) configuration.
	 */
	link_info->cqm_rssi_state.request_id = req.request_id;
	link_info->cqm_rssi_state.rssi_threshold = rssi_thold;
	link_info->cqm_rssi_state.rssi_hysteresis = rssi_hyst;
	link_info->cqm_rssi_state.enabled = !!rssi_thold;

	hdd_exit();
	return 0;
}

int wlan_hdd_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
					  struct net_device *dev,
					  s32 rssi_thold, u32 rssi_hyst)
{
	struct osif_vdev_sync *vdev_sync;
	int errno;

	errno = osif_vdev_sync_op_start(dev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_set_cqm_rssi_config(wiphy, dev,
							rssi_thold, rssi_hyst);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

void hdd_cqm_rssi_breach_cb(struct vdev_cqm_rssi_breach_event *event)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	struct hdd_adapter *adapter;
	struct wlan_hdd_link_info *link_info;
	enum nl80211_cqm_rssi_threshold_event rssi_event;

	hdd_enter();

	if (wlan_hdd_validate_context(hdd_ctx))
		return;
	if (!event) {
		hdd_err("event is null");
		return;
	}

	link_info = hdd_get_link_info_by_vdev(hdd_ctx, event->vdev_id);
	if (!link_info) {
		hdd_err("Invalid vdev_id: %u in CQM breach event",
			event->vdev_id);
		return;
	}
	adapter = link_info->adapter;

	if (!link_info->cqm_rssi_state.enabled ||
	    event->request_id != link_info->cqm_rssi_state.request_id) {
		hdd_debug("Discard stale CQM breach: req_id %u (active %u)",
			  event->request_id,
			  link_info->cqm_rssi_state.request_id);
		return;
	}

	if (event->event_type == WLAN_RSSI_LOW_RSSI_THRESHOLD_CQM_MET) {
		rssi_event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	} else if (event->event_type == WLAN_RSSI_HIGH_RSSI_THRESHOLD_CQM_MET) {
		rssi_event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
	} else {
		hdd_err("Invalid CQM RSSI breach event_type: %u",
			event->event_type);
		return;
	}

	hdd_debug("Req Id: %u vdev id: %d event_type: %u Current rssi: %d",
		  event->request_id, event->vdev_id, event->event_type,
		  event->curr_rssi);
	hdd_debug("Current BSSID: " QDF_MAC_ADDR_FMT,
		  QDF_MAC_ADDR_REF(event->curr_bssid.bytes));

	cfg80211_cqm_rssi_notify(adapter->dev, rssi_event, event->curr_rssi,
				 GFP_KERNEL);
}
