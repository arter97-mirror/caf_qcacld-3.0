/*
 * Copyright (c) 2012-2018, 2024 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: Declare public API related to the action_oui called by WLAN internal
 * components like CM (Connection Manager), scan, etc.
 */

#ifndef _WLAN_ACTION_OUI_API_H_
#define _WLAN_ACTION_OUI_API_H_

#include <qdf_status.h>
#include <qdf_types.h>
#include "wlan_action_oui_public_struct.h"
#include "wlan_objmgr_psoc_obj.h"

#ifdef WLAN_FEATURE_ACTION_OUI

/**
 * wlan_action_oui_get_active_action_id() - Get active action OUI ID for
 * arbitrator
 * @psoc: objmgr psoc object
 * @arbitrator_type: type of arbitrator to query
 *
 * This function determines which action OUI list is currently active for the
 * specified arbitrator type. The function checks configured lists and returns
 * the active policy identifier. This API is intended for use by WLAN internal
 * components like Connection Manager (CM), scan module, etc.
 *
 * Return: Active action_oui_id if a list is active,
 *         ACTION_OUI_MAXIMUM_ID if no active list found or on error
 */
enum action_oui_id
wlan_action_oui_get_active_action_id(
			      struct wlan_objmgr_psoc *psoc,
			      enum action_oui_arbitrator_type arbitrator_type);

/**
 * wlan_action_oui_get_nss_policy() - Get NSS arbitrator list match result
 * @psoc: objmgr psoc object
 * @attr: action OUI search attributes (contains AP info like OUI, MAC, etc.)
 * @found_in_list: pointer to filled match result
 * @list_type: pointer to filled list type
 *
 * This dispatcher API identifies the active NSS arbitrator list and fills
 * whether the AP matches that list. The caller is responsible for applying
 * the NSS min/max selection logic.
 *
 * Return: void
 */
void
wlan_action_oui_get_nss_policy(struct wlan_objmgr_psoc *psoc,
			       struct action_oui_search_attr *attr,
			       bool *found_in_list,
			       uint32_t *list_type);

/**
 * wlan_action_oui_is_ul_tx_beamformer_config_supported() - Check whether UL TX
 * beamformer configuration is supported
 * @psoc: objmgr psoc object
 *
 * This dispatcher API returns true only when the underlying action OUI UL TX
 * beamformer configuration support is enabled.
 *
 * Return: true if supported, else false
 */
bool
wlan_action_oui_is_ul_tx_beamformer_config_supported(
				struct wlan_objmgr_psoc *psoc);

/**
 * wlan_search_action_oui() - Search for AP in action OUI list
 * @psoc: objmgr psoc object
 * @attr: action OUI search attributes (contains AP info like OUI, MAC, etc.)
 * @action_id: action OUI ID to search in
 *
 * This dispatcher API searches for an AP in the specified action OUI list
 * based on the provided search attributes. It is similar to
 * ucfg_action_oui_search but intended for use by WLAN internal components
 * like CM (Connection Manager), scan module, etc.
 *
 * Return: true if AP found in the list, false otherwise
 */
bool
wlan_search_action_oui(struct wlan_objmgr_psoc *psoc,
		       struct action_oui_search_attr *attr,
		       enum action_oui_id action_id);

#else

/**
 * wlan_action_oui_get_active_action_id() - Get active action OUI ID
 * for arbitrator
 * @psoc: objmgr psoc object
 * @arbitrator_type: type of arbitrator to query
 *
 * Stub function when WLAN_FEATURE_ACTION_OUI is not enabled.
 *
 * Return: ACTION_OUI_MAXIMUM_ID (always returns maximum ID in stub)
 */
static inline
enum action_oui_id
wlan_action_oui_get_active_action_id(
			struct wlan_objmgr_psoc *psoc,
			enum action_oui_arbitrator_type arbitrator_type)
{
	return ACTION_OUI_MAXIMUM_ID;
}

/**
 * wlan_action_oui_get_nss_policy() - Get NSS arbitrator list match result
 * @psoc: objmgr psoc object
 * @attr: action OUI search attributes (contains AP info like OUI, MAC, etc.)
 * @found_in_list: pointer to filled match result
 * @list_type: pointer to filled list type
 *
 * Stub function when WLAN_FEATURE_ACTION_OUI is not enabled.
 * Does nothing when feature is disabled.
 *
 * Return: void
 */
static inline void
wlan_action_oui_get_nss_policy(struct wlan_objmgr_psoc *psoc,
			       struct action_oui_search_attr *attr,
			       bool *found_in_list,
			       uint32_t *list_type)
{
}

static inline bool
wlan_action_oui_is_ul_tx_beamformer_config_supported(
				struct wlan_objmgr_psoc *psoc)
{
	return false;
}

static inline bool
wlan_search_action_oui(struct wlan_objmgr_psoc *psoc,
		       struct action_oui_search_attr *attr,
		       enum action_oui_id action_id)
{
	return false;
}

#endif /* WLAN_FEATURE_ACTION_OUI */

#endif /* _WLAN_ACTION_OUI_API_H_ */
