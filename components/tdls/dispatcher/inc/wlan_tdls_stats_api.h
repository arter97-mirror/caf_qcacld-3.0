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
 * DOC: wlan_tdls_stats_api.h
 *
 * TDLS stats dispatcher-layer public API declarations.
 *
 * This header exposes the event-delivery interface for the TDLS stats
 * state machine to external components (OS-IF, target_if, etc.).
 * Callers must include this header and link against the TDLS component
 * to deliver events to the SM.
 *
 * Lock ordering: tdls_stats_sm_lock (outer) -> tdls_stats_db::lock (inner).
 */

#ifndef _WLAN_TDLS_STATS_API_H_
#define _WLAN_TDLS_STATS_API_H_

#include <wlan_tdls_stats_public_structs.h>
#include <wlan_objmgr_psoc_obj.h>

/**
 * wlan_tdls_stats_sm_deliver_event() - Deliver an event to the TDLS stats SM.
 * @stats_ctx: TDLS stats context
 * @event: Event id (enum tdls_stats_sm_evt)
 * @data_len: Length of event data in bytes
 * @data: Pointer to event-specific data
 *
 * Dispatcher entry point for all external callers (OS-IF, target_if).
 * Delegates to the core tdls_stats_sm_deliver_event() which acquires the
 * SM lock, dispatches the event, and releases the lock.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_tdls_stats_sm_deliver_event(struct tdls_stats_context *stats_ctx,
				 enum tdls_stats_sm_evt event,
				 uint16_t data_len, void *data);

/**
 * wlan_tdls_stats_enable_cmd() - Dispatcher wrapper for the
 *                                TDLS_STATS_ENABLE scheduler message handler.
 * @stats_ctx: TDLS stats context obtained from the scheduler message bodyptr.
 *
 * Return: None
 */
void wlan_tdls_stats_enable_cmd(struct tdls_stats_context *stats_ctx);

/**
 * wlan_tdls_get_tdls_stats() - Dispatcher API to handle a TDLS stats
 *                              enable/disable request from the HDD layer.
 * @psoc: PSOC object.
 * @enable: true  -> enable TDLS stats forwarding (TDLS_STATS_EV_ENABLE)
 *          false -> disable TDLS stats forwarding (TDLS_STATS_EV_DISABLE)
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS wlan_tdls_get_tdls_stats(struct wlan_objmgr_psoc *psoc,
				    bool enable);

#endif /* _WLAN_TDLS_STATS_API_H_ */
