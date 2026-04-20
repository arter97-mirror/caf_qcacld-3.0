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
 *   DISABLED   - INI and FW capability absent; all events dropped.
 *   INIT       - Default caching; events buffered in the cache database.
 *   ENABLING   - Intermediate parent state entered on EV_ENABLE from INIT.
 *     SS_ENABLED - Sub-state: events emitted immediately as vendor events;
 *                  entered from ENABLING on a second EV_ENABLE delivery
 */

#include "wlan_tdls_main.h"
#include "wlan_tdls_stats.h"
#include <wlan_tdls_cfg_api.h>
#include <wlan_tdls_stats_public_structs.h>
#include <wlan_sm_engine.h>
#include "wlan_tdls_tgt_api.h"
#include "wlan_policy_mgr_public_struct.h"
#include "wlan_policy_mgr_api.h"
#include "wlan_mlme_main.h"

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
static QDF_STATUS tdls_stats_push(struct tdls_stats_db *db,
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
 * across the vendor-event path.  Called from the SS_ENABLED sub-state entry
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

		tdls_emit_vendor_event(stats_ctx->psoc, &node->entry);
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
 * SM engine via tdls_stats_sm_deliver_event_sync(), then releases the lock.
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
 * The cache is left intact; the SS_ENABLED entry callback is responsible
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
 * Return: true if event was handled, false otherwise
 */
static bool tdls_stats_state_init_event(void *ctx, uint16_t event,
					uint16_t event_data_len,
					void *event_data)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	struct tdls_stats_entry *entry;
	struct tdls_stats_batch *batch;
	bool event_handled = true;
	QDF_STATUS status;
	uint32_t i;

	switch (event) {
	case TDLS_STATS_EV_ENABLE:
		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_S_ENABLING);
		tdls_stats_sm_deliver_event_sync(stats_ctx,
						 TDLS_STATS_EV_ENABLE,
						 0, NULL);
		break;
	case TDLS_STATS_EV_DISABLE:
		/* Already in default caching mode — idempotent */
		tdls_debug("TDLS stats: DISABLE in INIT state - no-op");
		break;
	case TDLS_STATS_EV_NEW_EVENT:
		entry = (struct tdls_stats_entry *)event_data;
		status = tdls_stats_push(&stats_ctx->db, entry);
		if (QDF_IS_STATUS_ERROR(status)) {
			tdls_err("TDLS stats: failed to cache entry, status %d",
				 status);
			event_handled = false;
		}
		break;
	case TDLS_STATS_EV_FW_STATS:
		/*
		 * FW delivered N entries in a single WMI event.  Loop over
		 * all of them here, inside the SM, under the single lock
		 * acquisition that the caller already holds.
		 */
		batch = (struct tdls_stats_batch *)event_data;
		if (!batch || !batch->entries || !batch->num_entries)
			break;
		for (i = 0; i < batch->num_entries; i++) {
			status = tdls_stats_push(&stats_ctx->db,
						 &batch->entries[i]);
			if (QDF_IS_STATUS_ERROR(status))
				tdls_err("TDLS stats: failed to cache batch entry %u, status %d",
					 i, status);
		}
		break;
	case TDLS_STATS_EV_STA_CONNECTED:
		tdls_stats_handle_sta_connection(
				(struct wlan_objmgr_vdev *)event_data);
		break;
	case TDLS_STATS_EV_ENABLE_ACTIVE:
		tdls_debug("TDLS stats: Cache is already flushed");
		break;
	default:
		event_handled = false;
		break;
	}

	return event_handled;
}

/**
 * tdls_stats_state_enabling_entry() - Entry callback for ENABLING state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine when entering the ENABLING parent state.
 * A synchronous EV_ENABLE is delivered by the INIT event handler
 * immediately after this entry, which drives the transition into
 * the SS_ENABLED sub-state.
 */
static void tdls_stats_state_enabling_entry(void *ctx)
{
	tdls_debug("TDLS stats: entered ENABLING state");
}

/**
 * tdls_stats_state_enabling_exit() - Exit callback for ENABLING state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine after the sub-state exit when leaving the
 * ENABLING hierarchy.  No cleanup required.
 */
static void tdls_stats_state_enabling_exit(void *ctx)
{
	tdls_debug("TDLS stats: exiting ENABLING state");
}

/**
 * tdls_stats_state_enabling_event() - Event handler for ENABLING parent state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 * @event: Event id (enum tdls_stats_sm_evt)
 * @event_data_len: Length of event data in bytes
 * @event_data: Pointer to event-specific data
 *
 * Return: true if event was handled, false otherwise
 */
static bool tdls_stats_state_enabling_event(void *ctx, uint16_t event,
					    uint16_t event_data_len,
					    void *event_data)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	struct tdls_stats_entry *entry;
	struct tdls_stats_batch *batch;
	bool event_handled = true;
	struct scheduler_msg msg = {0};
	uint32_t flushed;
	QDF_STATUS status;
	uint32_t i;

	switch (event) {
	case TDLS_STATS_EV_ENABLE:
		msg.callback = tdls_process_cmd;
		msg.type = TDLS_STATS_ENABLE;
		msg.bodyptr = stats_ctx;
		status = scheduler_post_message(QDF_MODULE_ID_TDLS,
						QDF_MODULE_ID_TDLS,
						QDF_MODULE_ID_TARGET_IF, &msg);
		if (QDF_IS_STATUS_ERROR(status)) {
			tdls_err("post TDLS STATS ENABLE/DISABLE fail");
			event_handled = false;
		}
		break;
	case TDLS_STATS_EV_DISABLE:
		flushed = tdls_stats_flush_entire_cache(stats_ctx);
		tdls_debug("TDLS stats: cache flush complete, %u records emitted",
			   flushed);

		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_S_INIT);
		break;
	case TDLS_STATS_EV_ENABLE_ACTIVE:
		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_SS_ENABLED);
		break;
	case TDLS_STATS_EV_NEW_EVENT:
		/*
		 * Cache the entry while the enable transition is in progress,
		 * exactly as the INIT state does.  It will be flushed as a
		 * vendor event when SS_ENABLED is entered.
		 */
		entry = (struct tdls_stats_entry *)event_data;
		status = tdls_stats_push(&stats_ctx->db, entry);
		if (QDF_IS_STATUS_ERROR(status)) {
			tdls_err("TDLS stats: failed to cache entry in ENABLING, status %d",
				 status);
			event_handled = false;
		}
		break;
	case TDLS_STATS_EV_FW_STATS:
		/*
		 * Cache the batch entries while the enable transition is in
		 * progress, exactly as the INIT state does.  They will be
		 * flushed as vendor events when SS_ENABLED is entered.
		 */
		batch = (struct tdls_stats_batch *)event_data;
		if (!batch || !batch->entries || !batch->num_entries)
			break;
		for (i = 0; i < batch->num_entries; i++) {
			status = tdls_stats_push(&stats_ctx->db,
						 &batch->entries[i]);
			if (QDF_IS_STATUS_ERROR(status))
				tdls_err("TDLS stats: failed to cache batch entry %u in ENABLING, status %d",
					 i, status);
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

/**
 * tdls_stats_subst_enabled_entry() - Entry callback for SS_ENABLED sub-state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine when entering the SS_ENABLED sub-state.
 * Flushes all entries currently held in the cache database by emitting
 * them as vendor events in FIFO (oldest-first) order, then switches to
 * immediate-forwarding mode for all subsequent events.
 */
static void tdls_stats_subst_enabled_entry(void *ctx)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	uint32_t flushed;

	tdls_debug("TDLS stats: entered SS_ENABLED sub-state - flushing cache");

	flushed = tdls_stats_flush_entire_cache(stats_ctx);

	tdls_debug("TDLS stats: cache flush complete, %u records emitted",
		   flushed);
}

/**
 * tdls_stats_subst_enabled_exit() - Exit callback for SS_ENABLED sub-state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 *
 * Called by the SM engine before leaving the SS_ENABLED sub-state.
 * No cleanup required.
 */
static void tdls_stats_subst_enabled_exit(void *ctx)
{
	tdls_debug("TDLS stats: exiting SS_ENABLED sub-state");
}

/**
 * tdls_stats_subst_enabled_event() - Event handler for SS_ENABLED sub-state.
 * @ctx: TDLS stats context (struct tdls_stats_context *)
 * @event: Event id (enum tdls_stats_sm_evt)
 * @event_data_len: Length of event data in bytes
 * @event_data: Pointer to event-specific data
 *
 * Return: true if event was handled, false otherwise
 */
static bool tdls_stats_subst_enabled_event(void *ctx, uint16_t event,
					   uint16_t event_data_len,
					   void *event_data)
{
	struct tdls_stats_context *stats_ctx =
				(struct tdls_stats_context *)ctx;
	struct tdls_stats_entry *entry;
	struct tdls_stats_batch *batch;
	bool event_handled = true;
	uint32_t i;

	switch (event) {
	case TDLS_STATS_EV_ENABLE:
		/* Already enabled — idempotent */
		tdls_debug("TDLS stats: ENABLE in SS_ENABLED sub-state - no-op");
		break;
	case TDLS_STATS_EV_DISABLE:
		tdls_stats_sm_transition_to(stats_ctx, TDLS_STATS_S_INIT);
		break;
	case TDLS_STATS_EV_NEW_EVENT:
		entry = (struct tdls_stats_entry *)event_data;
		tdls_emit_vendor_event(stats_ctx->psoc, entry);
		break;
	case TDLS_STATS_EV_FW_STATS:
		/*
		 * FW delivered N entries in a single WMI event.  Loop over
		 * all of them here, inside the SM, under the single lock
		 * acquisition that the caller already holds.
		 */
		batch = (struct tdls_stats_batch *)event_data;
		if (!batch || !batch->entries || !batch->num_entries)
			break;
		for (i = 0; i < batch->num_entries; i++)
			tdls_emit_vendor_event(stats_ctx->psoc,
					       &batch->entries[i]);
		break;
	case TDLS_STATS_EV_STA_CONNECTED:
		tdls_stats_handle_sta_connection(
				(struct wlan_objmgr_vdev *)event_data);
		break;
	case TDLS_STATS_EV_ENABLE_ACTIVE:
		tdls_debug("TDLS stats: ENABLE_ACTIVE in SS_ENABLED sub-state - no-op");
		break;
	default:
		event_handled = false;
		break;
	}

	return event_handled;
}

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
		(uint8_t)TDLS_STATS_S_ENABLING,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		true,
		"TDLS_STATS_ENABLING",
		tdls_stats_state_enabling_entry,
		tdls_stats_state_enabling_exit,
		tdls_stats_state_enabling_event
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
	{
		(uint8_t)TDLS_STATS_SS_ENABLED,
		(uint8_t)TDLS_STATS_S_ENABLING,
		(uint8_t)WLAN_SM_ENGINE_STATE_NONE,
		false,
		"TDLS_STATS_SS_ENABLED",
		tdls_stats_subst_enabled_entry,
		tdls_stats_subst_enabled_exit,
		tdls_stats_subst_enabled_event
	},
};

static const char *tdls_stats_sm_event_names[] = {
	"EV_ENABLE",
	"EV_DISABLE",
	"EV_NEW_EVENT",
	"EV_STA_CONNECTED",
	"EV_ENABLE_ACTIVE",
	"EV_FW_STATS",
};

/**
 * tdls_stats_handle_sta_connection() - Handle a STA-connected event.
 * @vdev: VDEV on which the STA connection completed.
 *
 * Called from the TDLS stats state machine when TDLS_STATS_EV_STA_CONNECTED
 * is delivered (from both INIT and SS_ENABLED states).
 *
 * Checks the current connection state via the policy manager:
 *   - Single STA connection with SCC (no MCC): sends WMI enable=1 to start
 *     FW TDLS stats collection.
 *   - Multiple STAs or MCC active: no WMI command sent; FW handles disable
 *     automatically on disconnect/MCC transitions.
 *
 * No state machine transitions are performed — the state machine is driven
 * independently by GETTDLSINFO enable/disable user commands.
 */
void tdls_stats_handle_sta_connection(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_psoc *psoc;
	uint32_t sta_count;
	bool is_mcc;
	QDF_STATUS status;

	if (!vdev) {
		tdls_err("TDLS stats: vdev is NULL in sta_connection handler");
		return;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		tdls_err("TDLS stats: psoc is NULL in sta_connection handler");
		return;
	}

	/*
	 * Query current STA connection count and MCC status via the policy
	 * manager.  These are the same APIs used by tdls_set_ct_mode() and
	 * tdls_is_concurrency_allowed() for consistency.
	 */
	sta_count = policy_mgr_mode_specific_connection_count(psoc,
							      PM_STA_MODE,
							      NULL);
	is_mcc = policy_mgr_is_mcc_on_any_sta_vdev(psoc);

	if (sta_count == 1 && !is_mcc) {
		/*
		 * Single STA, SCC condition met — send WMI enable=1 to start
		 * FW TDLS stats collection.  Host never sends enable=0; FW
		 * handles disable automatically on disconnect/MCC.
		 */
		tdls_debug("TDLS stats: STA connected SCC (sta_count=%u, mcc=%d, vdev_id=%u), sending WMI enable=1",
			   sta_count, is_mcc, wlan_vdev_get_id(vdev));
		status = tgt_tdls_request_stats_info(psoc,
						     wlan_vdev_get_id(vdev),
						     1);
		if (QDF_IS_STATUS_ERROR(status))
			tdls_err("TDLS stats: WMI request_stats_info enable=1 failed, status %d",
				 status);
	} else {
		/*
		 * MCC or multiple STAs — no WMI command.  FW handles disable
		 * automatically; no state machine transition needed.
		 */
		tdls_debug("TDLS stats: STA connected but not SCC (sta_count=%u, mcc=%d) - no WMI cmd",
			   sta_count, is_mcc);
	}
}

/**
 * tdls_emit_vendor_event() - Emit a single stats entry as a vendor event.
 * @psoc:  PSOC object used to look up the registered OS-IF callback.
 * @entry: Stats entry to emit.
 *
 * Retrieves the tdls_stats_emit_cb registered by the OS-IF layer (HDD) via
 * wlan_tdls_register_stats_emit_cb() and invokes it.  This keeps the TDLS
 * component free of any direct dependency on HDD headers.
 *
 * If no callback has been registered the entry is silently dropped.
 */
void tdls_emit_vendor_event(struct wlan_objmgr_psoc *psoc,
			    const struct tdls_stats_entry *entry)
{
	struct tdls_soc_priv_obj *soc_obj;

	if (!psoc || !entry)
		return;

	soc_obj = wlan_psoc_get_tdls_soc_obj(psoc);
	if (!soc_obj) {
		tdls_err("TDLS stats: soc_obj is NULL, dropping entry");
		return;
	}

	if (!soc_obj->stats_emit_cb) {
		tdls_debug("TDLS stats: no emit callback registered, dropping entry");
		return;
	}

	soc_obj->stats_emit_cb(soc_obj->soc, entry);
}

/**
 * tdls_stats_sm_create() - Allocate and initialise the TDLS stats context
 *                          and state machine.
 * @psoc: PSOC object — used to query FW service capability and stored as
 *        a back-pointer in the allocated context.
 * @stats_ctx_out: Output pointer.  On success, *@stats_ctx_out is set to
 *                 the newly allocated and fully initialised context.
 *                 The caller stores this in the TDLS PSOC object
 *                 (e.g. tdls_soc_priv_obj::stats_ctx).
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_create(struct wlan_objmgr_psoc *psoc,
				struct tdls_stats_context **stats_ctx_out)
{
	struct tdls_stats_context *stats_ctx;
	struct wlan_sm *sm;
	uint8_t name[WLAN_SM_ENGINE_MAX_NAME];
	enum tdls_stats_sm_state initial_state;
	QDF_STATUS status;
	bool is_tdls_stats_supported;

	if (!psoc || !stats_ctx_out)
		return QDF_STATUS_E_INVAL;

	stats_ctx = qdf_mem_malloc(sizeof(*stats_ctx));
	if (!stats_ctx) {
		tdls_err("TDLS stats: failed to allocate stats context");
		return QDF_STATUS_E_NOMEM;
	}

	stats_ctx->psoc = psoc;
	stats_ctx->psoc_id = wlan_psoc_get_id(psoc);
	stats_ctx->db_initialized = false;

	cfg_tdls_get_stats_enable(psoc, &is_tdls_stats_supported);
	initial_state = is_tdls_stats_supported
				? TDLS_STATS_S_INIT : TDLS_STATS_S_DISABLED;

	qdf_scnprintf(name, sizeof(name), "TDLS-Stats");

	sm = wlan_sm_create(name, stats_ctx, initial_state,
			    tdls_stats_sm_info,
			    QDF_ARRAY_SIZE(tdls_stats_sm_info),
			    tdls_stats_sm_event_names,
			    QDF_ARRAY_SIZE(tdls_stats_sm_event_names));
	if (!sm) {
		tdls_err("TDLS stats: state machine creation failed");
		qdf_mem_free(stats_ctx);
		return QDF_STATUS_E_NOMEM;
	}
	stats_ctx->sm.sm_hdl = sm;

	qdf_spinlock_create(&stats_ctx->sm.tdls_stats_sm_lock);

	if (initial_state == TDLS_STATS_S_INIT) {
		status = tdls_stats_db_init(&stats_ctx->db,
					    TDLS_STATS_HIST_MAX_NODES);
		if (QDF_IS_STATUS_ERROR(status)) {
			tdls_err("TDLS stats: cache DB init failed, status %d",
				 status);
			qdf_spinlock_destroy(&stats_ctx->sm.tdls_stats_sm_lock);
			wlan_sm_delete(stats_ctx->sm.sm_hdl);
			qdf_mem_free(stats_ctx);
			return status;
		}
		stats_ctx->db_initialized = true;
	}

	*stats_ctx_out = stats_ctx;

	tdls_debug("TDLS stats SM created, psoc %d, initial state %d",
		   stats_ctx->psoc_id, initial_state);
	return QDF_STATUS_SUCCESS;
}

/**
 * tdls_stats_sm_destroy() - Destroy the TDLS stats state machine and free
 *                           all associated resources.
 * @stats_ctx: TDLS stats context returned by tdls_stats_sm_create().
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_destroy(struct tdls_stats_context *stats_ctx)
{
	if (!stats_ctx)
		return QDF_STATUS_E_INVAL;

	if (stats_ctx->db_initialized) {
		tdls_stats_db_deinit(&stats_ctx->db);
		stats_ctx->db_initialized = false;
	}

	qdf_spinlock_destroy(&stats_ctx->sm.tdls_stats_sm_lock);

	wlan_sm_delete(stats_ctx->sm.sm_hdl);

	qdf_mem_free(stats_ctx);

	tdls_debug("TDLS stats SM destroyed");
	return QDF_STATUS_SUCCESS;
}

/**
 * tdls_stats_enable_cmd() - Core handler for the TDLS_STATS_ENABLE scheduler
 *                           message.
 * @stats_ctx: TDLS stats context obtained from the scheduler message bodyptr.
 *
 * Called from the scheduler thread (via tdls_process_cmd) when a
 * TDLS_STATS_ENABLE message is dequeued.  Delivers TDLS_STATS_EV_ENABLE to
 * the SM only when the SM is in the ENABLING parent state, i.e. waiting for
 * the scheduler-thread confirmation to enter SS_ENABLED.  All other states
 * are either already active (SS_ENABLED — no-op) or stale (INIT / DISABLED —
 * ignore).
 *
 * Lock note: uses tdls_stats_sm_deliver_event() which acquires
 * tdls_stats_sm_lock internally; must NOT be called while that lock is held.
 */
void tdls_stats_enable_cmd(struct tdls_stats_context *stats_ctx)
{
	if (!stats_ctx)
		return;

	tdls_stats_sm_deliver_event(stats_ctx, TDLS_STATS_EV_ENABLE_ACTIVE,
				    0, NULL);
}

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
			       bool enable)
{
	enum tdls_stats_sm_state state;

	if (!stats_ctx)
		return QDF_STATUS_E_INVAL;

	state = wlan_sm_get_current_state(stats_ctx->sm.sm_hdl);

	if (!enable && (state == TDLS_STATS_S_DISABLED ||
			state == TDLS_STATS_S_INIT))
		return QDF_STATUS_SUCCESS;

	if (enable && (state == TDLS_STATS_S_ENABLING ||
		       state == TDLS_STATS_SS_ENABLED))
		return QDF_STATUS_SUCCESS;

	return tdls_stats_sm_deliver_event(stats_ctx,
					   enable ? TDLS_STATS_EV_ENABLE
						  : TDLS_STATS_EV_DISABLE,
					   0, NULL);
}

void tdls_stats_record_peer_add(struct tdls_soc_priv_obj *soc_obj,
				struct wlan_objmgr_vdev *vdev,
				const uint8_t *macaddr,
				int8_t rssi)
{
	struct tdls_stats_entry entry = {0};

	if (!soc_obj || !vdev || !macaddr)
		return;

	if (!soc_obj->stats_ctx)
		return;

	entry.ts_ms       = qdf_get_time_of_the_day_ms();
	qdf_mem_copy(entry.peer_mac, macaddr, QDF_MAC_ADDR_SIZE);
	entry.type        = TDLS_STATS_IF_SETUP;
	entry.subtype     = TDLS_STATS_SUBTYPE_GENERAL;
	entry.is_sender   = 0;
	entry.reason_code = TDLS_STATS_REASON_GENERAL;
	entry.session_id  = wlan_vdev_get_id(vdev);
	entry.rssi        = rssi;
	entry.channel     = wlan_get_operation_chan_freq(vdev);

	tdls_stats_sm_deliver_event(soc_obj->stats_ctx,
				    TDLS_STATS_EV_NEW_EVENT,
				    sizeof(entry), &entry);
}
