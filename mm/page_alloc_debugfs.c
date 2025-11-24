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
#include <linux/nodemask.h>
#include <linux/printk.h>

static inline void create_nodes_subdirs(struct dentry *mmdir)
{
	struct dentry *nodedir;
	int nodeid;
	char dirname[12];

	for_each_online_node(nodeid) {
		snprintf(dirname, sizeof(dirname), "node-%d", nodeid);
		nodedir = debugfs_create_dir(dirname, mmdir);
	}
}

static int __init page_alloc_debugfs_init(void)
{
	struct dentry *mmdir;

	pr_info("Starting");
	mmdir = debugfs_create_dir("mm", NULL);
	if (IS_ERR(mmdir))
		return PTR_ERR(mmdir);

	create_nodes_subdirs(mmdir);

	return 0;
}

subsys_initcall(page_alloc_debugfs_init);

