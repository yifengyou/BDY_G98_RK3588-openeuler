// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - HUAWEI TECHNOLOGIES CO., LTD.
 * Author: Gonglei <arei.gonglei@huawei.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vfio.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include "vfio_platform_private.h"

#define DRIVER_VERSION  "0.10"
#define DRIVER_AUTHOR   "Gonlei <arei.gonglei@huawei.com>"
#define DRIVER_DESC     "VFIO for HISI05A1 devices - User Level meta-driver"

/* probing devices from the linux platform bus */

static struct resource *get_platform_resource(struct vfio_platform_device *vdev,
					      int num)
{
	struct platform_device *dev = (struct platform_device *) vdev->opaque;

	return platform_get_mem_or_io(dev, num);
}

static int get_platform_irq(struct vfio_platform_device *vdev, int i)
{
	struct platform_device *pdev = (struct platform_device *) vdev->opaque;

	return platform_get_irq_optional(pdev, i);
}

static int vfio_hisi05a1_init_dev(struct vfio_device *core_vdev)
{
	struct vfio_platform_device *vdev =
		container_of(core_vdev, struct vfio_platform_device, vdev);
	struct platform_device *pdev = to_platform_device(core_vdev->dev);

	vdev->opaque = (void *) pdev;
	vdev->name = pdev->name;
	vdev->flags = VFIO_DEVICE_FLAGS_PLATFORM;
	vdev->get_resource = get_platform_resource;
	vdev->get_irq = get_platform_irq;
	vdev->reset_required = false;

	return vfio_platform_init_common(vdev);
}

static void vfio_hisi05a1_release_dev(struct vfio_device *core_vdev)
{
	struct vfio_platform_device *vdev =
		container_of(core_vdev, struct vfio_platform_device, vdev);

	vfio_platform_release_common(vdev);
}

static const struct vfio_device_ops vfio_hisi05a1_ops;
static int vfio_hisi05a1_probe(struct platform_device *pdev)
{
	struct vfio_platform_device *vdev;
	int ret;

	vdev = vfio_alloc_device(vfio_platform_device, vdev, &pdev->dev,
				 &vfio_hisi05a1_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	ret = vfio_register_emulated_iommu_dev(&vdev->vdev);
	if (ret)
		goto out_put_vdev;

	pm_runtime_enable(&pdev->dev);
	dev_set_drvdata(&pdev->dev, vdev);
	return 0;

out_put_vdev:
	vfio_put_device(&vdev->vdev);
	return ret;
}

static int vfio_hisi05a1_remove(struct platform_device *pdev)
{
	struct vfio_platform_device *vdev = dev_get_drvdata(&pdev->dev);

	vfio_unregister_group_dev(&vdev->vdev);
	pm_runtime_disable(&pdev->dev);
	vfio_put_device(&vdev->vdev);
	return 0;
}

static const struct vfio_device_ops vfio_hisi05a1_ops = {
	.name		= "vfio-hisi05a1",
	.init		= vfio_hisi05a1_init_dev,
	.release	= vfio_hisi05a1_release_dev,
	.open_device	= vfio_platform_open_device,
	.close_device	= vfio_platform_close_device,
	.ioctl		= vfio_platform_ioctl,
	.read		= vfio_platform_read,
	.write		= vfio_platform_write,
	.mmap		= vfio_platform_mmap,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static struct platform_driver vfio_hisi05a1_driver = {
	.probe		= vfio_hisi05a1_probe,
	.remove		= vfio_hisi05a1_remove,
	.driver	= {
		.name	= "vfio-hisi05a1",
	},
	.driver_managed_dma = true,
};

module_platform_driver(vfio_hisi05a1_driver);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
