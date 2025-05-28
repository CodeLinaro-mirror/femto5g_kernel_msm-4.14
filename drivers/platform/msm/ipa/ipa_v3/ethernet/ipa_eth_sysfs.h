/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
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
#ifndef _IPA_ETH_SYSFS_H_
#define _IPA_ETH_SYSFS_H_

#include "ipa_eth_i.h"

int ipa_eth_sysfs_add_device(struct ipa_eth_device *eth_dev);
void ipa_eth_sysfs_remove_device(struct ipa_eth_device *eth_dev);

int ipa_eth_sysfs_add_offload_driver(struct ipa_eth_offload_driver *od);
void ipa_eth_sysfs_remove_offload_driver(struct ipa_eth_offload_driver *od);

int ipa_eth_sysfs_init(void);
void ipa_eth_sysfs_cleanup(void);

struct eth_dev_sys_ent {
	struct kobject kobj;
	struct ipa_eth_device *eth_dev;
	bool init;
	bool start;
};

extern struct kobject *eth_dev_kobj;
extern struct kobject *eth_drv_kobj;
extern struct kobject *eth_bus_kobj;
extern struct kobject *eth_pci_kobj;
extern struct kobject *eth_off_kobj;

#endif /* _IPA_ETH_DEBUGFS_H_ */
#endif
