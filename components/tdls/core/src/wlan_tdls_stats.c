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
#include <wlan_tdls_cfg_api.h>
#include <wlan_tdls_stats_public_structs.h>
#include <wlan_sm_engine.h>
#include "wlan_tdls_tgt_api.h"
#include "wlan_policy_mgr_public_struct.h"
#include "wlan_policy_mgr_api.h"

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
 * tdls_stats_sm_create() - Allocate and initialise the TDLS stats context
 *                          and state machine.
 * @psoc: PSOC object — used to query FW service capability and stored as
 *        a back-pointer in the allocated context.
 * @stats_ctx_out: Output pointer.  On success, *@stats_ctx_out is set to
 *                 the newly allocated and fully initialised context.
 *                 The caller stores this in the TDLS PSOC object
 *                 (e.g. tdls_soc_priv_obj::stats_ctx).
 *
 * Design note — why a double pointer?
 *   This function owns the allocation of tdls_stats_context.  The double
 *   pointer is the standard C "allocate-and-return" pattern: the function
 *   writes the allocated pointer into *stats_ctx_out so the caller never
 *   needs to pre-allocate or know the size of the context.  The caller
 *   must later pass the returned pointer to tdls_stats_sm_destroy() which
 *   frees it.
 *
 * Initialisation sequence:
 *   1. Allocate tdls_stats_context (includes embedded cache DB).
 *   2. Query wmi_service_tdls_stats_info to pick the initial state:
 *        FW capable   -> TDLS_STATS_S_INIT  (begin caching immediately)
 *        FW incapable -> TDLS_STATS_S_DISABLED (permanent; no caching)
 *   3. Create the wlan_sm_engine instance (triggers initial state entry).
 *   4. Create the SM spinlock.
 *   5. If initial state is INIT, initialise the cache DB.
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

	/* Step 1: Allocate the context (includes the embedded cache DB) */
	stats_ctx = qdf_mem_malloc(sizeof(*stats_ctx));
	if (!stats_ctx) {
		tdls_err("TDLS stats: failed to allocate stats context");
		return QDF_STATUS_E_NOMEM;
	}

	stats_ctx->psoc    = psoc;
	stats_ctx->psoc_id = wlan_psoc_get_id(psoc);
	stats_ctx->db_initialized = false;

	/* Step 2: Determine initial state from INI and FW service capability.
	 * This is a one-time decision at PSOC create time.
	 *   INI and FW capable   -> INIT     (start caching immediately)
	 *   INI OR FW incapable -> DISABLED (permanent terminal state)
	 */
	cfg_tdls_get_stats_enable(psoc, &is_tdls_stats_supported);
	initial_state = is_tdls_stats_supported
				? TDLS_STATS_S_INIT : TDLS_STATS_S_DISABLED;

	/* Step 3: Create the SM engine.
	 * The initial state's entry callback (INIT or DISABLED) does nothing
	 * and does not acquire tdls_stats_sm_lock, so the lock need not exist
	 * yet at this point.
	 */
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

	/* Step 4: Create the SM spinlock (after SM engine, matching
	 * ttlm_sm_create pattern).
	 * Lock order: tdls_stats_sm_lock (outer) -> db.lock (inner).
	 */
	qdf_spinlock_create(&stats_ctx->sm.tdls_stats_sm_lock);

	/* Step 5: Initialise the cache DB only for the INIT path.
	 * The DISABLED path never uses the cache DB.
	 */
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
 * Cleanup order (mirrors ttlm_sm_destroy pattern):
 *   1. Deinit cache DB — only if db_initialized is true (INIT path).
 *      All state exit callbacks do not access the DB, so it is safe to
 *      deinit the DB before deleting the SM engine.
 *   2. Destroy the SM spinlock — all state exit callbacks do not acquire
 *      tdls_stats_sm_lock, so destroying it before wlan_sm_delete() is safe.
 *   3. Delete the SM engine.
 *   4. Free the context itself.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tdls_stats_sm_destroy(struct tdls_stats_context *stats_ctx)
{
	if (!stats_ctx)
		return QDF_STATUS_E_INVAL;

	/* 1. Deinit cache DB only if it was initialised */
	if (stats_ctx->db_initialized) {
		tdls_stats_db_deinit(&stats_ctx->db);
		stats_ctx->db_initialized = false;
	}

	/* 2. Destroy SM spinlock */
	qdf_spinlock_destroy(&stats_ctx->sm.tdls_stats_sm_lock);

	/* 3. Delete SM engine */
	wlan_sm_delete(stats_ctx->sm.sm_hdl);

	/* 4. Free context */
	qdf_mem_free(stats_ctx);

	tdls_debug("TDLS stats SM destroyed");
	return QDF_STATUS_SUCCESS;
}
