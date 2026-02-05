/* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef CONFIG_DEBUG_FS
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/stringify.h>
#include "ipa_eth_sysfs.h"

struct kobject *eth_dev_kobj = NULL;
struct kobject *eth_drv_kobj = NULL;
struct kobject *eth_bus_kobj = NULL;
struct kobject *eth_pci_kobj = NULL;
struct kobject *eth_off_kobj = NULL;
struct kobject *eth_kobj = NULL;

extern unsigned long ipa_eth_state;

static LIST_HEAD(ipa_eth_devices);
static DEFINE_MUTEX(ipa_eth_devices_lock);

extern bool ipa_eth_noauto;
#ifdef CONFIG_IPC_LOGGING
extern bool ipa_eth_ipc_logdbg;
#endif
/* Function prototypes */
/* Function prototypes */
static ssize_t eth_dev_init_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t eth_dev_init_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
static ssize_t eth_dev_write_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t eth_dev_write_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
static ssize_t ethdev_start_on_wakeup_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ethdev_start_on_wakeup_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
static ssize_t ethdev_start_on_resume_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ethdev_start_on_resume_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
static ssize_t ethdev_start_on_timeout_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ethdev_start_on_timeout_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
static ssize_t ethdev_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *ubuf);
static ssize_t ethdev_stats_store(struct kobject *kobj, struct kobj_attribute *attr, const char *ubuf, size_t size);
static ssize_t ready_show(struct kobject *kobj, struct kobj_attribute *attr, char *ubuf);
static ssize_t ready_store(struct kobject *kobj, struct kobj_attribute *attr, const char *ubuf, size_t size);
#ifdef CONFIG_IPC_LOGGING
static ssize_t ipc_logdbg_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ipc_logdbg_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);
#endif
static ssize_t no_auto_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t no_auto_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size);


/* Function definitions */
static ssize_t eth_dev_init_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    return sprintf(buf, "%s\n", ent->init ? "true" : "false");
}

static ssize_t eth_dev_init_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    ssize_t ret = 0;
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    ipa_eth_device_refresh_sync(eth_dev);
    ret = kstrtobool(buf, &(ent->init));
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}

static ssize_t no_auto_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", ipa_eth_noauto ? "true" : "false");
}

static ssize_t no_auto_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    ssize_t ret = 0;
     ret = kstrtobool(buf, &ipa_eth_noauto);
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}
#ifdef CONFIG_IPC_LOGGING
static ssize_t ipc_logdbg_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", ipa_eth_ipc_logdbg ? "true" : "false");
}

static ssize_t ipc_logdbg_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    ssize_t ret = 0;
     ret = kstrtobool(buf, &ipa_eth_ipc_logdbg);
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}
#endif
static ssize_t eth_dev_write_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    return sprintf(buf, "%s\n", eth_dev->start ? "true" : "false");
}

static ssize_t eth_dev_write_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    ssize_t ret = 0;
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;

    if (!eth_dev->start && eth_dev->start_on_timeout)
        mod_timer(&eth_dev->start_timer, jiffies + msecs_to_jiffies(eth_dev->start_on_timeout));

    ipa_eth_device_refresh_sync(eth_dev);
    ret = kstrtobool(buf, &(ent->start));
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}

static ssize_t ethdev_start_on_wakeup_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    return sprintf(buf, "%s\n", eth_dev->start_on_wakeup ? "true" : "false");
}

static ssize_t ethdev_start_on_wakeup_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    int ret = kstrtobool(buf, &eth_dev->start_on_wakeup);
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}

static ssize_t ethdev_start_on_resume_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    return sprintf(buf, "%s\n", eth_dev->start_on_resume ? "true" : "false");
}

static ssize_t ethdev_start_on_resume_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    int ret = kstrtobool(buf, &eth_dev->start_on_resume);
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}

static ssize_t ethdev_start_on_timeout_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    return sprintf(buf, "%u\n", eth_dev->start_on_timeout);
}

static ssize_t ethdev_start_on_timeout_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;
    int ret = kstrtouint(buf, 10, &eth_dev->start_on_timeout);
    if (ret < 0) {
        ipa_eth_log("Invalid user input\n");
    }
    return ret;
}

static ssize_t eth_dev_stats_print_one(char *buf, const size_t size, const char *dir, const char *link, struct ipa_eth_offload_link_stats *stats)
{
    return scnprintf(buf, size, "%10s%10s%10s%10llu%10llu%10llu%10llu\n", dir, link, (stats->valid ? "yes" : "no"), stats->events, stats->frames, stats->packets, stats->octets);
}

static ssize_t eth_dev_stats_print(char *buf, const size_t size, struct ipa_eth_device *eth_dev)
{
    ssize_t n = 0;
    struct ipa_eth_offload_stats stats;

    if (!eth_dev->od->ops->get_stats)
        return scnprintf(buf, size - n, "Not supported\n");

    memset(&stats, 0, sizeof(stats));

    if (eth_dev->od->ops->get_stats(eth_dev, &stats))
        return scnprintf(buf, size - n, "Operation failed\n");

    n += scnprintf(&buf[n], size - n, "%10s%10s%10s%10s%10s%10s%10s\n", "Dir", "Link", "Valid", "Events", "Frames", "Packets", "Octets");

    n += eth_dev_stats_print_one(&buf[n], size - n, "rx", "ndev", &stats.rx.ndev);
    n += eth_dev_stats_print_one(&buf[n], size - n, "rx", "host", &stats.rx.host);
    n += eth_dev_stats_print_one(&buf[n], size - n, "rx", "uc", &stats.rx.uc);
    n += eth_dev_stats_print_one(&buf[n], size - n, "rx", "gsi", &stats.rx.gsi);
    n += eth_dev_stats_print_one(&buf[n], size - n, "rx", "ipa", &stats.rx.ipa);

    n += scnprintf(&buf[n], size - n, "\n");

    n += eth_dev_stats_print_one(&buf[n], size - n, "tx", "ndev", &stats.tx.ndev);
    n += eth_dev_stats_print_one(&buf[n], size - n, "tx", "host", &stats.tx.host);
    n += eth_dev_stats_print_one(&buf[n], size - n, "tx", "uc", &stats.tx.uc);
    n += eth_dev_stats_print_one(&buf[n], size - n, "tx", "gsi", &stats.tx.gsi);
    n += eth_dev_stats_print_one(&buf[n], size - n, "tx", "ipa", &stats.tx.ipa);

    return n;
}

static ssize_t ethdev_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *ubuf)
{
    ssize_t n = 0, size = 2048;
    char *buf = NULL;
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    struct ipa_eth_device *eth_dev = ent->eth_dev;

    buf = kzalloc(size, GFP_KERNEL);
    if (!buf)
        return 0;

    n = eth_dev_stats_print(buf, size, eth_dev);
    memcpy(ubuf, buf, n);

    kfree(buf);
    return n;
}

static ssize_t ethdev_stats_store(struct kobject *kobj, struct kobj_attribute *attr, const char *ubuf, size_t size)
{
    int ret = 0;
    ipa_eth_log("Invalid access\n");
    ret = -EINVAL;
    return ret;
}

void eth_dev_release(struct kobject *kobj)
{
    struct eth_dev_sys_ent *ent = container_of(kobj, struct eth_dev_sys_ent, kobj);
    kfree(ent);
}

static struct kobj_type ktype = {
    .release = eth_dev_release,
    .sysfs_ops = &kobj_sysfs_ops,
    .default_attrs = NULL,
};

int ipa_eth_sysfs_add_device(struct ipa_eth_device *eth_dev)
{
    int err = 0;
    struct eth_dev_sys_ent *eth_ent = NULL;
    eth_dev->sfs = kobject_create_and_add(eth_dev->net_dev->name, eth_dev_kobj);
    if (IS_ERR_OR_NULL(eth_dev->sfs)) {
        ipa_eth_dev_err(eth_dev, "Failed to create debugfs root");
        return -EFAULT;
    }

    eth_ent = kzalloc(sizeof(struct eth_dev_sys_ent), GFP_KERNEL);
    if (!eth_ent) {
        ipa_eth_log("unable to allocate structure");
        return -ENOMEM;
    }

    static struct kobj_attribute eth_dev_init_attr = __ATTR(eth_dev_init, 0644, eth_dev_init_show, eth_dev_init_store);
    kobject_init(&eth_ent->kobj, &ktype);
    err = kobject_add(&eth_ent->kobj, eth_dev->sfs, eth_dev->net_dev->name);
    if (err) {
        ipa_eth_log("Unable to create kobject");
        kfree(eth_ent);
        return err;
    }

    err = sysfs_create_file(&eth_ent->kobj, &eth_dev_init_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create init file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }

    /*static struct kobj_attribute eth_dev_start_attr = __ATTR(eth_dev_start, 0644, eth_dev_start_show, eth_dev_start_store);
    err = sysfs_create_file(&eth_ent->kobj, &eth_dev_start_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create start file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }*/

    static struct kobj_attribute ethdev_start_on_wakeup_attr = __ATTR(ethdev_start_on_wakeup, 0644, ethdev_start_on_wakeup_show, ethdev_start_on_wakeup_store);
    err = sysfs_create_file(&eth_ent->kobj, &ethdev_start_on_wakeup_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create start_on_wakeup file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }

    static struct kobj_attribute ethdev_start_on_resume_attr = __ATTR(ethdev_start_on_resume, 0644, ethdev_start_on_resume_show, ethdev_start_on_resume_store);
    err = sysfs_create_file(&eth_ent->kobj, &ethdev_start_on_resume_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create start_on_resume file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }

    static struct kobj_attribute ethdev_start_on_timeout_attr = __ATTR(ethdev_start_on_timeout, 0644, ethdev_start_on_timeout_show, ethdev_start_on_timeout_store);
    err = sysfs_create_file(&eth_ent->kobj, &ethdev_start_on_timeout_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create start_on_timeout file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }

    static struct kobj_attribute ethdev_stats_attr = __ATTR(stats, 0644, ethdev_stats_show, ethdev_stats_store);
    err = sysfs_create_file(&eth_ent->kobj, &ethdev_stats_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create stats file");
        kobject_put(&eth_ent->kobj);
        kfree(eth_ent);
        return err;
    }

    return 0;
}

void ipa_eth_sysfs_remove_device(struct ipa_eth_device *eth_dev)
{
    kobject_del(eth_dev->sfs);
}

int ipa_eth_sysfs_add_offload_driver(struct ipa_eth_offload_driver *od)
{
    if (eth_off_kobj) {
        od->kobj = kobject_create_and_add(od->name, eth_off_kobj);
        if (IS_ERR_OR_NULL(od->kobj)) {
            ipa_eth_log("Failed to create sysfs root");
            return -EFAULT;
        }
    }
    return -EFAULT;
}

void ipa_eth_sysfs_remove_offload_driver(struct ipa_eth_offload_driver *od)
{
    kobject_del(od->kobj);
    od->kobj = NULL;
}

static ssize_t ready_show(struct kobject *kobj, struct kobj_attribute *attr, char *ubuf)
{
    char *buf;
    ssize_t n = 0, size = 128;

    buf = kzalloc(size, GFP_KERNEL);
    if (!buf)
        return 0;

    n += scnprintf(&buf[n], size - n, "Offload Sub-system: %s\n", test_bit(IPA_ETH_ST_READY, &ipa_eth_state) ? "Ready" : "Not Ready");
    n += scnprintf(&buf[n], size - n, "IPA API: %s\n", test_bit(IPA_ETH_ST_API_READY, &ipa_eth_state) ? "Ready" : "Not Ready");
    n += scnprintf(&buf[n], size - n, "ALL: %s\n", ipa_eth_all_ready() ? "Ready" : "Not Ready");

    memcpy(ubuf, buf, n);
    kfree(buf);

    return n;
}

static ssize_t ready_store(struct kobject *kobj, struct kobj_attribute *attr, const char *ubuf, size_t size)
{
    return -EINVAL;
}

int ipa_eth_sysfs_init(void)
{
    int err = 0;
    eth_kobj = kobject_create_and_add("ethernet", kernel_kobj);

    if (IS_ERR_OR_NULL(eth_kobj))
        return -ENOMEM;

    static struct kobj_attribute ready_attr = __ATTR(ready, 0644, ready_show, ready_store);
    static struct kobj_attribute no_auto_attr = __ATTR(no_auto, 0644, no_auto_show, no_auto_store);
#ifdef CONFIG_IPC_LOGGING
    static struct kobj_attribute ipc_logdbg_attr = __ATTR(ipc_logdbg, 0644, ipc_logdbg_show, ipc_logdbg_store);
#endif
    err = sysfs_create_file(eth_kobj, &ready_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create ready file\n");
    }

    err = sysfs_create_file(eth_kobj, &no_auto_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create no_auto file\n");
    }
#ifdef CONFIG_IPC_LOGGING
    err = sysfs_create_file(eth_kobj, &ipc_logdbg_attr.attr);
    if (err) {
        ipa_eth_log("Unable to create ipc_logdbg file\n");
    }
#endif
    eth_dev_kobj = kobject_create_and_add("devices", eth_kobj);
    if (IS_ERR_OR_NULL(eth_dev_kobj))
        return -ENOMEM;

    eth_drv_kobj = kobject_create_and_add("drivers", eth_kobj);
    if (IS_ERR_OR_NULL(eth_drv_kobj))
        return -ENOMEM;

    eth_bus_kobj = kobject_create_and_add("bus", eth_kobj);
    if (IS_ERR_OR_NULL(eth_bus_kobj))
        return -ENOMEM;

    eth_pci_kobj = kobject_create_and_add("pci", eth_kobj);
    if (IS_ERR_OR_NULL(eth_pci_kobj))
        return -ENOMEM;

    eth_off_kobj = kobject_create_and_add("offload", eth_kobj);
    if (IS_ERR_OR_NULL(eth_off_kobj))
        return -ENOMEM;

    ipa_eth_log("Debugfs root is initialized");

    return 0;

err_exit:
    ipa_eth_sysfs_cleanup();
    return -EFAULT;
}

void ipa_eth_sysfs_cleanup(void)
{
    kobject_del(eth_dev_kobj);
    kobject_del(eth_drv_kobj);
    kobject_del(eth_bus_kobj);
    kobject_del(eth_pci_kobj);
    kobject_del(eth_off_kobj);
    kobject_del(eth_kobj);
}

#endif // CONFIG_DEBUG_FS
