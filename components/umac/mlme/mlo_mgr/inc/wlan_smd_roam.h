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

#ifdef WLAN_FEATURE_11BN_SMD

/**
 * smd_fw_roam_start - Handler for SMD roam start event handler
 *
 * @vdev: vdev pointer
 * @roam_event: roam event pointer
 *
 * This api will be called from CM layer,handles SMD roam
 * start.
 *
 * Return: qdf status
 */
QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev,
				struct roam_offload_roam_event *roam_event);

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
 * smd_create_link_recfg_transition_list() - Section 6.3: Create transition list
 * @vdev: VDEV object
 * @recfg_ctx: Link reconfiguration context
 *
 * This function creates the link transition list from vdev_repurpose_req.
 * It parses vdev_repurpose_req for each link, creates link transition
 * descriptors, determines add/delete operations,
 * and stores in link_recfg_context.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_create_link_recfg_transition_list(struct wlan_objmgr_vdev *vdev,
				      struct mlo_link_recfg_context *recfg_ctx);

/**
 * smd_link_recfg_assign_self_link_addr() - Assign link addresses for target AP
 * @recfg_ctx: Link reconfiguration context
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
smd_link_recfg_assign_self_link_addr(struct mlo_link_recfg_context *recfg_ctx);

#else
static inline
QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev,
			     struct roam_offload_roam_event *roam_event)
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
smd_create_link_recfg_transition_list(struct wlan_objmgr_vdev *vdev,
				      struct mlo_link_recfg_context *recfg_ctx)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline QDF_STATUS
smd_link_recfg_assign_self_link_addr(struct mlo_link_recfg_context *recfg_ctx)

{
	return QDF_STATUS_E_NOSUPPORT;
}
#endif /* WLAN_FEATURE_11BN */
#endif
