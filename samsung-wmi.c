/*
 * samsung-wmi.c - Samsung Laptop WMI Driver
 *
 * Copyright (C) 2016 Devin J. Pohly <djpohly+linux@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

MODULE_AUTHOR("Devin J. Pohly");
MODULE_DESCRIPTION("WMI extras for Samsung laptops");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

#define SAMSUNG_WMI_GUID	"C16C47BA-50E3-444A-AF3A-B1C348380001"

MODULE_ALIAS("wmi:"SAMSUNG_WMI_GUID);


static int
samsung_wmi_probe(struct platform_device *dev)
{
	pr_info("Platform device detected\n");
	return 0;
}

static int
samsung_wmi_remove(struct platform_device *dev)
{
	pr_info("Platform device removed\n");
	return 0;
}

static struct platform_driver samsung_wmi_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = samsung_wmi_probe,
	.remove = samsung_wmi_remove,
};

static struct platform_device *samsung_device;

static int __init
samsung_platform_init(void)
{
	int ret;

	pr_info("Registering platform driver\n");
	ret = platform_driver_register(&samsung_wmi_driver);
	if (ret) {
		pr_err("Failed to register platform driver (error %d)\n", -ret);
		return ret;
	}

	pr_info("Registering platform device\n");
	samsung_device = platform_device_alloc(KBUILD_MODNAME, -1);
	if (!samsung_device) {
		pr_err("Failed to allocate platform device (error %d)\n", -ret);
		platform_driver_unregister(&samsung_wmi_driver);
		return ret;
	}

	ret = platform_device_add(samsung_device);
	if (ret) {
		pr_err("Failed to add platform device (error %d)\n", -ret);
		platform_device_put(samsung_device);
		platform_driver_unregister(&samsung_wmi_driver);
		return ret;
	}

	return 0;
}

static void __exit
samsung_platform_destroy(void)
{
	platform_device_unregister(samsung_device);
	pr_info("Unregistered platform device\n");

	platform_driver_unregister(&samsung_wmi_driver);
	pr_info("Unregistered platform driver\n");
}

static int __init
samsung_wmi_init(void)
{
	int ret;

	pr_info("\n");
	pr_info("Loading module\n");

	/* Ensure that the required interface is present */
	if (!wmi_has_guid(SAMSUNG_WMI_GUID)) {
		pr_err("WMI interface not found\n");
		return -ENODEV;
	}

	/* Set up platform driver and device */
	ret = samsung_platform_init();
	if (ret)
		return ret;

	pr_info("Module loaded\n");

	return 0;
}

static void __exit
samsung_wmi_exit(void)
{
	pr_info("\n");
	pr_info("Unloading module\n");

	/* Tear down platform device and driver */
	samsung_platform_destroy();

	pr_info("Module unloaded\n");
}

module_init(samsung_wmi_init);
module_exit(samsung_wmi_exit);
