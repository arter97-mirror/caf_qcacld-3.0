/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_wow_wake_reason.h
 *
 * implementation for creating sysfs file wow_wake_reason
 */

#ifndef _WLAN_HDD_SYSFS_WOW_WAKE_REASON_H
#define _WLAN_HDD_SYSFS_WOW_WAKE_REASON_H

#include <wow_wake_reason.h>

#if defined(WLAN_SYSFS) && defined(WLAN_WOW_WAKEUP_NOTIFY)

/**
 * hdd_sysfs_wow_wake_reason_create() - API to create wow_wake_reason
 * @driver_kobject: sysfs driver kobject
 *
 * file path: /sys/kernel/wifi/wow_wake_reason
 *
 * Return: 0 on success and errno on failure
 */
int hdd_sysfs_wow_wake_reason_create(struct kobject *driver_kobject);

/**
 * hdd_sysfs_wow_wake_reason_destroy() - API to destroy wow_wake_reason
 * @driver_kobject: sysfs driver kobject
 *
 * Return: none
 */
void hdd_sysfs_wow_wake_reason_destroy(struct kobject *driver_kobject);

/**
 * hdd_sysfs_wow_wake_reason_notify() - API to notify wow wake reason
 * @uapi: UAPI payload to update the sysfs buffer with
 *
 * Return: none
 */
void hdd_sysfs_wow_wake_reason_notify(const struct wow_wake_reason_uapi *uapi);

#else

static inline int
hdd_sysfs_wow_wake_reason_create(struct kobject *driver_kobject)
{
	return 0;
}

static inline void
hdd_sysfs_wow_wake_reason_destroy(struct kobject *driver_kobject)
{
}

static inline void
hdd_sysfs_wow_wake_reason_notify(const struct wow_wake_reason_uapi *uapi)
{
}

#endif
#endif /* #ifndef _WLAN_HDD_SYSFS_WOW_WAKE_REASON_H */
