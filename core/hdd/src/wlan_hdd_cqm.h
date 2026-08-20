/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef __WLAN_HDD_CQM_H
#define __WLAN_HDD_CQM_H

/**
 * DOC: wlan_hdd_cqm.h
 *
 * WLAN Host Device Driver adaptive-hysteresis RSSI CQM API specification.
 *
 * Implements the standard Linux NL80211_CMD_SET_CQM single-threshold
 * configuration (cfg80211_ops.set_cqm_rssi_config) on top of the
 * WMI_RSSI_ADAPTIVE_BREACH_MONITOR_CONFIG_CMDID /
 * WMI_RSSI_ADAPTIVE_BREACH_EVENTID
 * WMI interface. Only a single RSSI threshold + hysteresis is supported
 * (customer-confirmed scope); cfg80211_ops.set_cqm_rssi_range_config and
 * NL80211_EXT_FEATURE_CQM_RSSI_LIST are intentionally not implemented.
 */

struct vdev_cqm_rssi_breach_event;

#ifdef FEATURE_CQM_RSSI

#include <wlan_vdev_mgr_tgt_if_rx_defs.h>

/**
 * wlan_hdd_cfg80211_set_cqm_rssi_config() - set cqm rssi config
 * @wiphy: Pointer to wireless phy
 * @dev: Pointer to network device
 * @rssi_thold: RSSI threshold in dBm; 0 disables monitoring
 * @rssi_hyst: RSSI hysteresis in dBm
 *
 * Return: 0 on success, negative errno on failure
 */
int wlan_hdd_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
					  struct net_device *dev,
					  s32 rssi_thold, u32 rssi_hyst);

/**
 * hdd_cqm_rssi_breach_cb() - CQM RSSI breach NL80211 notification
 * @event: CQM RSSI breach event data
 *
 * This function reads the CQM RSSI breach event %event, validates it
 * against the active configuration on the vdev, and delivers the
 * standard Linux CQM notification via cfg80211_cqm_rssi_notify().
 *
 * Return: none
 */
void hdd_cqm_rssi_breach_cb(struct vdev_cqm_rssi_breach_event *event);

#else /* FEATURE_CQM_RSSI */
static inline
void hdd_cqm_rssi_breach_cb(struct vdev_cqm_rssi_breach_event *event)
{
}
#endif /* FEATURE_CQM_RSSI */

#endif /* __WLAN_HDD_CQM_H */
