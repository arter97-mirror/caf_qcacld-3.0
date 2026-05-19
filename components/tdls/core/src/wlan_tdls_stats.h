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
 * DOC: wlan_tdls_stats.h
 *
 * TDLS stats state machine — internal declarations.
 *
 * This header exposes the internal API of wlan_tdls_stats.c to the rest
 * of the TDLS core layer.  It covers:
 *
 *   - SM lifecycle:    tdls_stats_sm_create() / tdls_stats_sm_destroy()
 *   - SM transition:   tdls_stats_sm_transition_to()
 *
 * It also forward-declares the helper functions that wlan_tdls_stats.c
 * calls but that are defined in other stats-subsystem files (cache DB,
 * vendor-event emit, WMI helpers).  Those files must include this header
 * and provide the implementations.
 *
 * External (OS-IF / target_if) callers must use the dispatcher-layer API
 * in wlan_tdls_stats_api.h instead of including this header directly.
 */

#ifndef _WLAN_TDLS_STATS_H_
#define _WLAN_TDLS_STATS_H_

#include <wlan_tdls_stats_public_structs.h>
#include <wlan_sm_engine.h>

/* Forward declaration to avoid circular includes with wlan_tdls_main.h */
struct tdls_soc_priv_obj;

/* =========================================================================
 * SM lock helpers (inline, mirroring the cm_lock_* pattern)
 * =========================================================================
 */

/**
 * tdls_stats_lock_create() - Initialise the TDLS stats SM mutex.
 * @stats_ctx: TDLS stats context.
 */
static inline void
tdls_stats_lock_create(struct tdls_stats_context *stats_ctx)
{
	qdf_mutex_create(&stats_ctx->sm.tdls_stats_sm_lock);
}

/**
 * tdls_stats_lock_destroy() - Destroy the TDLS stats SM mutex.
 * @stats_ctx: TDLS stats context.
 */
static inline void
tdls_stats_lock_destroy(struct tdls_stats_context *stats_ctx)
{
	qdf_mutex_destroy(&stats_ctx->sm.tdls_stats_sm_lock);
}

/**
 * tdls_stats_lock_acquire() - Acquire the TDLS stats SM mutex.
 * @stats_ctx: TDLS stats context.
 */
static inline void
tdls_stats_lock_acquire(struct tdls_stats_context *stats_ctx)
{
	qdf_mutex_acquire(&stats_ctx->sm.tdls_stats_sm_lock);
}

/**
 * tdls_stats_lock_release() - Release the TDLS stats SM mutex.
 * @stats_ctx: TDLS stats context.
 */
static inline void
tdls_stats_lock_release(struct tdls_stats_context *stats_ctx)
{
	qdf_mutex_release(&stats_ctx->sm.tdls_stats_sm_lock);
}

/* =========================================================================
 * SM lifecycle APIs (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_sm_create() - Allocate and initialise the TDLS stats context
 *                          and state machine.
 * @psoc: PSOC object used to query FW service capability and for back-pointer
 *        storage in the allocated context.
 * @stats_ctx_out: Output pointer.  On success, *@stats_ctx_out is set to the
 *                 newly allocated and fully initialised tdls_stats_context.
 *                 The caller stores this pointer in the TDLS PSOC object
 *                 (e.g. tdls_soc_priv_obj::stats_ctx).
 *
 * This function:
 *   1. Allocates a tdls_stats_context (includes the embedded cache DB).
 *   2. Queries wmi_service_tdls_stats_info to determine the initial state:
 *        - FW capability present  -> TDLS_STATS_S_INIT  (cache DB initialised)
 *        - FW capability absent   -> TDLS_STATS_S_DISABLED (cache DB skipped)
 *   3. Creates the wlan_sm_engine instance and the SM mutex lock.
 *   4. Initialises the cache DB when starting in TDLS_STATS_S_INIT.
 *
 * The double-pointer output parameter is the standard C pattern for
 * "allocate-and-return": the function owns the allocation and the caller
 * receives the pointer via *@stats_ctx_out.  The caller must later pass
 * this pointer to tdls_stats_sm_destroy() to free all resources.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_create(struct wlan_objmgr_psoc *psoc,
				struct tdls_stats_context **stats_ctx_out);

/**
 * tdls_stats_sm_destroy() - Destroy the TDLS stats state machine and free
 *                           all associated resources.
 * @stats_ctx: TDLS stats context returned by tdls_stats_sm_create().
 *
 * Cleanup order:
 *   1. Deinit cache DB (only if db_initialized is true).
 *   2. Destroy the SM mutex lock.
 *   3. Delete the wlan_sm_engine instance.
 *   4. Free the context itself.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_destroy(struct tdls_stats_context *stats_ctx);

/* =========================================================================
 * SM transition helper (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_sm_transition_to() - Transition the SM to a new state.
 * @stats_ctx: TDLS stats context.
 * @state: Target state (enum tdls_stats_sm_state).
 *
 * Thin wrapper around wlan_sm_transition_to().  The SM engine
 * automatically invokes the exit callback of the current state and the
 * entry callback of the new state.
 *
 * Must be called with @stats_ctx->sm.tdls_stats_sm_lock held.
 */
void tdls_stats_sm_transition_to(struct tdls_stats_context *stats_ctx,
				 enum tdls_stats_sm_state state);

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
				 uint16_t data_len, void *data);

/**
 * tdls_stats_sm_deliver_event() - Deliver an event to the TDLS stats SM
 *                                 with SM lock protection.
 * @stats_ctx: TDLS stats context
 * @event: Event id (enum tdls_stats_sm_evt)
 * @data_len: Length of event data in bytes
 * @data: Pointer to event-specific data
 *
 * Acquires @stats_ctx->sm.tdls_stats_sm_lock, dispatches @event to the
 * SM engine via tdls_stats_sm_deliver_event_sync(), then releases the lock.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tdls_stats_sm_deliver_event(struct tdls_stats_context *stats_ctx,
				       enum tdls_stats_sm_evt event,
				       uint16_t data_len, void *data);

/* =========================================================================
 * Cache database lifecycle APIs (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_db_init() - Initialise the TDLS stats cache database.
 * @db: Pointer to the cache database structure to initialise.
 * @max_entries: Maximum number of entries the cache can hold.
 *
 * Zeroes the structure, sets the capacity, creates the mutex, and
 * creates the QDF list.  Called during PSOC creation when FW capability
 * is present.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_db_init(struct tdls_stats_db *db,
			      uint32_t max_entries);

/**
 * tdls_stats_db_flush() - Silently remove all entries from the cache.
 * @db: Pointer to the cache database structure.
 *
 * Removes and frees every node without emitting vendor events.  Used
 * during cleanup / deinit only.  Distinct from
 * tdls_stats_flush_entire_cache() which emits vendor events.
 */
void tdls_stats_db_flush(struct tdls_stats_db *db);

/**
 * tdls_stats_db_deinit() - Deinitialise the TDLS stats cache database.
 * @db: Pointer to the cache database structure.
 *
 * Flushes all remaining entries, destroys the QDF list, and destroys
 * the mutex.  Called during PSOC destruction.
 */
void tdls_stats_db_deinit(struct tdls_stats_db *db);

/**
 * tdls_stats_flush_entire_cache() - Flush all cached entries as vendor events.
 * @stats_ctx: TDLS stats context.
 *
 * Dequeues every entry from the cache database in FIFO (oldest-first)
 * order and emits each one as a vendor event via tdls_emit_vendor_event().
 * Called from the ENABLED state entry callback.
 *
 * Return: Number of entries flushed.
 */
uint32_t tdls_stats_flush_entire_cache(struct tdls_stats_context *stats_ctx);

/**
 * tdls_emit_vendor_event() - Emit a single stats entry as a vendor event.
 * @psoc:  PSOC object used to look up the registered OS-IF callback.
 * @entry: Stats entry to emit.
 *
 * Retrieves the %tdls_stats_emit_cb callback registered by the OS-IF layer
 * (HDD) via wlan_tdls_register_stats_emit_cb() and invokes it.  This keeps
 * the TDLS component free of any direct dependency on HDD headers.
 *
 * If no callback has been registered the entry is silently dropped.
 */
void tdls_emit_vendor_event(struct wlan_objmgr_psoc *psoc,
			    const struct tdls_stats_entry *entry);

/**
 * tdls_stats_handle_sta_connection() - Handle a STA-connected event.
 * @vdev: VDEV on which the STA connection completed.
 *
 * Sends a WMI_REQUEST_TDLS_STATS_CMDID enable=1 command to firmware
 * if the single-STA SCC condition is met.
 */
void tdls_stats_handle_sta_connection(struct wlan_objmgr_vdev *vdev);

/**
 * tdls_stats_enable_cmd() - Core handler for the TDLS_STATS_ENABLE scheduler
 *                           message.
 * @stats_ctx: TDLS stats context obtained from the scheduler message bodyptr.
 *
 * Must NOT be called while tdls_stats_sm_lock is held.
 */
void tdls_stats_enable_cmd(struct tdls_stats_context *stats_ctx);

/**
 * tdls_stats_record_peer_add() - Record a TDLS stats entry when a new peer
 *                                is added to the peer list.
 * @soc_obj: TDLS soc private object (provides stats_ctx).
 * @vdev:    VDEV on which the peer is being added (used for channel lookup).
 * @macaddr: MAC address of the newly added peer.
 * @rssi:    Last known RSSI for the peer (0 if not yet measured).
 *
 * Populates a struct tdls_stats_entry with type=TDLS_STATS_IF_SETUP,
 * subtype=TDLS_STATS_SUBTYPE_GENERAL, is_sender=0 (responder), and delivers
 * it to the TDLS stats SM via TDLS_STATS_EV_NEW_EVENT.
 * No-op if soc_obj->stats_ctx is NULL.
 */
void tdls_stats_record_peer_add(struct tdls_soc_priv_obj *soc_obj,
				struct wlan_objmgr_vdev *vdev,
				const uint8_t *macaddr,
				int8_t rssi);

/**
 * tdls_stats_record_dp_pkt() - Record a TDLS data-path packet stats entry.
 * @soc_obj:     TDLS soc private object (provides stats_ctx).
 * @vdev:        VDEV on which the packet was sent/received.
 * @macaddr:     Peer MAC address of the packet.
 * @dir:         QDF_TX or QDF_RX.
 * @type:        TDLS stats event type derived from the TDLS action code.
 * @subtype:     TDLS stats event subtype derived from the TDLS action code.
 * @reason_code: Reason code to record with the entry; use
 *               TDLS_STATS_REASON_GENERAL for normal data-path frames.
 *
 * Called from tdls_process_stats_dp_pkt() on the TDLS scheduler thread
 * after wlan_dp_rx_tdls_packet() detects a TDLS frame (EtherType 0x890D)
 * in the RX data path and posts a TDLS_CMD_STATS_DP_PKT message.
 * The type, subtype, and reason_code are already mapped from the raw
 * action_code and dot11_reason before this function is called.
 * No-op if soc_obj->stats_ctx is NULL.
 */
void tdls_stats_record_dp_pkt(struct tdls_soc_priv_obj *soc_obj,
			      struct wlan_objmgr_vdev *vdev,
			      const uint8_t *macaddr,
			      enum qdf_proto_dir dir,
			      enum tdls_stats_type type,
			      enum tdls_stats_subtype subtype,
			      enum tdls_stats_reason_code reason_code);

/**
 * tdls_stats_record_discovery_resp() - Record a TDLS discovery response stats
 *                                      entry.
 * @soc_obj: TDLS soc private object (provides stats_ctx).
 * @vdev:    VDEV on which the discovery response was received.
 * @macaddr: MAC address of the peer that sent the discovery response.
 * @rssi:    RSSI of the received discovery response frame.
 * @success: true if the discovery was successful (RSSI threshold met and
 *           setup request will be sent); false if the RSSI threshold was
 *           not met and the link returns to IDLE.
 *
 * Populates a struct tdls_stats_entry with type=TDLS_STATS_DISCOVERY,
 * subtype=TDLS_STATS_SUBTYPE_RESP, is_sender=0 (we received it), and
 * delivers it to the TDLS stats SM via TDLS_STATS_EV_NEW_EVENT.
 * No-op if soc_obj->stats_ctx is NULL.
 */
void tdls_stats_record_discovery_resp(struct tdls_soc_priv_obj *soc_obj,
				      struct wlan_objmgr_vdev *vdev,
				      const uint8_t *macaddr,
				      int8_t rssi,
				      bool success);

/**
 * tdls_stats_record_peer_teardown() - Record a TDLS teardown stats entry.
 * @soc_obj:     TDLS soc private object (provides stats_ctx).
 * @vdev:        VDEV on which the teardown is occurring.
 * @macaddr:     MAC address of the peer being torn down.
 * @reason_code: Teardown reason; use TDLS_STATS_REASON_NO_TRAFFIC when
 *               both tx and rx packet counts are zero, or
 *               TDLS_STATS_REASON_INSUFFICIENT_TRAFFIC when traffic exists
 *               but is below the idle threshold.
 *
 * Populates a struct tdls_stats_entry with type=TDLS_STATS_TEARDOWN,
 * subtype=TDLS_STATS_SUBTYPE_COMPLETE, is_sender=1, and delivers it to
 * the TDLS stats SM via TDLS_STATS_EV_NEW_EVENT.
 * No-op if soc_obj->stats_ctx is NULL.
 */
void tdls_stats_record_peer_teardown(struct tdls_soc_priv_obj *soc_obj,
				     struct wlan_objmgr_vdev *vdev,
				     const uint8_t *macaddr,
				     enum tdls_stats_reason_code reason_code);

/**
 * tdls_stats_record_peers_teardown() - Unified teardown stats recorder for
 *                                       connected TDLS peers.
 * @psoc:        PSOC object.
 * @vdev_id:     Vdev ID of the STA session, or WLAN_UMAC_VDEV_ID_MAX to
 *               record teardown for all connected peers regardless of vdev.
 *               When WLAN_UMAC_VDEV_ID_MAX is passed, the TDLS link vdev is
 *               used for the channel and session_id fields of the stats entry.
 * @reason_code: Teardown reason to record for each peer.
 *
 * Covers two use cases:
 *   1. Per-vdev (vdev_id != WLAN_UMAC_VDEV_ID_MAX): records teardown only for
 *      peers whose session_id matches @vdev_id.
 *   2. All-peers (vdev_id == WLAN_UMAC_VDEV_ID_MAX): records teardown for
 *      every connected TDLS peer using the TDLS link vdev.
 *
 * No-op if soc_obj->stats_ctx is NULL.
 */
void tdls_stats_record_peers_teardown(struct wlan_objmgr_psoc *psoc,
				      uint8_t vdev_id,
				      enum tdls_stats_reason_code reason_code);

/**
 * tdls_get_tdls_stats() - Core API to handle a TDLS stats enable/disable
 *                         request from the dispatcher layer.
 * @stats_ctx: TDLS stats context obtained from tdls_soc_priv_obj::stats_ctx.
 * @enable: true  -> deliver TDLS_STATS_EV_ENABLE to the SM
 *          false -> deliver TDLS_STATS_EV_DISABLE to the SM
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_get_tdls_stats(struct tdls_stats_context *stats_ctx,
			       bool enable);

/**
 * tdls_stats_entry_fill_vdev_info() - Snapshot dut_mac and link_id into a
 *                                     stats entry.
 * @entry: Stats entry to populate.
 * @psoc:  PSOC used to look up the vdev by entry->session_id.
 *
 * Called at cache time so the entry carries the correct DUT MAC and link ID
 * regardless of any subsequent reconnection.
 *
 * Core-internal only — callers outside the core layer must use the
 * dispatcher wrapper wlan_tdls_stats_entry_fill_vdev_info().
 */
void tdls_stats_entry_fill_vdev_info(struct tdls_stats_entry *entry,
				     struct wlan_objmgr_psoc *psoc);

/**
 * tdls_stats_entry_find_vdev_info() - Resolve per-peer vdev then fill
 *                                     dut_mac and link_id into entry.
 * @entry: Stats entry whose peer_mac is used to locate the correct vdev.
 * @psoc:  PSOC for the psoc-wide peer lookup.
 *
 * Use instead of tdls_stats_entry_fill_vdev_info() when entry->session_id
 * may not reflect the peer's actual registered vdev (e.g. FW batch events
 * that carry a single ev->vdev_id for all peers).
 *
 * Core-internal only — callers outside the core layer must use the
 * dispatcher wrapper wlan_tdls_stats_entry_find_vdev_info().
 */
void tdls_stats_entry_find_vdev_info(struct tdls_stats_entry *entry,
				     struct wlan_objmgr_psoc *psoc);

#endif /* _WLAN_TDLS_STATS_H_ */
