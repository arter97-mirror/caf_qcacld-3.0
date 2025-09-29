/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 * DOC: wlan_hdd_connectivity_logging.c
 *
 * Implementation for the Common connectivity and roam logging api.
 */

#include "wlan_hdd_connectivity_logging.h"

#ifdef CONNECTIVITY_DIAG_EVENT
static enum wlan_diag_connect_fail_reason
wlan_hdd_convert_con_fail_reason_to_diag_reason(
				enum wlan_cm_connect_fail_reason reason)
{
	switch (reason) {
	case CM_NO_CANDIDATE_FOUND:
		return WLAN_DIAG_NO_CANDIDATE_FOUND;
	case CM_ABORT_DUE_TO_NEW_REQ_RECVD:
		return WLAN_DIAG_ABORT_DUE_TO_NEW_REQ_RECVD;
	case CM_BSS_SELECT_IND_FAILED:
		return WLAN_DIAG_BSS_SELECT_IND_FAILED;
	case CM_PEER_CREATE_FAILED:
		return WLAN_DIAG_PEER_CREATE_FAILED;
	case CM_JOIN_FAILED:
		return WLAN_DIAG_JOIN_FAILED;
	case CM_JOIN_TIMEOUT:
		return WLAN_DIAG_JOIN_TIMEOUT;
	case CM_AUTH_FAILED:
		return WLAN_DIAG_AUTH_FAILED;
	case CM_AUTH_TIMEOUT:
		return WLAN_DIAG_AUTH_TIMEOUT;
	case CM_ASSOC_FAILED:
		return WLAN_DIAG_ASSOC_FAILED;
	case CM_ASSOC_TIMEOUT:
		return WLAN_DIAG_ASSOC_TIMEOUT;
	case CM_HW_MODE_FAILURE:
		return WLAN_DIAG_HW_MODE_FAILURE;
	case CM_SER_FAILURE:
		return WLAN_DIAG_SER_FAILURE;
	case CM_SER_TIMEOUT:
		return WLAN_DIAG_SER_TIMEOUT;
	case CM_GENERIC_FAILURE:
		return WLAN_DIAG_GENERIC_FAILURE;
	case CM_VALID_CANDIDATE_CHECK_FAIL:
		return WLAN_DIAG_VALID_CANDIDATE_CHECK_FAIL;
	default:
		hdd_err("Invalid connect fail reason code");
	}

	return WLAN_DIAG_UNSPECIFIC_REASON;
}

void
wlan_hdd_connectivity_fail_event(struct wlan_objmgr_vdev *vdev,
				 struct wlan_cm_connect_resp *rsp)
{
	WLAN_HOST_DIAG_EVENT_DEF(wlan_diag_event, struct wlan_diag_connect);

	qdf_mem_zero(&wlan_diag_event, sizeof(struct wlan_diag_connect));

	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_STA_MODE)
		return;

	if (wlan_vdev_mlme_is_mlo_vdev(vdev) &&
	    (wlan_vdev_mlme_is_mlo_link_switch_in_progress(vdev) ||
	     wlan_vdev_mlme_is_mlo_link_vdev(vdev)))
		return;

	wlan_diag_event.diag_cmn.vdev_id = wlan_vdev_get_id(vdev);

	wlan_diag_event.diag_cmn.timestamp_us = qdf_get_time_of_the_day_us();
	wlan_diag_event.diag_cmn.ktime_us = qdf_ktime_to_us(qdf_ktime_get());
	wlan_diag_event.subtype = WLAN_CONN_DIAG_CONNECT_FAIL_EVENT;
	qdf_mem_copy(wlan_diag_event.diag_cmn.bssid, rsp->bssid.bytes,
		     QDF_MAC_ADDR_SIZE);

	wlan_diag_event.version = DIAG_CONN_VERSION;
	wlan_diag_event.freq = rsp->freq;
	wlan_diag_event.reason =
	wlan_hdd_convert_con_fail_reason_to_diag_reason(rsp->reason);

	WLAN_HOST_DIAG_EVENT_REPORT(&wlan_diag_event, EVENT_WLAN_CONN);
}
#endif
