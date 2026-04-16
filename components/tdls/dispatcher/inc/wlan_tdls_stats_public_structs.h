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
 * DOC: wlan_tdls_stats_public_structs.h
 *
 * TDLS stats public structure definitions.
 *
 * This file contains the public data structures used by the TDLS stats
 * state machine infrastructure, including the primary stats entry type
 * (struct tdls_stats_entry), the cache database, the state machine
 * context, and their associated enumerations.
 *
 * These definitions are shared across the TDLS core, dispatcher, and
 * OS-IF layers.
 */

#ifndef _WLAN_TDLS_STATS_PUBLIC_STRUCTS_H_
#define _WLAN_TDLS_STATS_PUBLIC_STRUCTS_H_

#include <qdf_types.h>
#include <qdf_list.h>
#include <qdf_lock.h>
#include <wlan_objmgr_psoc_obj.h>
#include <wlan_sm_engine.h>

/* =========================================================================
 * Constants / macros
 * =========================================================================
 */

/**
 * TDLS_STATS_NON_MLO_LINK_ID - Sentinel link ID for non-MLO connections.
 *
 * Used in struct tdls_stats_entry::link_id to indicate that the TDLS
 * session is not associated with any specific MLO link.
 */
#define TDLS_STATS_NON_MLO_LINK_ID      0xFF

/**
 * TDLS_STATS_MAX_MCS_COUNTERS - Size of the per-direction MCS histogram arrays.
 *
 * Matches the firmware constant WMI_ENHANCE_STATS_MAX_MCS_COUNTERS (16).
 * Used for struct tdls_stats_entry::tx_mcs_data_ppdu and
 * struct tdls_stats_entry::rx_mcs_data_ppdu.
 */
#define TDLS_STATS_MAX_MCS_COUNTERS     16

/**
 * TDLS_STATS_HIST_MAX_NODES - Maximum number of entries in the stats cache.
 *
 * Defines the capacity of struct tdls_stats_db.  When the cache is full,
 * the oldest entry is evicted before a new one is inserted (FIFO policy).
 */
#define TDLS_STATS_HIST_MAX_NODES       128

/* =========================================================================
 * Enumerations
 * =========================================================================
 */

/**
 * enum tdls_stats_type - TDLS stats event types.
 * @TDLS_STATS_IF_SETUP:        TDLS interface setup event.
 * @TDLS_STATS_DISCOVERY:       TDLS discovery event.
 * @TDLS_STATS_SETUP:           TDLS setup event.
 * @TDLS_STATS_TEARDOWN:        TDLS teardown event.
 * @TDLS_STATS_STATE_CHANGED:   TDLS state-changed event.
 * @TDLS_STATS_DATA:            TDLS periodic data stats event (Type 5).
 *
 * Identifies the category of a TDLS stats entry.  For entries with
 * type != @TDLS_STATS_DATA all Type-5 stats fields in
 * struct tdls_stats_entry are set to zero.
 */
enum tdls_stats_type {
	TDLS_STATS_IF_SETUP       = 0,
	TDLS_STATS_DISCOVERY      = 1,
	TDLS_STATS_SETUP          = 2,
	TDLS_STATS_TEARDOWN       = 3,
	TDLS_STATS_STATE_CHANGED  = 4,
	TDLS_STATS_DATA           = 5,
};

/**
 * enum tdls_stats_subtype - TDLS stats event subtypes.
 * @TDLS_STATS_SUBTYPE_REQ:      Request frame / action.
 * @TDLS_STATS_SUBTYPE_RESP:     Response frame / action.
 * @TDLS_STATS_SUBTYPE_CONFIRM:  Confirm frame / action.
 * @TDLS_STATS_SUBTYPE_COMPLETE: Completion indication.
 * @TDLS_STATS_SUBTYPE_GENERAL:  General / informational event.
 *
 * Qualifies the direction or phase of the event identified by
 * enum tdls_stats_type.
 */
enum tdls_stats_subtype {
	TDLS_STATS_SUBTYPE_REQ      = 0,
	TDLS_STATS_SUBTYPE_RESP     = 1,
	TDLS_STATS_SUBTYPE_CONFIRM  = 2,
	TDLS_STATS_SUBTYPE_COMPLETE = 3,
	TDLS_STATS_SUBTYPE_GENERAL  = 4,
};

/**
 * enum tdls_stats_reason_code - TDLS stats reason codes.
 * @TDLS_STATS_REASON_GENERAL:               General / unspecified reason.
 * @TDLS_STATS_REASON_PEER_UNREACHABLE:      Peer became unreachable.
 * @TDLS_STATS_REASON_TEARDOWN_UNSPECIFIED:  Teardown with unspecified reason.
 * @TDLS_STATS_REASON_INSUFFICIENT_TRAFFIC:  Traffic below teardown threshold.
 * @TDLS_STATS_REASON_NO_TRAFFIC:            No traffic detected.
 * @TDLS_STATS_REASON_ROAMED:                Local STA roamed to a new BSS.
 * @TDLS_STATS_REASON_CONC_SAME_BAND:        Concurrency on the same band.
 * @TDLS_STATS_REASON_CONC_DIFF_BAND:        Concurrency on a different band.
 * @TDLS_STATS_REASON_BT_COEX:              Bluetooth coexistence constraint.
 * @TDLS_STATS_REASON_BSS_CHANNEL_SWITCH:    BSS channel switch (CSA).
 * @TDLS_STATS_REASON_DEAUTH_LEAVING:        Deauthentication / STA leaving.
 * @TDLS_STATS_REASON_UNKNOWN:               Unknown or unrecognised reason.
 *
 * Carried in struct tdls_stats_entry::reason_code.  Applicable primarily
 * to teardown and state-change events.
 */
enum tdls_stats_reason_code {
	TDLS_STATS_REASON_GENERAL               = 0,
	TDLS_STATS_REASON_PEER_UNREACHABLE      = 1,
	TDLS_STATS_REASON_TEARDOWN_UNSPECIFIED  = 2,
	TDLS_STATS_REASON_INSUFFICIENT_TRAFFIC  = 3,
	TDLS_STATS_REASON_NO_TRAFFIC            = 4,
	TDLS_STATS_REASON_ROAMED                = 5,
	TDLS_STATS_REASON_CONC_SAME_BAND        = 6,
	TDLS_STATS_REASON_CONC_DIFF_BAND        = 7,
	TDLS_STATS_REASON_BT_COEX               = 8,
	TDLS_STATS_REASON_BSS_CHANNEL_SWITCH    = 9,
	TDLS_STATS_REASON_DEAUTH_LEAVING        = 10,
	TDLS_STATS_REASON_UNKNOWN               = 255,
};

/**
 * enum tdls_stats_sm_state - TDLS stats state machine states.
 * @TDLS_STATS_S_DISABLED:  FW capability absent; logging permanently disabled.
 *                          No caching, no forwarding.  All events are dropped.
 *                          This is a terminal state — no transitions out.
 * @TDLS_STATS_S_INIT:      Default caching state.  All TDLS events are cached
 *                          in the stats database until an enable command is
 *                          received.
 * @TDLS_STATS_S_ENABLING:  Parent state for the active-forwarding hierarchy.
 *                          Has one sub-state: @TDLS_STATS_SS_ENABLED.
 * @TDLS_STATS_S_MAX:       Sentinel for main states — not a valid state.
 *                          Shares the same integer value as the first
 *                          sub-state (@TDLS_STATS_SS_ENABLED), following
 *                          the same convention used by the TTLM SM.
 * @TDLS_STATS_SS_ENABLED:  Sub-state of @TDLS_STATS_S_ENABLING.
 *                          Active forwarding mode: all TDLS events are
 *                          forwarded immediately as vendor events; no caching.
 *                          On entry the cache is flushed (oldest-first).
 * @TDLS_STATS_SS_MAX:      Sentinel for sub-states — not a valid state.
 */
enum tdls_stats_sm_state {
	TDLS_STATS_S_DISABLED  = 0,
	TDLS_STATS_S_INIT      = 1,
	TDLS_STATS_S_ENABLING  = 2,
	TDLS_STATS_S_MAX       = 3,
	TDLS_STATS_SS_ENABLED  = 4,
	TDLS_STATS_SS_MAX      = 5,
};

/**
 * enum tdls_stats_sm_evt - TDLS stats state machine events.
 * @TDLS_STATS_EV_ENABLE:             User enabled TDLS stats logging.
 * @TDLS_STATS_EV_DISABLE:            User disabled TDLS stats logging.
 * @TDLS_STATS_EV_NEW_EVENT:          A new TDLS stats event occurred (covers
 *                                    all types: control-path Types 0-4 and
 *                                    periodic data stats Type 5).  Used for
 *                                    single-entry delivery.
 * @TDLS_STATS_EV_STA_CONNECTED:      STA connection completed; triggers WMI
 *                                    enable=1 if single-STA SCC condition is
 *                                    met.  Does not cause a state transition.
 * @TDLS_STATS_EV_ENABLE_ACTIVE:      TDLS stats enable is active.
 * @TDLS_STATS_EV_FW_STATS:           Batch of TDLS stats entries from a single
 *                                    FW WMI event (covers both control-path
 *                                    Types 0-4 and periodic data stats Type 5).
 *                                    Event data is a pointer to
 *                                    struct tdls_stats_batch.  The SM loops
 *                                    over all entries under a single lock
 *                                    acquisition, avoiding N separate
 *                                    lock/dispatch/unlock cycles.
 * @TDLS_STATS_EV_MAX:                Sentinel — not a valid event.
 */
enum tdls_stats_sm_evt {
	TDLS_STATS_EV_ENABLE = 0,
	TDLS_STATS_EV_DISABLE,
	TDLS_STATS_EV_NEW_EVENT,
	TDLS_STATS_EV_STA_CONNECTED,
	TDLS_STATS_EV_ENABLE_ACTIVE,
	TDLS_STATS_EV_FW_STATS,
	TDLS_STATS_EV_MAX,
};

/* =========================================================================
 * Structures
 * =========================================================================
 */

/**
 * struct tdls_stats_entry - Single TDLS stats event record.
 * @ts_ms:               Timestamp of the event in milliseconds.
 * @peer_mac:            MAC address of the TDLS peer (6 bytes).
 * @type:                Event type; see enum tdls_stats_type.
 * @subtype:             Event subtype; see enum tdls_stats_subtype.
 * @success:             Outcome flag: 0 = success, 1 = failure.
 * @reason_code:         Reason code; see enum tdls_stats_reason_code.
 * @link_id:             MLO link ID.  Set to %TDLS_STATS_NON_MLO_LINK_ID
 *                       for non-MLO sessions.
 * @rssi:                RSSI in dBm (range -127 to 0).
 * @snr:                 Signal-to-noise ratio.
 * @channel:             Operating channel number.
 * @is_sender:           Sender flag (1 = local STA is the initiator).
 *                       Not applicable for Type-5 entries; set to 0.
 * @data_rate:           Wi-Fi data rate in units of 0.5 Mbps (range 0-9999).
 *                       Applicable for Type-5 entries only; not present in
 *                       wmi_tdls_data_stats and is derived by the host.
 *                       Set to 0 for all other event types.
 * @tx_ppdus_cumulative: Cumulative total TX PPDUs for this TDLS session.
 *                       Sourced from WMI field tx_ppdus_cumulative.
 *                       Set to 0 for non-Type-5 entries.
 * @tx_mcs_data_ppdu:    TX MCS histogram — raw PPDU counts per MCS index
 *                       (array of %TDLS_STATS_MAX_MCS_COUNTERS elements).
 *                       Sourced from WMI field tx_mcs_data_ppdu[].
 *                       Formatted as "N=count,..." strings at vendor-event
 *                       emit time.  All zeros for non-Type-5 entries.
 * @tx_ppdu_failures:    Number of TX PPDUs that failed to be sent.
 *                       Sourced from WMI field tx_ppdu_failures.
 *                       Set to 0 for non-Type-5 entries.
 * @rx_ppdus_cumulative: Cumulative total RX PPDUs for this TDLS session.
 *                       Sourced from WMI field rx_ppdus_cumulative.
 *                       Set to 0 for non-Type-5 entries.
 * @rx_mcs_data_ppdu:    RX MCS histogram — raw PPDU counts per MCS index
 *                       (array of %TDLS_STATS_MAX_MCS_COUNTERS elements).
 *                       Sourced from WMI field rx_mcs_data_ppdu[].
 *                       Formatted as "N=count,..." strings at vendor-event
 *                       emit time.  All zeros for non-Type-5 entries.
 * @rx_ppdu_failures:    Best-effort RX PPDU failure count.
 *                       Sourced from WMI field rx_ppdu_failures.
 *                       Set to 0 for non-Type-5 entries.
 * @session_id:          Per-peer session identifier.  Incremented each time
 *                       the TDLS link with this peer is torn down and
 *                       re-established.
 *
 * This structure holds all data for a single TDLS stats event.  It is
 * the unit of storage in the TDLS stats cache database
 * (struct tdls_stats_db) and the unit passed to the vendor-event emit
 * path.
 *
 * Semantic notes:
 *  - @tx_ppdus_cumulative and @rx_ppdus_cumulative are cumulative totals,
 *    not per-interval deltas.  The per-peer context stores the last
 *    snapshot (last_type5_tx_pkts / last_type5_rx_pkts) for traffic-
 *    detection delta computation.
 *  - MCS histogram arrays (@tx_mcs_data_ppdu / @rx_mcs_data_ppdu) are
 *    stored as raw uint32_t counts and converted to "N=count,..." strings
 *    only at vendor-event emit time.
 *  - For entries with @type != %TDLS_STATS_DATA, all Type-5 stats fields
 *    (@data_rate through @rx_ppdu_failures) are set to 0.
 */
struct tdls_stats_entry {
	uint64_t ts_ms;
	uint8_t  peer_mac[6];
	uint8_t  type;
	uint8_t  subtype;
	uint8_t  success;
	uint8_t  reason_code;
	uint8_t  link_id;
	int16_t  rssi;
	int16_t  snr;
	uint16_t channel;
	uint8_t  is_sender;

	/*
	 * Type-5 periodic data stats fields.
	 * Field names are aligned with the WMI wmi_tdls_data_stats structure.
	 * All fields below are set to 0 for entries with
	 * type != TDLS_STATS_DATA.
	 * MCS arrays are formatted as "N=count,..." strings at emit time.
	 */
	uint16_t data_rate;
	uint32_t tx_ppdus_cumulative;
	uint32_t tx_mcs_data_ppdu[TDLS_STATS_MAX_MCS_COUNTERS];
	uint32_t tx_ppdu_failures;
	uint32_t rx_ppdus_cumulative;
	uint32_t rx_mcs_data_ppdu[TDLS_STATS_MAX_MCS_COUNTERS];
	uint32_t rx_ppdu_failures;

	uint32_t session_id;
};

/**
 * struct tdls_stats_batch - Batch of TDLS stats entries from a single FW event.
 * @num_entries: Number of valid entries in @entries[].
 * @entries:     Pointer to an array of @num_entries stats entries.
 *               The array is owned by the caller and must remain valid for
 *               the entire duration of the synchronous SM dispatch call.
 *
 * Passed as @event_data for %TDLS_STATS_EV_FW_STATS events. The SM
 * state handlers iterate over all @num_entries entries and apply the per-state
 * operation (cache or emit) to each one under a single SM lock acquisition,
 * avoiding N separate lock/unlock cycles when FW delivers N stats in one WMI
 * event.
 */
struct tdls_stats_batch {
	uint32_t                 num_entries;
	struct tdls_stats_entry *entries;
};

/**
 * struct tdls_stats_node - Cache database node.
 * @node: Intrusive list node for embedding in struct tdls_stats_db::list.
 * @entry: The TDLS stats entry data stored at this node.
 *
 * Each node wraps one struct tdls_stats_entry and is linked into the
 * FIFO list maintained by struct tdls_stats_db.
 */
struct tdls_stats_node {
	qdf_list_node_t node;
	struct tdls_stats_entry entry;
};

/**
 * struct tdls_stats_db - TDLS stats cache database.
 * @lock:        Spinlock protecting all cache operations.
 * @max_entries: Maximum number of entries the cache can hold
 *               (%TDLS_STATS_HIST_MAX_NODES).
 * @num_entries: Current number of entries present in the cache.
 * @list:        Doubly-linked list of cached nodes.  Oldest entry is at
 *               the front; newest entry is at the back.
 *
 * Implements a FIFO cache with automatic oldest-first eviction when the
 * cache reaches @max_entries.  All operations must be performed under
 * @lock.
 *
 * The cache is initialised only when FW capability is present
 * (db_initialized == true in struct tdls_stats_context).  Do not access
 * any field of this structure unless db_initialized is true.
 */
struct tdls_stats_db {
	qdf_spinlock_t lock;
	uint32_t       max_entries;
	uint32_t       num_entries;
	qdf_list_t     list;  /* oldest at front, newest at back */
};

/**
 * struct tdls_stats_sm - TDLS stats state machine handle.
 * @sm_hdl:              State machine handle from the wlan_sm_engine
 *                       framework.  This is the authoritative source of
 *                       the current state; use wlan_sm_get_curstate() to
 *                       query it.
 * @tdls_stats_sm_lock:  Spinlock protecting state machine operations.
 *                       Must be held by external callers before invoking
 *                       tdls_stats_sm_deliver_event().
 *
 * Lock ordering: tdls_stats_sm_lock (outer) -> tdls_stats_db::lock (inner).
 * Never acquire tdls_stats_sm_lock while already holding tdls_stats_db::lock.
 */
struct tdls_stats_sm {
	struct wlan_sm *sm_hdl;
	qdf_spinlock_t  tdls_stats_sm_lock;
};

/**
 * struct tdls_stats_context - TDLS stats context (per-PSOC).
 * @psoc:           Back-pointer to the owning PSOC object.
 * @psoc_id:        PSOC identifier, used for debug logging.
 * @sm:             State machine handle (includes the SM lock).
 * @db:             Embedded cache database.  Valid only when
 *                  @db_initialized is true.
 * @db_initialized: True if @db has been successfully initialised.
 *                  Set to false for the DISABLED path (FW capability absent).
 *
 * One instance is allocated per PSOC and is owned by
 * struct wlan_tdls_psoc_obj::stats_ctx.  It is created during PSOC
 * creation and destroyed during PSOC destruction.
 *
 * The current state machine state is maintained exclusively by the
 * wlan_sm_engine; use wlan_sm_get_curstate(sm.sm_hdl) to query it.
 *
 * Initial state is determined once at PSOC creation based on FW service
 * capability:
 * - INI and FW capability present  -> TDLS_STATS_S_INIT
 *				       (cache DB initialised)
 * - INI and FW capability absent   -> TDLS_STATS_S_DISABLED
 *				       (cache DB NOT initialised)
 */
struct tdls_stats_context {
	struct wlan_objmgr_psoc *psoc;
	uint8_t                  psoc_id;
	struct tdls_stats_sm     sm;
	struct tdls_stats_db     db;
	bool                     db_initialized;
};

#endif /* _WLAN_TDLS_STATS_PUBLIC_STRUCTS_H_ */
