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

#endif /* WLAN_FEATURE_ACTION_OUI */

#endif /* _WLAN_ACTION_OUI_API_H_ */
