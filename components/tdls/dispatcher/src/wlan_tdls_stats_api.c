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
 * Two variants are provided:
 *   tdls_stats_sm_deliver_event()      - acquires the SM lock (for external
 *                                        callers that do not hold the lock)
 *   tdls_stats_sm_deliver_event_sync() - lock-free (for callers that already
 *                                        hold the SM lock)
 *
 * Lock ordering: tdls_stats_sm_lock (outer) -> tdls_stats_db::lock (inner).
 */

#include <wlan_tdls_stats_api.h>
#include <wlan_sm_engine.h>

/**
 * tdls_stats_sm_deliver_event_sync() - Deliver an event to the TDLS stats SM
 *                                      without acquiring the SM lock.
 * @stats_ctx: TDLS stats context
 * @event: Event id (enum tdls_stats_sm_evt)
 * @data_len: Length of event data in bytes
 * @data: Pointer to event-specific data
 *
 * Dispatches @event directly to the SM engine.  The caller is responsible
 * for holding @stats_ctx->sm.tdls_stats_sm_lock before invoking this
 * function.  Use tdls_stats_sm_deliver_event() when the lock is not
 * already held.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
tdls_stats_sm_deliver_event_sync(struct tdls_stats_context *stats_ctx,
				 enum tdls_stats_sm_evt event,
				 uint16_t data_len, void *data)
{
	return wlan_sm_dispatch(stats_ctx->sm.sm_hdl, event, data_len, data);
}

/**
 * tdls_stats_sm_deliver_event() - Deliver an event to the TDLS stats SM
 *                                 with SM lock protection.
 * @stats_ctx: TDLS stats context
 * @event: Event id (enum tdls_stats_sm_evt)
 * @data_len: Length of event data in bytes
 * @data: Pointer to event-specific data
 *
 * Acquires @stats_ctx->sm.tdls_stats_sm_lock, dispatches @event to the
 * SM engine, then releases the lock.  This is the standard entry point
 * for all external callers (OS-IF, target_if).
 *
 * Do NOT call this function while already holding tdls_stats_sm_lock;
 * use tdls_stats_sm_deliver_event_sync() instead to avoid deadlock.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tdls_stats_sm_deliver_event(struct tdls_stats_context *stats_ctx,
				       enum tdls_stats_sm_evt event,
				       uint16_t data_len, void *data)
{
	QDF_STATUS status;

	qdf_spin_lock_bh(&stats_ctx->sm.tdls_stats_sm_lock);
	status = tdls_stats_sm_deliver_event_sync(stats_ctx, event,
						  data_len, data);
	qdf_spin_unlock_bh(&stats_ctx->sm.tdls_stats_sm_lock);

	return status;
}
