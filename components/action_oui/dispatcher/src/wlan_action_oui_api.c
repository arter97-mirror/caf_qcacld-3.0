/*
 * Copyright (c) 2012-2018, 2024 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: Public API implementation of action_oui called by WLAN internal
 * components like CM (Connection Manager), scan, etc.
 */

#include "wlan_action_oui_api.h"
#include "wlan_action_oui_main.h"

enum action_oui_id
wlan_action_oui_get_active_action_id(
			      struct wlan_objmgr_psoc *psoc,
			      enum action_oui_arbitrator_type arbitrator_type)
{
	return action_oui_get_active_action_id(psoc, arbitrator_type);
}

void
wlan_action_oui_get_nss_policy(struct wlan_objmgr_psoc *psoc,
			       struct action_oui_search_attr *attr,
			       bool *found_in_list,
			       uint32_t *list_type)
{
	action_oui_get_nss_policy(psoc, attr, found_in_list, list_type);
}

bool
wlan_action_oui_is_ul_tx_beamformer_config_supported(
				struct wlan_objmgr_psoc *psoc)
{
	return wlan_is_ul_tx_beamformer_config_supported(psoc);
}

bool
wlan_search_action_oui(struct wlan_objmgr_psoc *psoc,
		       struct action_oui_search_attr *attr,
		       enum action_oui_id action_id)
{
	return wlan_action_oui_search(psoc, attr, action_id);
}
