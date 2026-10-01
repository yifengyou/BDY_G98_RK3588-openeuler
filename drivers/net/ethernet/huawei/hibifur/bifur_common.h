/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BIFUR_COMMON_H__
#define BIFUR_COMMON_H__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/kdev_t.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <net/net_namespace.h>
#include "asm-generic/int-ll64.h"
#include "linux/pci.h"

#include "ossl_knl_linux.h"
#include "hinic3_nic_dev.h"
#include "ossl_knl.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_srv_nic.h"

#define BIFUR_VF_NUM 40
#define BIFUR_FILE_PATH_SIZE 50
#define BIFUR_RESOURCE_PF_SSID 0x5a1

#define BIFUR_ENABLED   1
#define BIFUR_DISABLED  0

#define PCI_DBDF(dom, bus, dev, func) \
	((((u32)(dom) << 16) | ((u32)(bus) << 8) | ((u32)(dev) << 3) | ((u32)(func) & 0x7)))
#define PCI_DBDF_DOM(dbdf) (((dbdf) >> 16) & 0xFFFF)
#define PCI_DBDF_BUS(dbdf) (((dbdf) >> 8) & 0xFF)
#define PCI_DBDF_DEVID(dbdf) (((dbdf) >> 3) & 0x1F)
#define PCI_DBDF_FUNCTION(dbdf) ((dbdf) & 0x7)
#define PCI_DBDF_DEVFN(dbdf) ((dbdf) & 0xFF)

struct bifur_cdev {
	struct cdev cdev;
	dev_t cdev_id;
	struct proc_dir_entry *proc_dir;
};

#define BIFUR_DEV_INFO(lld_dev, fmt, arg...) dev_info(&((lld_dev)->pdev->dev), "[BIFUR]" fmt, ##arg)

#define BIFUR_DEV_WARN(lld_dev, fmt, arg...) dev_warn(&((lld_dev)->pdev->dev), "[BIFUR]" fmt, ##arg)

#define BIFUR_DEV_ERR(lld_dev, fmt, arg...) dev_err(&((lld_dev)->pdev->dev), "[BIFUR]" fmt, ##arg)
#endif
