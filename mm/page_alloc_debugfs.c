// SPDX-License-Identifier: GPL-2.0
/*
 * Page allocator DebugFS Interface
 *
 * This interface allows to make page allocations per node, zone, migrate type
 * and order using the DebugFS filesystem.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/printk.h>

static int __init page_alloc_debugfs_init(void)
{
	pr_info("Starting");

	return 0;
}

subsys_initcall(page_alloc_debugfs_init);

