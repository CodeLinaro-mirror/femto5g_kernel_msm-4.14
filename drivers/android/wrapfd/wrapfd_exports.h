/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal declarations for the Rust wrapfd entry points wrapped by
 * wrapfd_exports.c.
 *
 * Copyright (c) 2026, Google LLC.
 */

#ifndef _DRIVERS_ANDROID_WRAPFD_EXPORTS_H
#define _DRIVERS_ANDROID_WRAPFD_EXPORTS_H

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/wrapfd.h>

int rust_wrapfd_get_mappable(struct file *file, struct device *dev,
			     union wrapfd_mappable *mappable);
int rust_wrapfd_put_mappable(struct file *file, struct device *dev,
			     union wrapfd_mappable *mappable);
int rust_wrapfd_add_listener(struct file *file, struct device *dev,
			     const struct wrapfd_listener_operations *ops,
			     void *param, void **handlep);
int rust_wrapfd_del_listener(struct file *file, void *handle);
int rust_wrapfd_acquire_ownership(struct file *file, struct device *dev);
int rust_wrapfd_release_ownership(struct file *file, struct device *dev);

#if IS_ENABLED(CONFIG_ANDROID_WRAPFD_KUNIT_TEST)
int rust_create_wrapfd(int fd, int prot);
int rust_empty_wrapfd(struct file *file, struct device *dev);
#endif

#endif /* _DRIVERS_ANDROID_WRAPFD_EXPORTS_H */
