/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
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
 * DOC: wlan_tdls_tgt_api.h
 *
 * TDLS south bound interface declaration
 */

#ifndef _WLAN_TDLS_TGT_API_H_
#define _WLAN_TDLS_TGT_API_H_
#include <wlan_tdls_public_structs.h>
#include "wlan_tdls_main.h"

/**
 * tgt_tdls_set_fw_state() - invoke lmac tdls update fw
 * @psoc: soc object
 * @tdls_param: update tdls state parameters
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_set_fw_state(struct wlan_objmgr_psoc *psoc,
				 struct tdls_info *tdls_param);

/**
 * tgt_tdls_set_offchan_mode() - invoke lmac tdls set off-channel mode
 * @psoc: soc object
 * @param: set tdls off channel parameters
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_set_offchan_mode(struct wlan_objmgr_psoc *psoc,
				     struct tdls_channel_switch_params *param);

/**
 * tgt_tdls_send_mgmt_rsp() - process tdls mgmt response
 * @pmsg: scheduler msg
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_send_mgmt_rsp(struct scheduler_msg *pmsg);

/**
 * tgt_tdls_send_mgmt_tx_completion() -process tx completion message
 * @pmsg: scheduler msg
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_send_mgmt_tx_completion(struct scheduler_msg *pmsg);

/**
 * tgt_tdls_del_peer_rsp() - handle TDLS del peer response
 * @pmsg: scheduler msg
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_del_peer_rsp(struct scheduler_msg *pmsg);

/**
 * tgt_tdls_add_peer_rsp() - handle TDLS add peer response
 * @pmsg: scheduler msg
 *
 * Return: QDF_STATUS
 */
QDF_STATUS tgt_tdls_add_peer_rsp(struct scheduler_msg *pmsg);

/**
 * tgt_tdls_register_ev_handler() - invoke lmac register tdls event handler
 * @psoc: soc object
 *
 * Return: QDF_STATUS_SUCCESS for success or error code.
 */
QDF_STATUS tgt_tdls_register_ev_handler(struct wlan_objmgr_psoc *psoc);

/**
 * tgt_tdls_unregister_ev_handler() - invoke lmac unregister tdls event handler
 * @psoc: soc object
 *
 * Return: QDF_STATUS_SUCCESS for success or error code.
 */
QDF_STATUS tgt_tdls_unregister_ev_handler(struct wlan_objmgr_psoc *psoc);

/**
 * tgt_tdls_event_handler() - The callback registered to WMI for tdls events
 * @psoc: psoc object
 * @info: tdls event info
 *
 * The callback is registered by tgt as tdls rx ops handler.
 *
 * Return: 0 for success or err code.
 */
QDF_STATUS
tgt_tdls_event_handler(struct wlan_objmgr_psoc *psoc,
		       struct tdls_event_info *info);

/**
 * tgt_tdls_mgmt_frame_rx_cb() - callback for rx mgmt frame
 * @psoc: soc context
 * @peer: peer context
 * @buf: rx buffer
 * @mgmt_rx_params: mgmt rx parameters
 * @frm_type: frame type
 *
 * This function gets called from mgmt tx/rx component when rx mgmt
 * received.
 *
 * Return: QDF_STATUS_SUCCESS
 */
QDF_STATUS tgt_tdls_mgmt_frame_rx_cb(struct wlan_objmgr_psoc *psoc,
	struct wlan_objmgr_peer *peer, qdf_nbuf_t buf,
	struct mgmt_rx_event_params *mgmt_rx_params,
	enum mgmt_frame_type frm_type);

/**
 * tgt_tdls_peers_deleted_notification()- notification from legacy lim
 * @psoc: soc object
 * @session_id: session id
 *
 * This function called from legacy lim to notify tdls peer deletion
 *
 * Return: None
 */
void tgt_tdls_peers_deleted_notification(struct wlan_objmgr_psoc *psoc,
					 uint32_t session_id);

/**
 * tgt_tdls_delete_all_peers_indication()- Indication to tdls component
 * @psoc: soc object
 * @session_id: session id
 *
 * This function called from legacy lim to tdls component to delete tdls peers.
 *
 * Return: None
 */
void tgt_tdls_delete_all_peers_indication(struct wlan_objmgr_psoc *psoc,
					  uint32_t session_id);

/**
 * tgt_tdls_request_stats_info() - Send WMI_REQUEST_STATS_INFO_CMDID to FW
 * @psoc: PSOC object
 * @vdev_id: ID of the STA vdev for which stats collection is requested
 * @enable: 1 = enable FW TDLS stats collection; host never sends 0
 *          (FW handles disable automatically on disconnect/MCC)
 *
 * Invokes the request_stats_info tx_op registered by the target_if layer.
 * Called from tdls_stats_handle_sta_connection() when a single-STA SCC
 * condition is detected after a STA connection event.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
QDF_STATUS tgt_tdls_request_stats_info(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id, uint32_t enable);

#endif
