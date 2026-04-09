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
 * DOC: wlan_tdls_stats.c
 *
 * TDLS stats state machine implementation.
 *
 * This file implements the TDLS stats state machine using the
 * wlan_sm_engine framework.  It defines:
 *   - Per-state entry, exit, and event-handler callbacks
 *   - The state machine info table (tdls_stats_sm_info[])
 *   - The event name table (tdls_stats_sm_event_names[])
 *   - SM lifecycle APIs: create, destroy, deliver_event, transition
 *
 * State overview:
 *   DISABLED  - FW capability absent; all events dropped (terminal state)
 *   INIT      - Default caching; events buffered in the cache database
 *   ENABLED   - Active forwarding; events emitted immediately as vendor events
 */

#include "wlan_tdls_main.h"
#include "wlan_tdls_stats.h"
#include <wlan_tdls_stats_public_structs.h>
#include <wlan_sm_engine.h>

/* =========================================================================
 * Lock helpers
 * =========================================================================
 */

void tdls_stats_lock_create(struct tdls_stats_context *stats_ctx)
{
	qdf_spinlock_create(&stats_ctx->sm.tdls_stats_sm_lock);
}

void tdls_stats_lock_destroy(struct tdls_stats_context *stats_ctx)
{
	qdf_spinlock_destroy(&stats_ctx->sm.tdls_stats_sm_lock);
}

void tdls_stats_lock_acquire(struct tdls_stats_context *stats_ctx)
{
	qdf_spin_lock_bh(&stats_ctx->sm.tdls_stats_sm_lock);
}

void tdls_stats_lock_release(struct tdls_stats_context *stats_ctx)
{
	qdf_spin_unlock_bh(&stats_ctx->sm.tdls_stats_sm_lock);
}

/* =========================================================================
 * Cache database operations
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
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_INVAL if arguments
 *         are invalid.
 */
QDF_STATUS tdls_stats_db_init(struct tdls_stats_db *db, uint32_t max_entries)
{
	if (!db || !max_entries)
		return QDF_STATUS_E_INVAL;

	qdf_mem_zero(db, sizeof(*db));
	db->max_entries = max_entries;
	db->num_entries = 0;

	qdf_spinlock_create(&db->lock);
	qdf_list_create(&db->list, max_entries);

	return QDF_STATUS_SUCCESS;
}

/**
 * tdls_stats_db_flush() - Silently remove all entries from the cache.
 * @db: Pointer to the cache database structure.
 *
 * Removes and frees every node without emitting vendor events.  Used
 * during cleanup / deinit only.  Distinct from
 * tdls_stats_flush_entire_cache() which emits vendor events for each entry.
 */
void tdls_stats_db_flush(struct tdls_stats_db *db)
{
	qdf_list_node_t *ln = NULL;
	struct tdls_stats_node *node;

	if (!db)
		return;

	qdf_spin_lock_bh(&db->lock);

	while (!qdf_list_empty(&db->list)) {
		if (QDF_IS_STATUS_ERROR(
			qdf_list_remove_front(&db->list, &ln)) || !ln)
			break;

		node = qdf_container_of(ln, struct tdls_stats_node, node);
		qdf_mem_free(node);
		db->num_entries--;
	}

	qdf_spin_unlock_bh(&db->lock);
}

/**
 * tdls_stats_db_deinit() - Deinitialise the TDLS stats cache database.
 * @db: Pointer to the cache database structure.
 *
 * Flushes all remaining entries, destroys the QDF list, and destroys
 * the spinlock.  Called during PSOC destruction.
 */
void tdls_stats_db_deinit(struct tdls_stats_db *db)
{
	if (!db)
		return;

	tdls_stats_db_flush(db);

	qdf_list_destroy(&db->list);
	qdf_spinlock_destroy(&db->lock);
}

/**
 * tdls_stats_push() - Insert a stats entry into the cache database.
 * @db: TDLS stats cache database.
 * @entry: Entry to insert (copied into a newly allocated node).
 *
 * If the cache is full the oldest entry (at the front of the list) is
 * evicted before the new entry is inserted at the back, maintaining
 * FIFO order.  All operations are protected by @db->lock.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_push(struct tdls_stats_db *db,
			   const struct tdls_stats_entry *entry)
{
	struct tdls_stats_node *node;
	qdf_list_node_t *old_ln = NULL;
	QDF_STATUS status;

	if (!db || !entry)
		return QDF_STATUS_E_INVAL;

	qdf_spin_lock_bh(&db->lock);

	/* Evict oldest entry if the cache is full */
	if (db->num_entries >= db->max_entries) {
		status = qdf_list_remove_front(&db->list, &old_ln);

		if (QDF_IS_STATUS_SUCCESS(status) && old_ln) {
			struct tdls_stats_node *old_node =
				qdf_container_of(old_ln,
						 struct tdls_stats_node, node);
			qdf_mem_free(old_node);
			db->num_entries--;
		}
	}

	/* Allocate a new node */
	node = qdf_mem_malloc(sizeof(*node));
	if (!node) {
		qdf_spin_unlock_bh(&db->lock);
		return QDF_STATUS_E_NOMEM;
	}

	/* Copy entry data and insert at the back (newest at tail) */
	node->entry = *entry;
	qdf_list_insert_back(&db->list, &node->node);
	db->num_entries++;

	qdf_spin_unlock_bh(&db->lock);

	return QDF_STATUS_SUCCESS;
}

/**
 * tdls_stats_flush_entire_cache() - Flush all cached entries as vendor events.
 * @stats_ctx: TDLS stats context.
 *
 * Dequeues every entry from the cache database in FIFO (oldest-first)
 * order and emits each one as a vendor event via tdls_emit_vendor_event().
 * The lock is released while emitting each event to avoid holding it
 * across the vendor-event path.  Called from the ENABLED state entry
 * callback when transitioning from INIT.
 *
 * Return: Number of entries flushed.
 */
uint32_t tdls_stats_flush_entire_cache(struct tdls_stats_context *stats_ctx)
{
	uint32_t flushed_count = 0;
	qdf_list_node_t *ln = NULL;
	qdf_list_node_t *next_ln = NULL;
	struct tdls_stats_node *node;

	if (!stats_ctx)
		return 0;

	qdf_spin_lock_bh(&stats_ctx->db.lock);
	qdf_list_peek_front(&stats_ctx->db.list, &ln);

	while (ln) {
		qdf_list_peek_next(&stats_ctx->db.list, ln, &next_ln);
		qdf_list_remove_front(&stats_ctx->db.list, &ln);
		stats_ctx->db.num_entries--;

		/* Release lock before emitting the vendor event */
		qdf_spin_unlock_bh(&stats_ctx->db.lock);

		node = qdf_container_of(ln, struct tdls_stats_node, node);
		tdls_emit_vendor_event(&node->entry);
		qdf_mem_free(node);
		flushed_count++;

		qdf_spin_lock_bh(&stats_ctx->db.lock);
		ln = next_ln;
		next_ln = NULL;
	}

	qdf_spin_unlock_bh(&stats_ctx->db.lock);

	return flushed_count;
}

/* =========================================================================
 * State machine transition and event delivery helpers
 * =========================================================================
 */

/**
 * tdls_stats_sm_transition_to() - Transition the SM to a new state.
 * @stats_ctx: TDLS stats context
 * @state: Target state
 *
 * Wrapper around wlan_sm_transition_to().  The SM engine automatically
 * invokes the exit callback of the current state and the entry callback
 * of the new state.
 */
void tdls_stats_sm_transition_to(struct tdls_stats_context *stats_ctx,
				 enum tdls_stats_sm_state state)
{
	wlan_sm_transition_to(stats_ctx->sm.sm_hdl, state);
}

/* =========================================================================
 * DISABLED state callbacks
 * =========================================================================
 */

/**
 * tdls_stats_state_disabled_entry() - Entry callback for DISABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine when entering the DISABLED state.
 * No caching or forwarding is set up; this state is permanent for the
 * lifetime of the PSOC when FW capability is absent.
 */
static void tdls_stats_state_disabled_entry(void *ctx)
{
	tdls_debug("TDLS stats: entered DISABLED state");
}

/**
 * tdls_stats_state_disabled_exit() - Exit callback for DISABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine before leaving the DISABLED state.
 * No cleanup required; DISABLED is a terminal state and this callback
 * is provided only for SM engine completeness.
 */
static void tdls_stats_state_disabled_exit(void *ctx)
{
	tdls_debug("TDLS stats: exiting DISABLED state");
}

/**
 * tdls_stats_state_disabled_event() - Event handler for DISABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 * @event: Event id (enum tdls_stats_sm_evt)
 * @event_data_len: Length of event data in bytes
 * @event_data: Pointer to event-specific data
 *
 * All events are silently dropped in the DISABLED state.  The state
 * machine enters DISABLED only when FW capability is absent and remains
 * there permanently — no event causes a transition out.
 *
 * Return: true (event consumed / dropped)
 */
static bool tdls_stats_state_disabled_event(void *ctx, uint16_t event,
					    uint16_t event_data_len,
					    void *event_data)
{
	tdls_debug("TDLS stats: dropping event %u in DISABLED state", event);
	return false;
}

/* =========================================================================
 * INIT state callbacks
 * =========================================================================
 */

/**
 * tdls_stats_state_init_entry() - Entry callback for INIT state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine when entering the INIT state.
 * Default caching is now active; subsequent events will be buffered
 * in the cache database until an ENABLE command is received.
 */
static void tdls_stats_state_init_entry(void *ctx)
{
	tdls_debug("TDLS stats: entered INIT state - default caching active");
}

/**
 * tdls_stats_state_init_exit() - Exit callback for INIT state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine before leaving the INIT state.
 * The cache is left intact; the ENABLED entry callback is responsible
 * for flushing it.
 */
static void tdls_stats_state_init_exit(void *ctx)
{
	tdls_debug("TDLS stats: exiting INIT state");
}

/**
 * tdls_stats_state_init_event() - Event handler for INIT state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 * @event: Event id (enum tdls_stats_sm_evt)
 * @event_data_len: Length of event data in bytes
 * @event_data: Pointer to event-specific data
 *
 * Handles events while in the INIT (default caching) state:
 *   EV_ENABLE        - transition to ENABLED immediately
 *   EV_DISABLE       - idempotent; already in default caching mode
 *   EV_NEW_EVENT     - cache the entry in the stats database
 *   EV_FW_STATS      - cache the entry in the stats database
 *   EV_STA_CONNECTED - send WMI enable=1 if single-STA SCC; no state change
 *
 * Return: true if event was handled, false otherwise
 */
static bool tdls_stats_state_init_event(void *ctx, uint16_t event,
					uint16_t event_data_len,
					void *event_data)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	struct tdls_stats_entry *entry;
	bool event_handled = true;
	QDF_STATUS status;

	switch (event) {
	case TDLS_STATS_EV_ENABLE:
		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_S_ENABLED);
		break;

	case TDLS_STATS_EV_DISABLE:
		/* Already in default caching mode — idempotent */
		tdls_debug("TDLS stats: DISABLE in INIT state - no-op");
		break;

	case TDLS_STATS_EV_NEW_EVENT:
	case TDLS_STATS_EV_FW_STATS:
		entry = (struct tdls_stats_entry *)event_data;
		status = tdls_stats_push(&stats_ctx->db, entry);
		if (QDF_IS_STATUS_ERROR(status)) {
			tdls_err("TDLS stats: failed to cache entry, status %d",
				 status);
			event_handled = false;
		}
		break;

	case TDLS_STATS_EV_STA_CONNECTED:
		tdls_stats_handle_sta_connection(
				(struct wlan_objmgr_vdev *)event_data);
		break;

	default:
		event_handled = false;
		break;
	}

	return event_handled;
}

/* =========================================================================
 * ENABLED state callbacks
 * =========================================================================
 */

/**
 * tdls_stats_state_enabled_entry() - Entry callback for ENABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine when entering the ENABLED state.
 * Flushes all entries currently held in the cache database by emitting
 * them as vendor events in FIFO (oldest-first) order, then switches to
 * immediate-forwarding mode for all subsequent events.
 */
static void tdls_stats_state_enabled_entry(void *ctx)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	uint32_t flushed;

	tdls_debug("TDLS stats: entered ENABLED state - flushing cache");

	flushed = tdls_stats_flush_entire_cache(stats_ctx);

	tdls_debug("TDLS stats: cache flush complete, %u records emitted",
		   flushed);
}

/**
 * tdls_stats_state_enabled_exit() - Exit callback for ENABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine before leaving the ENABLED state.
 * No cleanup required.
 */
static void tdls_stats_state_enabled_exit(void *ctx)
{
	tdls_debug("TDLS stats: exiting ENABLED state");
}

/**
 * tdls_stats_state_enabled_event() - Event handler for ENABLED state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 * @event: Event id (enum tdls_stats_sm_evt)
 * @event_data_len: Length of event data in bytes
 * @event_data: Pointer to event-specific data
 *
 * Handles events while in the ENABLED (immediate forwarding) state:
 *   EV_ENABLE        - idempotent; already enabled
 *   EV_DISABLE       - transition back to INIT (resume default caching)
 *   EV_NEW_EVENT     - emit entry immediately as a vendor event
 *   EV_FW_STATS      - emit entry immediately as a vendor event
 *   EV_STA_CONNECTED - send WMI enable=1 if single-STA SCC; no state change
 *
 * Return: true if event was handled, false otherwise
 */
static bool tdls_stats_state_enabled_event(void *ctx, uint16_t event,
					   uint16_t event_data_len,
					   void *event_data)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	struct tdls_stats_entry *entry;
	bool event_handled = true;

	switch (event) {
	case TDLS_STATS_EV_ENABLE:
		/* Already enabled — idempotent */
		tdls_debug("TDLS stats: ENABLE in ENABLED state - no-op");
		break;

	case TDLS_STATS_EV_DISABLE:
		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_S_INIT);
		break;

	case TDLS_STATS_EV_NEW_EVENT:
	case TDLS_STATS_EV_FW_STATS:
		entry = (struct tdls_stats_entry *)event_data;
		tdls_emit_vendor_event(entry);
		break;

	case TDLS_STATS_EV_STA_CONNECTED:
		tdls_stats_handle_sta_connection(
				(struct wlan_objmgr_vdev *)event_data);
		break;

	default:
		event_handled = false;
		break;
	}

	return event_handled;
}

/* =========================================================================
 * State machine info table
 *
 * Each entry maps to one state in enum tdls_stats_sm_state and provides:
 *   - state id
 *   - parent state id  (WLAN_SM_ENGINE_STATE_NONE = no parent / flat SM)
 *   - initial sub-state (WLAN_SM_ENGINE_STATE_NONE = no sub-states)
 *   - has_substates flag
 *   - human-readable state name (used in debug logs)
 *   - entry, exit, and event callbacks
 *
 * Note: non-static so wlan_sm_create() can reference it directly.
 * =========================================================================
 */

struct wlan_sm_state_info tdls_stats_sm_info[] = {
	{
		(uint8_t)TDLS_STATS_S_DISABLED,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		false,
		"TDLS_STATS_DISABLED",
		tdls_stats_state_disabled_entry,
		tdls_stats_state_disabled_exit,
		tdls_stats_state_disabled_event
	},
	{
		(uint8_t)TDLS_STATS_S_INIT,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		false,
		"TDLS_STATS_INIT",
		tdls_stats_state_init_entry,
		tdls_stats_state_init_exit,
		tdls_stats_state_init_event
	},
	{
		(uint8_t)TDLS_STATS_S_ENABLED,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		false,
		"TDLS_STATS_ENABLED",
		tdls_stats_state_enabled_entry,
		tdls_stats_state_enabled_exit,
		tdls_stats_state_enabled_event
	},
	{
		(uint8_t)TDLS_STATS_S_MAX,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		false,
		"INVALID",
		NULL,
		NULL,
		NULL
	},
};

/* =========================================================================
 * Event name table and SM lifecycle APIs
 * =========================================================================
 */

static const char *tdls_stats_sm_event_names[] = {
	"EV_ENABLE",
	"EV_DISABLE",
	"EV_NEW_EVENT",
	"EV_FW_STATS",
	"EV_STA_CONNECTED",
};

/**
 * tdls_stats_sm_create() - Create the TDLS stats state machine.
 * @stats_ctx: TDLS stats context (pre-allocated by caller)
 * @initial_state: Initial SM state (TDLS_STATS_S_INIT or TDLS_STATS_S_DISABLED)
 *
 * Creates the wlan_sm_engine instance, stores the handle in @stats_ctx,
 * and initialises the SM spinlock.  The cache database must be initialised
 * separately by the caller when FW capability is present.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS tdls_stats_sm_create(struct tdls_stats_context *stats_ctx,
				enum tdls_stats_sm_state initial_state)
{
	struct wlan_sm *sm;
	uint8_t name[WLAN_SM_ENGINE_MAX_NAME];

	if (!stats_ctx)
		return QDF_STATUS_E_INVAL;

	qdf_scnprintf(name, sizeof(name), "TDLS-Stats-PSOC:%d",
		      stats_ctx->psoc_id);

	sm = wlan_sm_create(name, stats_ctx, initial_state,
			    tdls_stats_sm_info,
			    QDF_ARRAY_SIZE(tdls_stats_sm_info),
			    tdls_stats_sm_event_names,
			    QDF_ARRAY_SIZE(tdls_stats_sm_event_names));
	if (!sm) {
		tdls_err("TDLS stats state machine creation failed");
		return QDF_STATUS_E_NOMEM;
	}

	stats_ctx->sm.sm_hdl = sm;

	tdls_stats_lock_create(stats_ctx);

	return QDF_STATUS_SUCCESS;
}

/**
 * tdls_stats_sm_destroy() - Destroy the TDLS stats state machine.
 * @stats_ctx: TDLS stats context
 *
 * Destroys the SM spinlock and deletes the wlan_sm_engine instance.
 * The cache database must be deinitialised separately by the caller
 * before invoking this function.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS tdls_stats_sm_destroy(struct tdls_stats_context *stats_ctx)
{
	if (!stats_ctx)
		return QDF_STATUS_E_INVAL;

	tdls_stats_lock_destroy(stats_ctx);
	wlan_sm_delete(stats_ctx->sm.sm_hdl);

	return QDF_STATUS_SUCCESS;
}
