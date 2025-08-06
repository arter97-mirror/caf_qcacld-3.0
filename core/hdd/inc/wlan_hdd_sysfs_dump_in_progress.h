/*
 * Copyright (c) 2017-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022,2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
 * DOC: wlan_hdd_sysfs_dump_in_progress.h
 *
 * hdd dump in progress declarations
 */

#if defined(WLAN_SYSFS) && defined(CONFIG_WLAN_DUMP_IN_PROGRESS)
/**
 * hdd_sysfs_create_dump_in_progress_interface() - API to create
 * dump_in_progress sysfs file
 * @wifi_kobject: sysfs wifi kobject
 * @hdd_ctx: ptr to hdd_ctx
 *
 * Return: None
 */
void hdd_sysfs_create_dump_in_progress_interface(struct kobject *wifi_kobject,
						 struct hdd_context *hdd_ctx);

/**
 * hdd_sysfs_destroy_dump_in_progress_interface() - API to destroy
 * dump_in_progress sysfs file
 * @wifi_kobject: sysfs wifi kobject
 *
 * Return: None
 */
void hdd_sysfs_destroy_dump_in_progress_interface(struct kobject *wifi_kobject);

/**
 * hdd_sysfs_create_enhance_chipset_logging_interface() - Create sysfs file
 * @wifi_kobject: Pointer to the wifi kobject under which the file is created
 * @hdd_ctx: Pointer to the hdd_context structure
 *
 * This function creates the sysfs file 'enhance_chipset_logging' under
 * /sys/wifi/. It allows cnss_diag to signal support for
 * userspace caching.
 */
void
hdd_sysfs_create_enhance_chipset_logging_interface(struct kobject *wifi_kobject,
						   struct hdd_context *hdd_ctx);

/**
 * hdd_sysfs_destroy_enhance_chipset_logging_interface() - Remove sysfs file
 * @wifi_kobject: Pointer to the wifi kobject under which the file was created
 *
 * This function removes the sysfs file 'enhance_chipset_logging' if it exists.
 */
void hdd_sysfs_destroy_enhance_chipset_logging_interface(
					struct kobject *wifi_kobject);
#else
static inline void
hdd_sysfs_create_dump_in_progress_interface(struct kobject *wifi_kobject,
					    struct hdd_context *hdd_ctx)
{
}
static inline void
hdd_sysfs_destroy_dump_in_progress_interface(struct kobject *wifi_kobject)
{
}

static inline void
hdd_sysfs_create_enhance_chipset_logging_interface(struct kobject *wifi_kobject,
						   struct hdd_context *hdd_ctx)
{
}

static inline void hdd_sysfs_destroy_enhance_chipset_logging_interface(
					struct kobject *wifi_kobject)
{
}
#endif
