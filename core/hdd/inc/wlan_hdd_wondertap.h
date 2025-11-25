/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_wondertap.h
 *
 * WLAN Host Device Driver file for wondertap functionality.
 *
 */
#if !defined(WLAN_HDD_WONDERTAP_H)
#define WLAN_HDD_WONDERTAP_H

#ifdef DRIVER_PASSTHRU_MODE
#include <qdf_wondertap.h>

/**
 * wlan_hdd_wondertap_register_ops() - Register wondertap operations
 *
 * This function registers the WLAN driver's wondertap operations with the
 * wondertap framework. It should be called during driver initialization
 * to enable wondertap functionality.
 *
 * Return: 0 on success, negative error code on failure
 */
int wlan_hdd_wondertap_register_ops(void);

/**
 * wlan_hdd_wondertap_unregister_ops() - Unregister wondertap operations
 *
 * This function unregisters the WLAN driver's wondertap operations from the
 * wondertap framework. It should be called during driver cleanup to
 * properly release wondertap resources.
 *
 * Return: void
 */
void wlan_hdd_wondertap_unregister_ops(void);

/**
 * hdd_sme_passthrough_mode_callback() - Callback triggered by SME layer on
 *  successful channel change operation.
 * @vdev_id: vdev id
 * @is_up: is vdev up
 *
 * Return: None
 */
void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up);
#else
static inline int wlan_hdd_wondertap_register_ops(void)
{
	return 0;
}

static inline void wlan_hdd_wondertap_unregister_ops(void)
{
}

static inline
void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up)
{
}
#endif /*DRIVER_PASSTHRU_MODE */
#endif /* WLAN_HDD_WONDERTAP_H */
