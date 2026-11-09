/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_wow_wake_reason.c
 *
 * implementation for creating sysfs file wow_wake_reason
 */

#include <wlan_hdd_includes.h>
#include <wlan_hdd_sysfs.h>
#include "wlan_hdd_sysfs_wow_wake_reason.h"
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <qdf_lock.h>
#include <qdf_mem.h>

/**
 * struct wake_reason_notif_local - local state for wow wake reason sysfs node
 * @wow_wake_reason_buf: buffer holding the latest UAPI payload
 * @drv_kobject: driver kobject instance
 * @lock: protects @wow_wake_reason_buf and @drv_kobject
 * @initialized: true once @lock exists; guards against double create/destroy
 *               (e.g. across SSR) and lets notify bail out before the lock
 *               exists
 */
struct wake_reason_notif_local {
	struct wow_wake_reason_uapi wow_wake_reason_buf;
	struct kobject *drv_kobject;
	qdf_spinlock_t lock;
	bool initialized;
};

static struct wake_reason_notif_local wow_local;

/**
 * hdd_sysfs_wow_wake_reason_read() - sysfs bin_attr read handler
 * @filp: file pointer
 * @kobj: kobject
 * @attr: bin attribute
 * @buf: output buffer
 * @off: offset into the payload
 * @count: number of bytes requested
 *
 * Supports partial/offset reads; @count is clamped to bytes left from @off.
 *
 * Return: number of bytes copied (0 at/after EOF), negative errno on error
 */
static ssize_t
hdd_sysfs_wow_wake_reason_read(struct file *filp,
			       struct kobject *kobj,
			       struct bin_attribute *attr,
			       char *buf, loff_t off, size_t count)
{
	if (off < 0)
		return -EINVAL;

	if (off >= WOW_WAKE_REASON_UAPI_SIZE)
		return 0;

	if (count > WOW_WAKE_REASON_UAPI_SIZE - off)
		count = WOW_WAKE_REASON_UAPI_SIZE - off;

	qdf_spin_lock(&wow_local.lock);
	qdf_mem_copy(buf, (uint8_t *)&wow_local.wow_wake_reason_buf + off,
		     count);
	qdf_spin_unlock(&wow_local.lock);

	return count;
}

static const struct bin_attribute wow_wake_reason_bin_attr = {
	.attr = {
		.name = "wow_wake_reason",
		.mode = 0440,
	},
	.size  = WOW_WAKE_REASON_UAPI_SIZE,
	.read  = hdd_sysfs_wow_wake_reason_read,
};

void hdd_sysfs_wow_wake_reason_notify(const struct wow_wake_reason_uapi *uapi)
{
	struct kobject *kobj;

	if (!uapi)
		return;

	if (!wow_local.initialized)
		return;

	qdf_spin_lock(&wow_local.lock);
	qdf_mem_copy(&wow_local.wow_wake_reason_buf, uapi,
		     sizeof(wow_local.wow_wake_reason_buf));
	kobj = wow_local.drv_kobject;
	qdf_spin_unlock(&wow_local.lock);

	if (kobj)
		sysfs_notify(kobj, NULL, "wow_wake_reason");
}

int hdd_sysfs_wow_wake_reason_create(struct kobject *driver_kobject)
{
	int error;

	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return -EINVAL;
	}

	if (wow_local.initialized)
		return 0;

	error = sysfs_create_bin_file(driver_kobject,
				      &wow_wake_reason_bin_attr);
	if (error) {
		hdd_err("could not create wow_wake_reason sysfs file");
		return error;
	}

	qdf_spinlock_create(&wow_local.lock);

	qdf_spin_lock(&wow_local.lock);
	/*
	 * seed "unspecified" so a pre-event read differs from a
	 * real NLOD (0) wake
	 */
	wow_local.wow_wake_reason_buf.wake_reason =
					WOW_WAKE_REASON_CODE_UNSPECIFIED;
	wow_local.drv_kobject = driver_kobject;
	qdf_spin_unlock(&wow_local.lock);

	wow_local.initialized = true;

	return 0;
}

void hdd_sysfs_wow_wake_reason_destroy(struct kobject *driver_kobject)
{
	if (!wow_local.initialized)
		return;

	qdf_spin_lock(&wow_local.lock);
	wow_local.drv_kobject = NULL;
	qdf_spin_unlock(&wow_local.lock);

	wow_local.initialized = false;

	if (driver_kobject)
		sysfs_remove_bin_file(driver_kobject,
				      &wow_wake_reason_bin_attr);
	else
		hdd_err("could not get driver kobject!");

	qdf_spinlock_destroy(&wow_local.lock);
}
