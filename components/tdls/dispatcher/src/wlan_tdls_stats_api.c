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
 * DOC: wlan_tdls_stats_api.c
 *
 * TDLS stats dispatcher-layer APIs.
 *
 * This file provides the public event-delivery interface for the TDLS
 * stats state machine.  External callers (OS-IF for enable/disable
 * commands, target_if for FW periodic stats events) must use these APIs
 * to deliver events to the SM rather than calling into the core directly.
 *
 * Lock ordering: tdls_stats_sm_lock (outer) -> tdls_stats_db::lock (inner).
 */

#include <wlan_tdls_stats_api.h>
#include "wlan_tdls_stats.h"

/**
 * wlan_tdls_stats_sm_deliver_event() - Dispatcher entry point to deliver an
 *                                      event to the TDLS stats SM.
 * @stats_ctx: TDLS stats context
 * @event: Event id (enum tdls_stats_sm_evt)
 * @data_len: Length of event data in bytes
 * @data: Pointer to event-specific data
 *
 * Thin wrapper that delegates to the core function
 * tdls_stats_sm_deliver_event(), which acquires the SM lock, dispatches
 * the event, and releases the lock.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_tdls_stats_sm_deliver_event(struct tdls_stats_context *stats_ctx,
				 enum tdls_stats_sm_evt event,
					    uint16_t data_len, void *data)
{
	return tdls_stats_sm_deliver_event(stats_ctx, event, data_len, data);
}
