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
 * DOC: wlan_hdd_n79_coex.h
 *
 * HDD inline helpers and vendor command declaration for N79 5 GHz
 * antenna-sharing coexistence (FR200474).
 */

#ifndef __WLAN_HDD_N79_COEX_H
#define __WLAN_HDD_N79_COEX_H

#ifdef FEATURE_N79_COEX
#include <net/cfg80211.h>
#include <wlan_objmgr_psoc_obj.h>
#include "wlan_hdd_main.h"
#include "wlan_hdd_object_manager.h"
#include "wlan_coex_ucfg_api.h"
#include "wlan_cfg80211_coex.h"

static inline void
hdd_n79_coex_sta_connect(struct hdd_context *hdd_ctx,
			 struct wlan_objmgr_vdev *vdev)
{
	ucfg_coex_n79_wlan_evt(hdd_ctx->psoc, vdev, WLAN_COEX_N79_STA_CONNECT);
}

static inline void
hdd_n79_coex_sta_disconnect(struct hdd_context *hdd_ctx,
			    struct wlan_objmgr_vdev *vdev)
{
	ucfg_coex_n79_wlan_evt(hdd_ctx->psoc, vdev,
			       WLAN_COEX_N79_STA_DISCONNECT);
}

static inline void
hdd_n79_coex_sap_start(struct hdd_context *hdd_ctx,
		       struct wlan_hdd_link_info *link_info)
{
	struct wlan_objmgr_vdev *vdev =
		hdd_objmgr_get_vdev_by_user(link_info, WLAN_COEX_ID);

	if (!vdev)
		return;
	ucfg_coex_n79_wlan_evt(hdd_ctx->psoc, vdev, WLAN_COEX_N79_SAP_START);
	hdd_objmgr_put_vdev_by_user(vdev, WLAN_COEX_ID);
}

static inline void
hdd_n79_coex_sap_stop(struct hdd_context *hdd_ctx,
		      struct wlan_hdd_link_info *link_info)
{
	struct wlan_objmgr_vdev *vdev =
		hdd_objmgr_get_vdev_by_user(link_info, WLAN_COEX_ID);

	if (!vdev)
		return;
	ucfg_coex_n79_wlan_evt(hdd_ctx->psoc, vdev, WLAN_COEX_N79_SAP_STOP);
	hdd_objmgr_put_vdev_by_user(vdev, WLAN_COEX_ID);
}

static inline void
hdd_n79_coex_nan_update(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id,
			bool is_start)
{
	struct wlan_objmgr_vdev *vdev =
		wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						     WLAN_COEX_ID);

	if (!vdev)
		return;
	ucfg_coex_n79_wlan_evt(psoc, vdev,
			       is_start ? WLAN_COEX_N79_NAN_START
					: WLAN_COEX_N79_NAN_STOP);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_COEX_ID);
}

/**
 * wlan_hdd_cfg80211_n79_coex() - vendor cmd handler for N79_COEX
 * @wiphy: wiphy pointer
 * @wdev: wireless_dev pointer (used to reach hdd_ctx/psoc)
 * @data: NL attribute buffer
 * @data_len: buffer length
 *
 * Mode-agnostic outer handler: acquires psoc-level sync then delegates
 * to wlan_cfg80211_coex_n79_vendor_cmd() in os_if/coex.
 * WIPHY_VENDOR_CMD_NEED_WDEV is set; NEED_RUNNING is intentionally omitted
 * so the command works regardless of STA connection state.
 *
 * Return: 0 on success, negative errno on failure
 */
int wlan_hdd_cfg80211_n79_coex(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int data_len);

#define FEATURE_N79_COEX_COMMANDS                                       \
{                                                                       \
	.info.vendor_id = QCA_NL80211_VENDOR_ID,                        \
	.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_N79_COEX,              \
	.flags = WIPHY_VENDOR_CMD_NEED_WDEV,                            \
	.doit = wlan_hdd_cfg80211_n79_coex,                             \
	vendor_command_policy(n79_coex_policy,                          \
			      QCA_WLAN_VENDOR_ATTR_N79_COEX_CONFIG_MAX)  \
},

#else /* !FEATURE_N79_COEX */
#include "qdf_types.h"
struct hdd_context;
struct wlan_hdd_link_info;
struct wlan_objmgr_psoc;
struct wlan_objmgr_vdev;

static inline void
hdd_n79_coex_sta_connect(struct hdd_context *hdd_ctx,
			 struct wlan_objmgr_vdev *vdev) {}

static inline void
hdd_n79_coex_sta_disconnect(struct hdd_context *hdd_ctx,
			    struct wlan_objmgr_vdev *vdev) {}

static inline void
hdd_n79_coex_sap_start(struct hdd_context *hdd_ctx,
		       struct wlan_hdd_link_info *link_info) {}

static inline void
hdd_n79_coex_sap_stop(struct hdd_context *hdd_ctx,
		      struct wlan_hdd_link_info *link_info) {}

static inline void
hdd_n79_coex_nan_update(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id,
			bool is_start) {}

#define FEATURE_N79_COEX_COMMANDS

#endif /* FEATURE_N79_COEX */

#endif /* __WLAN_HDD_N79_COEX_H */
