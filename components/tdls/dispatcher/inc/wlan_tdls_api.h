/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * DOC: contains tdls link teardown declarations
 */
 #ifndef _WLAN_TDLS_API_H_
 #define _WLAN_TDLS_API_H_

#include "wlan_objmgr_psoc_obj.h"
#include "wlan_objmgr_pdev_obj.h"
#include "wlan_objmgr_vdev_obj.h"
#include "wlan_tdls_main.h"
#include "wlan_mlo_mgr_public_structs.h"

#ifdef FEATURE_WLAN_TDLS
#ifdef WLAN_FEATURE_11BE_MLO
/**
 * wlan_tdls_is_fw_11be_mlo_capable() - Get TDLS 11be mlo capab
 * @psoc: psoc context
 *
 * Return: True if 11be mlo capable
 */
bool wlan_tdls_is_fw_11be_mlo_capable(struct wlan_objmgr_psoc *psoc);
#else
static inline
bool wlan_tdls_is_fw_11be_mlo_capable(struct wlan_objmgr_psoc *psoc)
{
	return false;
}
#endif
#ifdef FEATURE_SET
/**
 * wlan_tdls_get_features_info() - Get tdls features info
 * @psoc: psoc context
 * @tdls_feature_set: TDLS feature set info structure
 *
 * Return: None
 */

void wlan_tdls_get_features_info(struct wlan_objmgr_psoc *psoc,
				 struct wlan_tdls_features *tdls_feature_set);
#endif

/**
 * wlan_tdls_register_lim_callbacks() - Register callbacks for legacy LIM API
 * @psoc: Pointer to psoc object
 * @cbs: Pointer to callback struct
 *
 * Return: None
 */
void wlan_tdls_register_lim_callbacks(struct wlan_objmgr_psoc *psoc,
				      struct tdls_callbacks *cbs);

/**
 * wlan_tdls_teardown_links() - notify TDLS module to teardown all TDLS links
 * @psoc: psoc object
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_tdls_teardown_links(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_tdls_check_and_teardown_links_sync() - teardown all the TDLS links
 * @psoc: psoc object
 * @vdev: Vdev object pointer
 *
 * Return: None
 */
void wlan_tdls_check_and_teardown_links_sync(struct wlan_objmgr_psoc *psoc,
					     struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_notify_sta_disconnect() - notify sta disconnect
 * @vdev_id: pointer to soc object
 * @lfr_roam: indicate, whether disconnect due to lfr roam
 * @user_disconnect: disconnect from user space
 * @vdev: vdev object manager
 *
 * Notify sta disconnect event to TDLS component
 *
 * Return: QDF_STATUS
 */
void wlan_tdls_notify_sta_disconnect(uint8_t vdev_id,
				     bool lfr_roam, bool user_disconnect,
				     struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_notify_sta_connect() - notify sta connect to TDLS
 * @vdev_id: pointer to soc object
 * @tdls_chan_swit_prohibited: indicates channel switch capability
 * @tdls_prohibited: indicates tdls allowed or not
 * @vdev: vdev object manager
 *
 * Notify sta connect event to TDLS component
 *
 * Return: None
 */
void
wlan_tdls_notify_sta_connect(uint8_t vdev_id,
			     bool tdls_chan_swit_prohibited,
			     bool tdls_prohibited,
			     struct wlan_objmgr_vdev *vdev);

/**
 * wlan_is_tdls_session_present() - Get TDLS session status
 * @vdev: vdev pointer
 *
 * Return: QDF_STATUS_SUCCESS if success; other value if failed
 */
QDF_STATUS
wlan_is_tdls_session_present(struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_update_tx_pkt_cnt() - update tx pkt count
 * @vdev: tdls vdev object
 * @mac_addr: peer mac address
 *
 * Return: None
 */
void wlan_tdls_update_tx_pkt_cnt(struct wlan_objmgr_vdev *vdev,
				 struct qdf_mac_addr *mac_addr);

/**
 * wlan_tdls_update_rx_pkt_cnt() - update rx pkt count
 * @vdev: tdls vdev object
 * @mac_addr: peer mac address
 * @dest_mac_addr: dest mac address
 *
 * Return: None
 */
void wlan_tdls_update_rx_pkt_cnt(struct wlan_objmgr_vdev *vdev,
				 struct qdf_mac_addr *mac_addr,
				 struct qdf_mac_addr *dest_mac_addr);
/**
 * wlan_tdls_notify_start_bss_failure() - Notify TDLS module on start bss
 * failure
 * @psoc: Pointer to PSOC object
 *
 * Return: None
 */
void wlan_tdls_notify_start_bss_failure(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_tdls_notify_start_bss() - Notify TDLS module on start bss
 * @psoc: Pointer to PSOC object
 * @vdev: Vdev object pointer
 *
 * Return: None
 */
void wlan_tdls_notify_start_bss(struct wlan_objmgr_psoc *psoc,
				struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_find_peer() - find TDLS peer in TDLS vdev object
 * @vdev_obj: TDLS vdev object
 * @macaddr: MAC address of peer
 *
 * Return: If peer is found, then it returns pointer to tdls_peer;
 *         otherwise, it returns NULL.
 */
struct tdls_peer *wlan_tdls_find_peer(struct tdls_vdev_priv_obj *vdev_obj,
				      const uint8_t *macaddr);

#ifdef WLAN_FEATURE_TDLS_CONCURRENCIES
/**
 * wlan_tdls_notify_channel_switch_complete() - Notify TDLS module about the
 * channel switch completion
 * @psoc: Pointer to PSOC object
 * @vdev_id: vdev id
 *
 * Return: None
 */
void wlan_tdls_notify_channel_switch_complete(struct wlan_objmgr_psoc *psoc,
					      uint8_t vdev_id);

/**
 * wlan_tdls_notify_channel_switch_start() - Process channel switch start
 * for SAP/P2P GO vdev. For STA vdev, TDLS teardown happens, so explicit
 * disable off channel is not required.
 * @psoc: Pointer to PSOC object
 * @vdev: Pointer to current vdev on which CSA is triggered
 *
 * Return: None
 */
void wlan_tdls_notify_channel_switch_start(struct wlan_objmgr_psoc *psoc,
					   struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_handle_p2p_client_connect() - Handle P2P Client connect start
 * @psoc: Pointer to PSOC object
 * @vdev: Pointer to P2P client vdev
 *
 * Return: None
 */
void wlan_tdls_handle_p2p_client_connect(struct wlan_objmgr_psoc *psoc,
					 struct wlan_objmgr_vdev *vdev);

/**
 * wlan_tdls_recompute_offchannel_mode() - Recompute TDLS offchannel mode
 * related parameters
 * @psoc: Pointer to PSOC object
 * @vdev: Pointer to vdev
 *
 * Return: None
 */
void wlan_tdls_recompute_offchannel_mode(struct wlan_objmgr_psoc *psoc,
					 struct wlan_objmgr_vdev *vdev);
#else
static inline
void wlan_tdls_notify_channel_switch_complete(struct wlan_objmgr_psoc *psoc,
					      uint8_t vdev_id)
{}

static inline
void wlan_tdls_notify_channel_switch_start(struct wlan_objmgr_psoc *psoc,
					   struct wlan_objmgr_vdev *vdev)
{}

static inline
void wlan_tdls_handle_p2p_client_connect(struct wlan_objmgr_psoc *psoc,
					 struct wlan_objmgr_vdev *vdev)
{}
#endif /* WLAN_FEATURE_TDLS_CONCURRENCIES */

/**
 * wlan_tdls_increment_discovery_attempts() - Increment TDLS peer discovery
 * attempts
 * @psoc: Pointer to PSOC object
 * @vdev_id: Vdev id
 * @peer_addr: Peer mac address
 *
 * Return: None
 */
void wlan_tdls_increment_discovery_attempts(struct wlan_objmgr_psoc *psoc,
					    uint8_t vdev_id,
					    uint8_t *peer_addr);

/**
 * wlan_tdls_record_mgmt_tx_complete() - Record a TDLS management frame
 *                                       tx completion as a stats entry.
 * @psoc: PSOC object
 * @vdev_id: vdev/session ID
 * @peer_mac: peer MAC address (6 bytes)
 * @type: stats event type (enum tdls_stats_type)
 * @subtype: stats event subtype (enum tdls_stats_subtype)
 * @success: true if tx completed successfully, false otherwise
 * @reason_code: teardown reason code (enum tdls_stats_reason_code);
 *               use TDLS_STATS_REASON_GENERAL for non-teardown frames
 *
 * Populates a struct tdls_stats_entry with timestamp, peer MAC, RSSI
 * (looked up via tdls_find_peer), session ID, type/subtype, success
 * flag, and reason_code, then delivers it to the TDLS stats SM via
 * TDLS_STATS_EV_NEW_EVENT.
 *
 * Return: None
 */
void wlan_tdls_record_mgmt_tx_complete(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id,
				       const uint8_t *peer_mac,
				       uint8_t type,
				       uint8_t subtype,
				       bool success,
				       uint8_t reason_code);

/**
 * wlan_tdls_stats_entry_fill_vdev_info() - Snapshot dut_mac and link_id
 *                                          into a stats entry.
 * @entry: Stats entry to populate.
 * @psoc:  PSOC used to look up the vdev by entry->session_id.
 *
 * Dispatcher wrapper around tdls_stats_entry_fill_vdev_info().  Must be
 * called while the vdev is still valid (at entry construction time).
 */
void wlan_tdls_stats_entry_fill_vdev_info(struct tdls_stats_entry *entry,
					  struct wlan_objmgr_psoc *psoc);

/**
 * wlan_tdls_stats_entry_find_vdev_info() - Resolve per-peer vdev then
 *                                             fill dut_mac and link_id.
 * @entry: Stats entry whose peer_mac is used to find the correct vdev.
 * @psoc:  PSOC for the psoc-wide peer lookup.
 *
 * Use instead of wlan_tdls_stats_entry_fill_vdev_info() when
 * entry->session_id may be wrong (e.g. FW batch events that carry a single
 * ev->vdev_id for all peers).  Corrects session_id from the peer's actual
 * registered vdev before stamping dut_mac and link_id.
 */
void wlan_tdls_stats_entry_find_vdev_info(struct tdls_stats_entry *entry,
					  struct wlan_objmgr_psoc *psoc);

/**
 * wlan_tdls_teardown_links_for_non_dbs() - notify TDLS module to teardown
 * TDLS links for non-DBS target
 * @psoc: psoc object
 * @vdev_id: Vdev id
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_tdls_teardown_links_for_non_dbs(struct wlan_objmgr_psoc *psoc,
				     uint8_t vdev_id);

/**
 * wlan_tdls_is_addba_request_allowed() - API to check if Add Block ack request
 * is allowed for TDLS peer in current state.
 * @vdev: Vdev object pointer
 * @mac_addr: Mac address of the peer
 *
 * Return: True if ADDBA frame can be allowed
 */
bool wlan_tdls_is_addba_request_allowed(struct wlan_objmgr_vdev *vdev,
					struct qdf_mac_addr *mac_addr);
/*
 * wlan_tdls_delete_all_peers() - Delete all TDLS peers in lim
 * @vdev: Pointer to vdev object
 *
 * Return: None
 */
void wlan_tdls_delete_all_peers(struct wlan_objmgr_vdev *vdev,
				enum wlan_tdls_peer_delete_reason);

/*
 * wlan_tdls_update_peer_kickout_count() - Update the TDLS peer sta kickout
 * count
 * @vdev: Pointer to vdev private object
 * @macaddr: Peer mac address
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_tdls_update_peer_kickout_count(struct wlan_objmgr_vdev *vdev,
					       uint8_t *macaddr);

/**
 * wlan_tdls_is_key_install_allowed() - API to check if key_install request
 * is allowed for TDLS peer in current state.
 * @vdev: Vdev object pointer
 * @mac_addr: Mac address of the peer
 *
 * Return: True if key_install can be allowed
 */
bool wlan_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev,
				      struct qdf_mac_addr *mac_addr);

/**
 * wlan_tdls_process_cmd() - Dispatcher wrapper for the TDLS command processor.
 * @msg: Scheduler message containing the TDLS command type and body pointer.
 *
 * This is the public dispatcher-layer entry point for TDLS scheduler
 * messages posted from external components (e.g. the DP layer via
 * wlan_dp_rx_tdls_packet()).  It is registered as msg.callback in
 * scheduler_post_message() calls and simply forwards to the core handler
 * tdls_process_cmd().
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_tdls_process_cmd(struct scheduler_msg *msg);

#else
static inline
void wlan_tdls_register_lim_callbacks(struct wlan_objmgr_psoc *psoc,
				      struct tdls_callbacks *cbs)
{}

#ifdef FEATURE_SET
static inline
void wlan_tdls_get_features_info(struct wlan_objmgr_psoc *psoc,
				 struct wlan_tdls_features *tdls_feature_set)
{
}
#endif

static inline
bool wlan_tdls_is_fw_11be_mlo_capable(struct wlan_objmgr_psoc *psoc)
{
	return false;
}

static inline QDF_STATUS wlan_tdls_teardown_links(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline void
wlan_tdls_check_and_teardown_links_sync(struct wlan_objmgr_psoc *psoc,
					struct wlan_objmgr_vdev *vdev)
{}

static inline
void wlan_tdls_notify_sta_disconnect(uint8_t vdev_id,
				     bool lfr_roam, bool user_disconnect,
				     struct wlan_objmgr_vdev *vdev)
{}

static inline void
wlan_tdls_notify_sta_connect(uint8_t vdev_id,
			     bool tdls_chan_swit_prohibited,
			     bool tdls_prohibited,
			     struct wlan_objmgr_vdev *vdev) {}

static inline QDF_STATUS
wlan_is_tdls_session_present(struct wlan_objmgr_vdev *vdev)
{
	return QDF_STATUS_E_INVAL;
}

static inline void
wlan_tdls_update_tx_pkt_cnt(struct wlan_objmgr_vdev *vdev,
			    struct qdf_mac_addr *mac_addr)
{
}

static inline
void wlan_tdls_update_rx_pkt_cnt(struct wlan_objmgr_vdev *vdev,
				 struct qdf_mac_addr *mac_addr,
				 struct qdf_mac_addr *dest_mac_addr)
{
}

static inline
void wlan_tdls_notify_start_bss(struct wlan_objmgr_psoc *psoc,
				struct wlan_objmgr_vdev *vdev)
{}

static inline
void wlan_tdls_notify_channel_switch_complete(struct wlan_objmgr_psoc *psoc,
					      uint8_t vdev_id)
{}

static inline
void wlan_tdls_notify_channel_switch_start(struct wlan_objmgr_psoc *psoc,
					   struct wlan_objmgr_vdev *vdev)
{}

static inline
void wlan_tdls_handle_p2p_client_connect(struct wlan_objmgr_psoc *psoc,
					 struct wlan_objmgr_vdev *vdev)
{}

static inline
void wlan_tdls_notify_start_bss_failure(struct wlan_objmgr_psoc *psoc)
{}

static inline
void wlan_tdls_increment_discovery_attempts(struct wlan_objmgr_psoc *psoc,
					    uint8_t vdev_id,
					    uint8_t *peer_addr)
{}

static inline
void wlan_tdls_record_mgmt_tx_complete(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id,
				       const uint8_t *peer_mac,
				       uint8_t type,
				       uint8_t subtype,
				       bool success,
				       uint8_t reason_code)
{}

static inline
void wlan_tdls_stats_entry_fill_vdev_info(struct tdls_stats_entry *entry,
					  struct wlan_objmgr_psoc *psoc)
{}

static inline
void wlan_tdls_stats_entry_find_vdev_info(struct tdls_stats_entry *entry,
					  struct wlan_objmgr_psoc *psoc)
{}

static inline
QDF_STATUS wlan_tdls_teardown_links_for_non_dbs(struct wlan_objmgr_psoc *psoc,
						uint8_t vdev_id)
{
	return QDF_STATUS_SUCCESS;
}

static inline
bool wlan_tdls_is_addba_request_allowed(struct wlan_objmgr_vdev *vdev,
					struct qdf_mac_addr *mac_addr)
{
	return false;
}

static inline
void wlan_tdls_delete_all_peers(struct wlan_objmgr_vdev *vdev,
				uint8_t wlan_tdls_peer_delete_reason)

{}

static inline
QDF_STATUS wlan_tdls_update_peer_kickout_count(struct wlan_objmgr_vdev *vdev,
					       uint8_t *macaddr)
{
	return QDF_STATUS_SUCCESS;
}

static inline
bool wlan_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev,
				      struct qdf_mac_addr *mac_addr)
{
	return false;
}

static inline void
wlan_tdls_recompute_offchannel_mode(struct wlan_objmgr_psoc *psoc,
				    struct wlan_objmgr_vdev *vdev)
{}

static inline QDF_STATUS wlan_tdls_process_cmd(struct scheduler_msg *msg)
{
	return QDF_STATUS_SUCCESS;
}
#endif
#endif
