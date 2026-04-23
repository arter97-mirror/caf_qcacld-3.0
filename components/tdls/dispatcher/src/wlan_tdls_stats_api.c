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
#include "wlan_tdls_main.h"

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

/**
 * wlan_tdls_stats_enable_cmd() - Dispatcher wrapper for the
 *                                TDLS_STATS_ENABLE scheduler message handler.
 * @stats_ctx: TDLS stats context obtained from the scheduler message bodyptr.
 *
 * Return: None
 */
void wlan_tdls_stats_enable_cmd(struct tdls_stats_context *stats_ctx)
{
	tdls_stats_enable_cmd(stats_ctx);
}

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
				    bool enable)
{
	struct tdls_soc_priv_obj *soc_obj;

	soc_obj = wlan_psoc_get_tdls_soc_obj(psoc);
	if (!soc_obj) {
		tdls_err("TDLS soc obj is NULL");
		return QDF_STATUS_E_INVAL;
	}

	if (!soc_obj->stats_ctx) {
		tdls_err("TDLS stats context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	return tdls_get_tdls_stats(soc_obj->stats_ctx, enable);
}

/**
 * wlan_tdls_stats_notify_fw_cap() - Notify the TDLS stats SM that FW service
 *                                   capability and INI have been finalised.
 * @psoc: PSOC object.
 *
 * Delivers TDLS_STATS_EV_FW_CAP_UPDATED to the SM.  If the SM is in DISABLED
 * state and tdls_stats_enable is now true, the cache DB is initialised and
 * the SM transitions to INIT.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS wlan_tdls_stats_notify_fw_cap(struct wlan_objmgr_psoc *psoc)
{
	struct tdls_soc_priv_obj *soc_obj;

	if (!psoc)
		return QDF_STATUS_E_INVAL;

	soc_obj = wlan_psoc_get_tdls_soc_obj(psoc);
	if (!soc_obj) {
		tdls_err("TDLS soc obj is NULL");
		return QDF_STATUS_E_INVAL;
	}

	if (!soc_obj->stats_ctx) {
		tdls_err("TDLS stats context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	return tdls_stats_sm_deliver_event(soc_obj->stats_ctx,
					   TDLS_STATS_EV_FW_CAP_UPDATED,
					   0, NULL);
}

/**
 * wlan_tdls_stats_record_peers_teardown() - Dispatcher wrapper for the
 *                                           unified TDLS teardown stats
 *                                           recorder.
 * @psoc:        PSOC object.
 * @vdev_id:     Vdev ID of the STA session, or WLAN_UMAC_VDEV_ID_MAX to
 *               record teardown for all connected peers regardless of vdev.
 * @reason_code: Teardown reason to record for each peer.
 *
 * Thin wrapper that delegates to the core function
 * tdls_stats_record_peers_teardown().
 *
 * Return: None
 */
void wlan_tdls_stats_record_peers_teardown(
				struct wlan_objmgr_psoc *psoc,
				uint8_t vdev_id,
				enum tdls_stats_reason_code reason_code)
{
	tdls_stats_record_peers_teardown(psoc, vdev_id, reason_code);
}
