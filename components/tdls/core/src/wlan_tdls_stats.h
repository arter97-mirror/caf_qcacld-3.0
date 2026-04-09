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
 *   - Lock helpers:    tdls_stats_lock_{create,destroy,acquire,release}()
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

/* =========================================================================
 * SM lifecycle APIs (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_sm_create() - Create the TDLS stats state machine.
 * @stats_ctx: Pre-allocated TDLS stats context (owned by the PSOC object).
 * @initial_state: Starting state — TDLS_STATS_S_INIT when FW capability is
 *                 present, TDLS_STATS_S_DISABLED otherwise.
 *
 * Allocates and initialises the wlan_sm_engine instance, stores the
 * handle in @stats_ctx->sm.sm_hdl, and creates the SM spinlock.
 * The cache database (@stats_ctx->db) must be initialised separately by
 * the caller when FW capability is present.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_create(struct tdls_stats_context *stats_ctx,
				enum tdls_stats_sm_state initial_state);

/**
 * tdls_stats_sm_destroy() - Destroy the TDLS stats state machine.
 * @stats_ctx: TDLS stats context whose SM is to be destroyed.
 *
 * Destroys the SM spinlock and deletes the wlan_sm_engine instance.
 * The cache database must be deinitialised separately by the caller
 * before invoking this function.
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

/* =========================================================================
 * Lock helpers (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_lock_create() - Initialise the SM spinlock.
 * @stats_ctx: TDLS stats context.
 */
void tdls_stats_lock_create(struct tdls_stats_context *stats_ctx);

/**
 * tdls_stats_lock_destroy() - Destroy the SM spinlock.
 * @stats_ctx: TDLS stats context.
 */
void tdls_stats_lock_destroy(struct tdls_stats_context *stats_ctx);

/**
 * tdls_stats_lock_acquire() - Acquire the SM spinlock (BH-safe).
 * @stats_ctx: TDLS stats context.
 */
void tdls_stats_lock_acquire(struct tdls_stats_context *stats_ctx);

/**
 * tdls_stats_lock_release() - Release the SM spinlock (BH-safe).
 * @stats_ctx: TDLS stats context.
 */
void tdls_stats_lock_release(struct tdls_stats_context *stats_ctx);

/* =========================================================================
 * Helpers called by wlan_tdls_stats.c — defined in other stats-subsystem
 * files.  Those files must provide the implementations.
 * =========================================================================
 */

/* =========================================================================
 * Cache database lifecycle APIs (defined in wlan_tdls_stats.c)
 * =========================================================================
 */

/**
 * tdls_stats_db_init() - Initialise the TDLS stats cache database.
 * @db: Pointer to the cache database structure to initialise.
 * @max_entries: Maximum number of entries the cache can hold.
 *
 * Zeroes the structure, sets the capacity, creates the spinlock, and
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
 * the spinlock.  Called during PSOC destruction.
 */
void tdls_stats_db_deinit(struct tdls_stats_db *db);

/**
 * tdls_stats_push() - Insert a stats entry into the cache database.
 * @db: TDLS stats cache database.
 * @entry: Entry to insert (copied into a newly allocated node).
 *
 * If the cache is full the oldest entry is evicted before the new one
 * is inserted (FIFO eviction policy).
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_push(struct tdls_stats_db *db,
			   const struct tdls_stats_entry *entry);

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
 * @entry: Stats entry to emit.
 *
 * Formats @entry and delivers it to user space via the nl80211 vendor
 * event path.
 */
void tdls_emit_vendor_event(const struct tdls_stats_entry *entry);

/**
 * tdls_stats_handle_sta_connection() - Handle a STA-connected event.
 * @vdev: VDEV on which the STA connection completed.
 *
 * Sends a WMI_REQUEST_TDLS_STATS_CMDID enable=1 command to firmware
 * if the single-STA SCC condition is met.
 */
void tdls_stats_handle_sta_connection(struct wlan_objmgr_vdev *vdev);

#endif /* _WLAN_TDLS_STATS_H_ */
