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
 * DOC: os_if_dp_wifi_transport.h
 *
 * OS-IF layer for the WiFi Transport feature (FEATURE_WIFI_TRANSPORT).
 * Publishes and tears down the auxiliary device binding point and
 * delegates session init/deinit to the dispatcher layer.
 */

#ifndef _OS_IF_DP_WIFI_TRANSPORT_H_
#define _OS_IF_DP_WIFI_TRANSPORT_H_

#ifdef FEATURE_WIFI_TRANSPORT

#include <wlan_objmgr_psoc_obj.h>
#include <qdf_types.h>

/**
 * os_if_dp_wifi_transport_register() - publish auxiliary device binding point
 * @psoc: psoc object
 *
 * Called once from hdd_wlan_start_modules(). Creates and registers the
 * auxiliary device so qca-wifi-transport.ko can probe and bind.
 *
 * Return: QDF_STATUS_SUCCESS or QDF_STATUS_E_FAILURE
 */
QDF_STATUS
os_if_dp_wifi_transport_register(struct wlan_objmgr_psoc *psoc);

/**
 * os_if_dp_wifi_transport_deregister() - teardown auxiliary device binding
 * @psoc: psoc object
 * @is_recovery_stop: true if called from SSR-down / hdd_wlan_stop_modules
 *
 * Called once from hdd_wlan_stop_modules(). Removes the auxiliary device.
 * If a session is still active, fires the deinit chain first.
 */
void os_if_dp_wifi_transport_deregister(struct wlan_objmgr_psoc *psoc,
					bool is_recovery_stop);

/**
 * os_if_dp_wifi_transport_init() - start a use-case session
 * @psoc: psoc object
 *
 * Delegated from wifi_transport_host_ops_init() after input validation.
 * May be called multiple times per driver lifetime (once per use case).
 *
 * Return: QDF_STATUS_SUCCESS or error code
 */
QDF_STATUS
os_if_dp_wifi_transport_init(struct wlan_objmgr_psoc *psoc);

/**
 * os_if_dp_wifi_transport_deinit() - stop the active use-case session
 * @psoc: psoc object
 * @is_recovery_stop: true if called from SSR-down path
 */
void os_if_dp_wifi_transport_deinit(struct wlan_objmgr_psoc *psoc,
				    bool is_recovery_stop);

#endif /* FEATURE_WIFI_TRANSPORT */
#endif /* _OS_IF_DP_WIFI_TRANSPORT_H_ */
