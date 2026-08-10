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
 * DOC: wlan_dp_wifi_transport_ucfg_api.h
 *
 * Dispatcher (ucfg) layer for the WiFi Transport feature.
 * Translates objmgr psoc into a dp_ctx and routes calls to the
 * core layer (wlan_dp_wifi_transport.c).
 */

#ifndef _WLAN_DP_WIFI_TRANSPORT_UCFG_API_H_
#define _WLAN_DP_WIFI_TRANSPORT_UCFG_API_H_

#ifdef FEATURE_WIFI_TRANSPORT

#include <wlan_objmgr_psoc_obj.h>
#include <qdf_types.h>

/**
 * ucfg_dp_wifi_transport_init() - start a wifi-transport session
 * @psoc: psoc object
 *
 * Return: QDF_STATUS_SUCCESS or error code
 */
QDF_STATUS
ucfg_dp_wifi_transport_init(struct wlan_objmgr_psoc *psoc);

/**
 * ucfg_dp_wifi_transport_deinit() - stop a wifi-transport session
 * @psoc: psoc object
 * @is_recovery_stop: true if called from SSR-down path
 */
void ucfg_dp_wifi_transport_deinit(struct wlan_objmgr_psoc *psoc,
				   bool is_recovery_stop);

#endif /* FEATURE_WIFI_TRANSPORT */
#endif /* _WLAN_DP_WIFI_TRANSPORT_UCFG_API_H_ */
