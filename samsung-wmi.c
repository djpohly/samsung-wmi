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

MODULE_AUTHOR("Devin J. Pohly");
MODULE_DESCRIPTION("WMI extras for Samsung laptops");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

#define SAMSUNG_WMI_GUID	"C16C47BA-50E3-444A-AF3A-B1C348380001"

MODULE_ALIAS("wmi:"SAMSUNG_WMI_GUID);


static int __init
samsung_wmi_init(void)
{
	pr_info("Loading module\n");

	/* Ensure that the required interface is present */
	if (!wmi_has_guid(SAMSUNG_WMI_GUID))
		return -ENODEV;

	return 0;
}

static void __exit
samsung_wmi_exit(void)
{
	pr_info("Module unloaded\n");
}

module_init(samsung_wmi_init);
module_exit(samsung_wmi_exit);
