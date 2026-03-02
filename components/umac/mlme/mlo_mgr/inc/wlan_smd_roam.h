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
 * smd_st_prep_response_received() - API to handle Link Recfg rsp
 * @recfg_ctx: Link reconfiguration context
 * @recfg_resp_data: Link reconfiguration resp data
 * @event_data_len: Event data length
 *
 * API to handle UHR Link Reconfiguration ST prep response
 *
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct link_recfg_rx_rsp *recfg_resp_data,
			      uint16_t event_data_len);
#else

static inline QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct link_recfg_rx_rsp *recfg_resp_data,
			      uint16_t event_data_len)
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
#endif /* WLAN_FEATURE_11BN */
#endif
