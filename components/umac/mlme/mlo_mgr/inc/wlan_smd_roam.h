/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/*
 * DOC: contains Simultaneous Multi Link Device Roam public file containing
 * roaming functionality
 */
#ifndef _WLAN_SMD_ROAM_H_
#define _WLAN_SMD_ROAM_H_

#include <wlan_mlo_mgr_cmn.h>
#include <wlan_mlo_mgr_public_structs.h>
#include <wlan_cm_roam_public_struct.h>
#include <../../core/src/wlan_cm_roam_i.h>
#include <wlan_mlo_link_recfg.h>

#ifdef WLAN_FEATURE_11BN_SMD

/* SMD removed link flag bit position for link_status_flags in
 * struct mlo_link_info
 * If link is deleted from setup links by SMD roaming, the link
 * will be handled by similar behaviour as link removed.
 */
#define LS_SMD_LNK_REMOVE_BIT 1

/**
 * enum smd_prep_status - Overall SMD ST Prep phase status reported to FW
 * @SMD_PREP_STATUS_SUCCESS:            ST Prep succeeded; proceed to ST Exec
 * @SMD_PREP_STATUS_UNSPECIFIC_FAIL:    Unspecified failure during ST Prep
 * @SMD_PREP_STATUS_VDEV_REPURPOSE_FAIL: VDEV repurpose operation failed
 * @SMD_PREP_STATUS_PREP_REQ_TX_NO_ACK: PREP Request frame was not acknowledged
 * @SMD_PREP_STATUS_PREP_RESP_RX_TIMEOUT: PREP Response not received in time
 */
enum smd_prep_status {
	SMD_PREP_STATUS_SUCCESS             = 0,
	SMD_PREP_STATUS_UNSPECIFIC_FAIL     = 1,
	SMD_PREP_STATUS_VDEV_REPURPOSE_FAIL = 2,
	SMD_PREP_STATUS_PREP_REQ_TX_NO_ACK  = 3,
	SMD_PREP_STATUS_PREP_RESP_RX_TIMEOUT = 4,
};

/**
 * smd_fw_roam_start - Handler for SMD roam start event handler
 *
 * @vdev: vdev pointer
 *
 * This api will be called from CM layer,handles SMD roam
 * start.
 *
 * Return: qdf status
 */
QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev);

/**
 * smd_start_link_recfg() - Section 6.2: Initialize SMD roaming in Link Recfg SM
 * @vdev: VDEV object
 * @roam_event: Roam event structure
 *
 * This function initializes SMD roaming in Link Reconfiguration SM.
 * It populates link_recfg_context from roam_offload_roam_event
 * (target AP links),and posts WLAN_LINK_RECFG_EV_SMD_ROAM_START
 * event to Link Recfg SM using mlo_link_recfg_sm_deliver_event().
 *
 * Return: QDF_STATUS
 */
QDF_STATUS smd_start_link_recfg(struct wlan_objmgr_vdev *vdev,
				struct roam_offload_roam_event *roam_event);

/**
 * smd_create_link_recfg_transition_list() - Create transition list
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request
 *
 * This function creates the link transition list from vdev_repurpose_req.
 * It parses vdev_repurpose_req for each link, creates link transition
 * descriptors, determines add/delete operations,
 * and stores in link_recfg_context.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_create_link_recfg_transition_list(
				struct mlo_link_recfg_context *recfg_ctx,
				struct wlan_mlo_link_recfg_req *recfg_req);

/**
 * smd_link_recfg_assign_self_link_addr() - Assign link addresses for target AP
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request
 * @del_link_set: Bitmap of links to be deleted
 * @first_del_link_set_no_common: Pointer to first delete link set with no common link
 *
 * Extracts link addresses from vdev_repurpose_req and assigns to target AP's
 * sta_ctx->links_info[] array in prepared_targets[0].
 *
 * Called by Link Recfg SM when handling
 * WLAN_LINK_RECFG_EV_SMD_ROAM_START event.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_link_recfg_assign_self_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			uint32_t del_link_set,
			uint32_t *first_del_link_set_no_common);

/**
 * smd_roam_in_progress() - API to check is SMD roaming ongoing
 * @recfg_ctx: Link reconfiguration context
 *
 * API to check if SMD roaming is in progress
 *
 *
 * Return: boolean value true or false
 */
bool
smd_roam_in_progress(struct mlo_link_recfg_context *recfg_ctx);

/**
 * smd_uhr_link_recfg_send_request_frame() - API to send Link Recfg request
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request
 *
 * API to send UHR Link Reconfiguration ST prep request
 *
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_uhr_link_recfg_send_request_frame(
		struct mlo_link_recfg_context *recfg_ctx,
		struct mlo_link_recfg_state_req *req);

/**
 * smd_get_roam_sync_timeout() - Get serialization timeout for FW roam command
 * @vdev: VDEV object
 *
 * Returns FW_SMD_ROAM_SYNC_TIMEOUT when SMD roaming is in progress on
 * the vdev, FW_ROAM_SYNC_TIMEOUT otherwise.
 *
 * Return: timeout in milliseconds
 */
uint32_t
smd_get_roam_sync_timeout(struct wlan_objmgr_vdev *vdev);

/**
 * smd_find_first_accepted_link() - Find first accepted link from ST Prep response
 * @recfg_ctx: Link reconfiguration context
 * @tran: Link reconfiguration state transition
 *
 * This function iterates through the add_link_info array in the transition
 * request and returns the first link with status_code == STATUS_SUCCESS.
 * This indicates the link was accepted by the target AP in the M2 response.
 *
 * Return: Pointer to first accepted wlan_mlo_link_recfg_bss_info, or NULL if
 *         no accepted links found
 */
struct wlan_mlo_link_recfg_bss_info *
smd_find_first_accepted_link(struct mlo_link_recfg_context *recfg_ctx,
			     struct mlo_link_recfg_state_tran *tran);

/**
 * smd_st_prep_response_received() - API to handle UHR Link Recfg rsp
 * @recfg_ctx: Link reconfiguration context
 * @tran: Link reconfiguration state tran pointer
 *
 * API to handle UHR Link Reconfiguration ST prep response
 *
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct mlo_link_recfg_state_tran *tran);

/**
 * smd_link_recfg_complete() - API for SMD link recfg complete
 * @recfg_ctx: Link reconfiguration context
 * @success: boolean flag
 *
 * API for handle SMD UHR Link Reconfiguration complete
 *
 *
 * Return: void
 */
void
smd_link_recfg_complete(struct mlo_link_recfg_context *recfg_ctx,
			bool success);

/**
 * smd_link_recfg_del_link_completed() - API for SMD link recfg del complete
 * @recfg_ctx: Link reconfiguration context
 *
 * API for SMD link reconfig del link complete handler
 *
 *
 * Return: void
 */
void
smd_link_recfg_del_link_completed(struct mlo_link_recfg_context *recfg_ctx);

/**
 * smd_host_link_switch_validate_request() - API to validate
 * SMD roaming link switch
 * @vdev: Vdev pointer
 * @req: Link switch req pointer
 *
 * API to validate SMD roaming host triggered link switch
 *
 * Return: QDF_STATUS success or failure
 */

QDF_STATUS
smd_host_link_switch_validate_request(struct wlan_objmgr_vdev *vdev,
				      struct wlan_mlo_link_switch_req *req);

/**
 * smd_get_prepared_ap_link_info() - Get the pointer of link info matching
 * AP mac addr/bssid.
 * @vdev: VDEV object manager.
 * @ap_link_addr: Pointer to AP BSSID MAC address.
 *
 * Returns the pointer to link info data structure matching with AP mac address
 * field.
 *
 * Return: Valid pointer on match or else %NULL
 */
struct mlo_link_info *
smd_get_prepared_ap_link_info(struct wlan_objmgr_vdev *vdev,
			      struct qdf_mac_addr *ap_link_addr);

/**
 * smd_get_prep_ap_link_info() - Wrapper to get link info for a link switch req.
 * @vdev: VDEV object manager.
 * @req: Pointer to link switch request.
 *
 * Return: Valid pointer on match or else %NULL
 */
struct mlo_link_info *
smd_get_prep_ap_link_info(struct wlan_objmgr_vdev *vdev,
			  struct wlan_mlo_link_switch_req *req);

/**
 * smd_roam_prep_complete() - Send SMD roam start status to FW on prep success
 * @recfg_ctx: Link Recfg ctx pointer
 * @req: Link recfg state req pointer (tran->req with updated status codes)
 *
 * Thin wrapper around smd_send_roam_start_status_cmd() called when ST Prep
 * completes successfully. Always passes SMD_PREP_STATUS_SUCCESS.
 *
 * Return: QDF_STATUS success or failure
 */
QDF_STATUS
smd_roam_prep_complete(struct mlo_link_recfg_context *recfg_ctx,
		       struct mlo_link_recfg_state_req *req);

/**
 * smd_add_prepared_target_links_in_smd_ctx() - Add prepared target links to SMD context
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request
 *
 * This function updates the SMD context with accepted target AP links from the
 * ST Prep response. It filters links by status code (STATUS_SUCCESS only),
 * populates target_bss_ctx->links_info[] array with link information including
 * AP link address, self link address, vdev_id, link_id, channel frequency,
 * and NSS capabilities. Updates smd_ctx->prepared_targets array and increments
 * num_prepared counter.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_add_prepared_target_links_in_smd_ctx(
					struct mlo_link_recfg_context *recfg_ctx,
					struct mlo_link_recfg_state_req *req);

/**
 * smd_link_recfg_has_active_vdev_for_add_link() - Check if active vdev exists for add link
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request
 * @link_sw_req: Link switch request to be populated
 *
 * This function checks if any of the target AP's accepted add links has an active
 * vdev (connected on an old deleted link). If found, it triggers a host-initiated
 * link switch with reason MLO_LINK_SWITCH_REASON_HOST_ADD_LINK to disconnect and
 * reconnect the vdev to the new link. The function also handles link rejection
 * scenarios where a rejected link's vdev can be reassigned to an accepted link.
 *
 * Return: true if active vdev found and link_sw_req populated, false otherwise
 */
bool
smd_link_recfg_has_active_vdev_for_add_link(
				struct mlo_link_recfg_context *recfg_ctx,
				struct mlo_link_recfg_state_req *req,
				struct wlan_mlo_link_switch_req *link_sw_req);

/**
 * smd_cleanup_curr_ap_link() - Prepare link switch to tear down cleanup vdev link
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request (del_link_info carries cleanup vdev link)
 * @link_sw_req: Link switch request to be populated
 *
 * Handles the multi-link to single-link SMD roaming case where add_link_info is empty
 * but del_link_info identifies the vdev to disconnect. Matches del_link entries against
 * sta_ctx->links_info[] by link_id and ap_link_addr. Prepares a link switch request
 * with MLO_LINK_SWITCH_REASON_SMD_ROAM_REMOVE_LINK to disconnect without reconnecting.
 *
 * Return: true if cleanup link found and link_sw_req populated, false otherwise
 */
bool
smd_cleanup_curr_ap_link(struct mlo_link_recfg_context *recfg_ctx,
			 struct mlo_link_recfg_state_req *req,
			 struct wlan_mlo_link_switch_req *link_sw_req);

/**
 * smd_roam_link_switch_disconnect_done() - Handle link switch disconnect
 *                                          completion during SMD roaming
 * @vdev: vdev on which the link switch disconnect completed
 * @mlo_dev_ctx: MLO device context
 *
 * Handles post-disconnect processing for SMD-initiated link switches.
 * For REMOVE_LINK advances FSM to COMPLETE_SUCCESS; for HOST_ADD_LINK
 * looks up the prepared target AP link and proceeds to MAC addr change.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_roam_link_switch_disconnect_done(struct wlan_objmgr_vdev *vdev,
				     struct wlan_mlo_dev_context *mlo_dev_ctx);

/**
 * smd_fw_roam_sync() - Handle firmware roam sync indication for SMD roaming
 * @vdev: Assoc VDEV object on which FW roam sync was received
 * @sync_ind: Roam offload sync indication from firmware carrying vdev
 *            repurpose requests and target AP link information
 *
 * Called from cm_fw_roam_sync_req() when wlan_is_smd_roam_sync() returns true.
 * Processes vdev repurpose requests from the sync indication, determines the
 * roaming scenario (multi-to-single or multi-to-multi link), and populates
 * cached_recfg_req with add/delete link information for the Link Recfg SM.
 * For multi-to-single roaming, clears add_link_info and populates del_link_info.
 * For multi-to-multi roaming, builds both add and delete link lists based on
 * current and target AP link information. Returns QDF_STATUS_E_PENDING when SM
 * execution is deferred; caller must invoke smd_trigger_link_recfg_sm() after
 * releasing the CM lock.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_PENDING if SM execution
 *         is deferred, or error code otherwise
 */
QDF_STATUS
smd_fw_roam_sync(struct wlan_objmgr_vdev *vdev,
		 struct roam_offload_synch_ind *sync_ind);

/**
 * wlan_is_smd_roam_sync() - Check if a ROAM_SYNC event is for SMD execution
 * @sync_ind: Roam offload sync indication from firmware
 *
 * Returns true when the sync indication carries vdev repurpose requests,
 * meaning SMD execution must handle the link switches. The LFR3 per-link
 * sync path must be skipped in this case.
 *
 * Return: true if SMD roam sync, false otherwise
 */
bool
wlan_is_smd_roam_sync(struct roam_offload_synch_ind *sync_ind);

/**
 * wlan_smd_roam_sync_status() - Normalize status after cm_fw_roam_sync_req()
 * @status: Status returned by cm_fw_roam_sync_req()
 *
 * E_PENDING from cm_fw_roam_sync_req() means SMD async execution started
 * successfully. Convert it to SUCCESS so mlo_fw_roam_sync_req() does not
 * clear the link bmap and does not unblock PM prematurely.
 *
 * Return: QDF_STATUS_SUCCESS if status was E_PENDING, original status otherwise
 */
QDF_STATUS
wlan_smd_roam_sync_status(QDF_STATUS status);

/**
 * smd_trigger_link_recfg_sm() - Trigger Link Recfg SM after CM lock release
 * @vdev: Assoc vdev pointer
 *
 * Called from cm_fw_roam_sync_req() after cm_sm_deliver_event() returns
 * E_PENDING, at which point the CM SM lock is already released. Delivers
 * WLAN_LINK_RECFG_SM_EV_SMD_ROAM_START using the cached_recfg_req that was
 * built by smd_fw_roam_sync() under the CM lock.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_trigger_link_recfg_sm(struct wlan_objmgr_vdev *vdev);

/**
 * smd_handle_cm_roam_sync_pending() - Handle E_PENDING from cm_sm_deliver_event
 * @vdev: Assoc vdev pointer
 * @status: Status returned by cm_sm_deliver_event(ROAM_SYNC)
 *
 * Called in cm_fw_roam_sync_req() immediately after cm_sm_deliver_event()
 * returns (CM lock is already released at this point). If status is
 * E_PENDING it means smd_fw_roam_sync() deferred SM execution; this
 * function checks smd_roam_in_progress and calls smd_trigger_link_recfg_sm().
 *
 * Return: true if E_PENDING was handled (caller should skip RSO stop and
 *         return), false otherwise.
 */
bool
smd_handle_cm_roam_sync_pending(struct wlan_objmgr_vdev *vdev,
				QDF_STATUS status);

/**
 * smd_exec_complete() - Complete SMD roaming execution
 * @psoc: Psoc pointer
 * @recfg_ctx: Link reconfiguration context
 *
 * Called from the EV_SMD_ROAM_COMPLETED handler after the last ADD_LINK
 * completes. Clears SMD flags, frees cached_sync_ind, and delivers
 * WLAN_CM_SM_EV_ROAM_DONE to the assoc vdev to transition it to CONNECTED.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_exec_complete(struct wlan_objmgr_psoc *psoc,
		  struct mlo_link_recfg_context *recfg_ctx);

/**
 * smd_roam_link_recfg_set_tx_link_addr() - Set transmit link address for link reconfiguration
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request
 * @req: Link reconfiguration state request to be updated with peer MAC
 * @candidate_link_set: Bitmap of candidate links for transmission
 *
 * This function determines which link should be used to send the link
 * reconfiguration action frame during SMD roaming. It iterates through
 * the MLO device's link information, filters out links marked for AP removal,
 * and selects an active roaming vdev whose link_id matches the candidate_link_set.
 * If no active link is found, it falls back to a standby link. The selected
 * link's AP MAC address is stored in req->peer_mac for frame transmission.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_INVAL if no valid peer MAC found
 */
QDF_STATUS
smd_roam_link_recfg_set_tx_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			struct mlo_link_recfg_state_req *req,
			uint32_t candidate_link_set);

/**
 * smd_is_roaming_in_progress() - Check if SMD roaming is active on a vdev.
 * @vdev: vdev pointer
 *
 * Convenience wrapper over smd_roam_in_progress() that takes a vdev pointer
 * instead of a recfg_ctx pointer. Used by cm_state_connected_entry() (T4)
 * and other CM SM callers that only have a vdev reference.
 *
 * Return: true if smd_roam_in_progress flag is set, false otherwise
 */
bool
smd_is_roaming_in_progress(struct wlan_objmgr_vdev *vdev);

/**
 * smd_roam_start_link_switch() - Start link switch for SMD roaming
 * @vdev: vdev pointer
 * @cmd: Serialization command pointer
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_roam_start_link_switch(struct wlan_objmgr_vdev *vdev,
			   struct wlan_serialization_command *cmd);
#else
static inline QDF_STATUS
smd_roam_link_recfg_set_tx_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			struct mlo_link_recfg_state_req *req,
			uint32_t candidate_link_set)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_fw_roam_sync(struct wlan_objmgr_vdev *vdev,
		 struct roam_offload_synch_ind *sync_ind)
{
	return QDF_STATUS_SUCCESS;
}

static inline bool
smd_handle_cm_roam_sync_pending(struct wlan_objmgr_vdev *vdev,
				QDF_STATUS status)
{
	return false;
}

static inline bool
wlan_is_smd_roam_sync(struct roam_offload_synch_ind *sync_ind)
{
	return false;
}

static inline QDF_STATUS
wlan_smd_roam_sync_status(QDF_STATUS status)
{
	return status;
}

static inline bool
smd_link_recfg_has_active_vdev_for_add_link(
				struct mlo_link_recfg_context *recfg_ctx,
				struct mlo_link_recfg_state_req *req,
				struct wlan_mlo_link_switch_req *link_sw_req)
{
	return false;
}

static inline bool
smd_cleanup_curr_ap_link(struct mlo_link_recfg_context *recfg_ctx,
			 struct mlo_link_recfg_state_req *req,
			 struct wlan_mlo_link_switch_req *link_sw_req)
{
	return false;
}

static inline QDF_STATUS
smd_roam_link_switch_disconnect_done(struct wlan_objmgr_vdev *vdev,
				     struct wlan_mlo_dev_context *mlo_dev_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_roam_prep_complete(struct mlo_link_recfg_context *recfg_ctx,
		       struct mlo_link_recfg_state_req *req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_add_prepared_target_links_in_smd_ctx(
					struct mlo_link_recfg_context *recfg_ctx,
					struct mlo_link_recfg_state_req *req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline struct mlo_link_info *
smd_get_prepared_ap_link_info(struct wlan_objmgr_vdev *vdev,
			      struct qdf_mac_addr *ap_link_addr)
{
	return NULL;
}

static inline struct mlo_link_info *
smd_get_prep_ap_link_info(struct wlan_objmgr_vdev *vdev,
			  struct wlan_mlo_link_switch_req *req)
{
	return NULL;
}

static inline QDF_STATUS
smd_host_link_switch_validate_request(struct wlan_objmgr_vdev *vdev,
				      struct wlan_mlo_link_switch_req *req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline void
smd_link_recfg_del_link_completed(struct mlo_link_recfg_context *recfg_ctx)
{
}

static inline void
smd_link_recfg_complete(struct mlo_link_recfg_context *recfg_ctx,
			bool success)
{
}

static inline struct wlan_mlo_link_recfg_bss_info *
smd_find_first_accepted_link(struct mlo_link_recfg_context *recfg_ctx,
			     struct mlo_link_recfg_state_tran *tran)
{
	return NULL;
}
static inline QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct mlo_link_recfg_state_tran *tran)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_uhr_link_recfg_send_request_frame(
		struct mlo_link_recfg_context *recfg_ctx,
		struct mlo_link_recfg_state_req *req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline bool
smd_roam_in_progress(struct mlo_link_recfg_context *recfg_ctx)
{
	return false;
}

static inline uint32_t
smd_get_roam_sync_timeout(struct wlan_objmgr_vdev *vdev)
{
	return FW_ROAM_SYNC_TIMEOUT;
}

static inline bool
smd_is_roaming_in_progress(struct wlan_objmgr_vdev *vdev)
{
	return false;
}

static inline
QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline
QDF_STATUS smd_start_link_recfg(struct wlan_objmgr_vdev *vdev,
				struct roam_offload_roam_event *roam_event)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_create_link_recfg_transition_list(
				struct mlo_link_recfg_context *recfg_ctx,
				struct wlan_mlo_link_recfg_req *recfg_req)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_link_recfg_assign_self_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			uint32_t del_link_set,
			uint32_t *first_del_link_set_no_common)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_validate_repurpose_smd_addr(struct mlo_link_recfg_context *recfg_ctx,
				struct wlan_mlo_dev_context *mlo_dev_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_roam_start_link_switch(struct wlan_objmgr_vdev *vdev,
			   struct wlan_serialization_command *cmd)
{
	return QDF_STATUS_E_NOSUPPORT;
}
#endif /* WLAN_FEATURE_11BN_SMD */
#endif
