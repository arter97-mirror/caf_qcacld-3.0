/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

/*
 * DOC: wlan_hdd_connectivity_logging.h
 *
 * Implementation for the Common connectivity logging api.
 */

#ifndef __WLAN_HDD_CONNECTIVITY_LOGGING_H__
#define __WLAN_HDD_CONNECTIVITY_LOGGING_H__

#include <qdf_types.h>
#include <wlan_cfg80211.h>
#include <wlan_connectivity_logging.h>
#include "wlan_hdd_main.h"

#if defined(CONNECTIVITY_DIAG_EVENT)
/**
 * wlan_hdd_connectivity_event_connecting() - Queue the connecting event to
 * the logging queue
 * @hdd_ctx: HDD context
 * @req: Request
 * @vdev_id: Vdev id
 */
void wlan_hdd_connectivity_event_connecting(struct hdd_context *hdd_ctx,
					    struct cfg80211_connect_params *req,
					    uint8_t vdev_id);

/**
 * wlan_hdd_connectivity_fail_event()- Connectivity queue logging event
 * @vdev: VDEV object
 * @rsp: Connection manager connect response
 *
 * Return: None
 */
void wlan_hdd_connectivity_fail_event(struct wlan_objmgr_vdev *vdev,
				      struct wlan_cm_connect_resp *rsp);

#else
static inline
void wlan_hdd_connectivity_event_connecting(struct hdd_context *hdd_ctx,
					    struct cfg80211_connect_params *req,
					    uint8_t vdev_id)
{}

static inline
void wlan_hdd_connectivity_fail_event(struct wlan_objmgr_vdev *vdev,
				      struct wlan_cm_connect_resp *rsp)
{}
#endif
#endif /* __WLAN_HDD_CONNECTIVITY_LOGGING_H__ */
