// SPDX-License-Identifier: GPL-2.0

/*
 * C wrappers for symbols exported from the Wrapfd Rust driver to loadable
 * kernel modules.
 *
 * Copyright (c) 2026, Google LLC.
 */

#include <linux/export.h>

#include "wrapfd_exports.h"

/*
 * Define thin C wrappers around the Rust entry points for symbols exported to
 * loadable kernel modules. On branches whose ABI tooling does not map Rust
 * bindgen types back to C types, exporting the C wrappers ensures that the GKI
 * ABI representation records standard C types from <linux/wrapfd.h> rather than
 * Rust DWARF types, and also exports symbols from a crate outside rust/.
 */
int wrapfd_get_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable)
{
	return rust_wrapfd_get_mappable(file, dev, mappable);
}
EXPORT_SYMBOL_GPL(wrapfd_get_mappable);

int wrapfd_put_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable)
{
	return rust_wrapfd_put_mappable(file, dev, mappable);
}
EXPORT_SYMBOL_GPL(wrapfd_put_mappable);

int wrapfd_add_listener(struct file *file, struct device *dev,
			const struct wrapfd_listener_operations *ops,
			void *param, void **handlep)
{
	return rust_wrapfd_add_listener(file, dev, ops, param, handlep);
}
EXPORT_SYMBOL_GPL(wrapfd_add_listener);

int wrapfd_del_listener(struct file *file, void *handle)
{
	return rust_wrapfd_del_listener(file, handle);
}
EXPORT_SYMBOL_GPL(wrapfd_del_listener);

int wrapfd_acquire_ownership(struct file *file, struct device *dev)
{
	return rust_wrapfd_acquire_ownership(file, dev);
}
EXPORT_SYMBOL_GPL(wrapfd_acquire_ownership);

int wrapfd_release_ownership(struct file *file, struct device *dev)
{
	return rust_wrapfd_release_ownership(file, dev);
}
EXPORT_SYMBOL_GPL(wrapfd_release_ownership);

#if IS_ENABLED(CONFIG_KUNIT)
int create_wrapfd(int fd, int prot)
{
	return rust_create_wrapfd(fd, prot);
}
EXPORT_SYMBOL_NS_GPL(create_wrapfd, EXPORTED_FOR_KUNIT_TESTING);

int empty_wrapfd(struct file *file, struct device *dev)
{
	return rust_empty_wrapfd(file, dev);
}
EXPORT_SYMBOL_NS_GPL(empty_wrapfd, EXPORTED_FOR_KUNIT_TESTING);
#endif
