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
#include <linux/leds.h>
#include <linux/platform_device.h>

MODULE_AUTHOR("Devin J. Pohly");
MODULE_DESCRIPTION("WMI extras for Samsung laptops");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

#define SAMSUNG_WMI_DRIVER	"samsung-wmi"
#define SAMSUNG_WMI_GUID	"C16C47BA-50E3-444A-AF3A-B1C348380001"
#define SAMSUNG_WMI_INSTANCE	0
#define SAMSUNG_WMI_METHOD	0
#define SAMSUNG_WMI_MAGIC	0x5843
#define SAMSUNG_RESPONSE_LEN	21
static const u8 SAMSUNG_QUERY_LID[16] = {0x82, 0xa3, 0x82};

MODULE_ALIAS("wmi:"SAMSUNG_WMI_GUID);

struct samsung_wmi {
	unsigned int has_kbdlight : 1;
	unsigned int has_perflevel : 1;
	unsigned int has_turbo : 1;
	unsigned int has_misc : 1;
	unsigned int has_lidcontrol : 1;
};

struct samsung_sabi_msg {
	u16 smfn;
	u16 ssfn;
	u8 sfcf;
	u8 sabx[16];
} __attribute__((packed));



/*
 * samsung_sabi_cmd
 *
 * Executes a command via Samsung's SABI interface.  This interface uses a
 * 16-bit function ID and a 16-byte input/output buffer for optional data or
 * return values.
 */
static acpi_status
samsung_sabi_cmd(u16 function, const u8 *in, u8 *out)
{
	struct samsung_sabi_msg msg;
	struct acpi_buffer sendBuf, recvBuf;
	union acpi_object *return_obj;
	struct samsung_sabi_msg *return_msg;
	acpi_status rv;

	/* Prepare SABI message */
	msg.smfn = SAMSUNG_WMI_MAGIC;
	msg.ssfn = function;
	msg.sfcf = 0;
	memcpy(msg.sabx, in, sizeof(msg.sabx));

	/* Set up send and receive ACPI buffers */
	sendBuf.length = sizeof(msg);
	sendBuf.pointer = (u8 *) &msg;
	recvBuf.length = ACPI_ALLOCATE_BUFFER;
	recvBuf.pointer = NULL;

	/* XXX debug */
	print_hex_dump(KERN_INFO, "SABI send: ", DUMP_PREFIX_OFFSET, 16, 1,
			sendBuf.pointer, sendBuf.length, false);

	/* Execute WMI method */
	rv = wmi_evaluate_method(SAMSUNG_WMI_GUID, SAMSUNG_WMI_INSTANCE,
			SAMSUNG_WMI_METHOD , &sendBuf, &recvBuf);
	if (ACPI_FAILURE(rv)) {
		pr_err("Error in SABI communication: %s\n",
				acpi_format_exception(rv));
		return rv;
	}

	/* Fetch and validate return object */
	return_obj = (union acpi_object *) recvBuf.pointer;
	if (!return_obj) {
		pr_err("Null buffer returned from SABI\n");
		rv = AE_TYPE;
		goto out_free;
	}
	if (return_obj->type != ACPI_TYPE_BUFFER) {
		pr_err("Unexpected (non-buffer) return type from SABI\n");
		rv = AE_TYPE;
		goto out_free;
	}
	if (return_obj->buffer.length > sizeof(msg)) {
		pr_err("Buffer returned from SABI too large\n");
		rv = AE_BUFFER_OVERFLOW;
		goto out_free;
	}

	/* Validate reply message */
	return_msg = (struct samsung_sabi_msg *) return_obj->buffer.pointer;
	if (return_msg->sfcf != 0xaa) {
		rv = AE_SUPPORT;
		goto out_free;
	}

	/* XXX debug */
	print_hex_dump(KERN_INFO, "SABI recv: ", DUMP_PREFIX_OFFSET, 16, 1,
			return_msg->sabx, sizeof(return_msg->sabx), false);

	/* Return the output data */
	if (out)
		memcpy(out, return_msg->sabx, sizeof(return_msg->sabx));

out_free:
	kfree(recvBuf.pointer);
	return rv;
}

/*
 * samsung_wmi_getmiscfeatures
 *
 * Probes the SABI interface for supported subfeatures under the 0x7a command.
 */
static int
samsung_wmi_getmiscfeatures(struct samsung_wmi *sammy)
{
	u8 buf[16];
	acpi_status rv;

	/* Check for lid-control support */
	rv = samsung_sabi_cmd(0x7a, SAMSUNG_QUERY_LID, buf);
	if (ACPI_FAILURE(rv))
		return -EIO;
	if (buf[2] == 0xaa) {
		sammy->has_lidcontrol = 1;
		pr_info("    . Lid control\n");
	}

	return 0;
}

/*
 * samsung_wmi_getfeatures
 *
 * Probes the SABI interface for feature support, recording the results in the
 * samsung_wmi struct.
 */
static int
samsung_wmi_getfeatures(struct samsung_wmi *sammy)
{
	u8 bbaa[16] = {0xbb, 0xaa};
	u8 buf[16];
	acpi_status rv;
	int ret;

	pr_info("Probing SABI for features\n");

	/* Check for and initialize keyboard backlight support */
	rv = samsung_sabi_cmd(0x78, bbaa, buf);
	if (ACPI_FAILURE(rv) && rv != AE_SUPPORT)
		return -EIO;
	if (rv != AE_SUPPORT && buf[0] == 0xdd && buf[1] == 0xcc) {
		sammy->has_kbdlight = 1;
		pr_info("  - Keyboard backlight\n");
	}

	/* Check for and initialize performance level support */
	rv = samsung_sabi_cmd(0x31, bbaa, buf);
	if (ACPI_FAILURE(rv) && rv != AE_SUPPORT)
		return -EIO;
	if (rv != AE_SUPPORT && buf[0] == 0xdd && buf[1] == 0xcc) {
		sammy->has_perflevel = 1;
		pr_info("  - Performance level\n");
	}

	/* Check for and initialize turbo support */
	rv = samsung_sabi_cmd(0x88, bbaa, buf);
	if (ACPI_FAILURE(rv) && rv != AE_SUPPORT)
		return -EIO;
	if (rv != AE_SUPPORT && buf[0] == 0xdd && buf[1] == 0xcc) {
		sammy->has_turbo = 1;
		pr_info("  - Turbo boost\n");
	}

	/* Check for and initialize miscellaneous settings */
	rv = samsung_sabi_cmd(0x7a, bbaa, buf);
	if (ACPI_FAILURE(rv) && rv != AE_SUPPORT)
		return -EIO;
	if (rv != AE_SUPPORT && buf[0] == 0xdd && buf[1] == 0xcc) {
		sammy->has_misc = 1;
		pr_info("  - Miscellaneous features\n");

		/* Check for miscellaneous (0x7a) subfeatures */
		ret = samsung_wmi_getmiscfeatures(sammy);
		if (ret)
			return ret;
	}

	return 0;
}

static int
samsung_kbd_backlight_init(struct samsung_wmi *sammy)
{
	pr_info("Initializing keyboard backlight\n");
	return 0;
}

static void
samsung_kbd_backlight_destroy(struct samsung_wmi *sammy)
{
	pr_info("Cleaning up keyboard backlight\n");
}

/*
 * samsung_wmi_probe
 *
 * Driver function to initialize platform device and features.  Checks first for
 * feature support, then initializes the relevant interfaces.
 */
static int
samsung_wmi_probe(struct platform_device *dev)
{
	struct samsung_wmi *sammy;
	int ret;

	pr_info("Platform device detected\n");

	sammy = kzalloc(sizeof(*sammy), GFP_KERNEL);
	if (!sammy) {
		pr_err("Failed to allocate private struct\n");
		return -ENOMEM;
	}

	ret = samsung_wmi_getfeatures(sammy);
	if (ret) {
		pr_err("Failed to probe for features (error %d)\n", -ret);
		goto err_getfeatures;
	}

	if (sammy->has_kbdlight) {
		ret = samsung_kbd_backlight_init(sammy);
		if (ret) {
			pr_err("Failed to initialize keyboard backlight"
					" (error %d)\n", -ret);
			goto err_backlight_init;
		}
	}

	platform_set_drvdata(dev, sammy);
	pr_info("Initialized platform device\n");
	return 0;

err_backlight_init:
err_getfeatures:
	kfree(sammy);
	return ret;
}

/*
 * samsung_wmi_remove
 *
 * Driver function to clean up platform device when it is removed.
 */
static int
samsung_wmi_remove(struct platform_device *dev)
{
	struct samsung_wmi *sammy;

	pr_info("Cleaning up platform device\n");

	sammy = platform_get_drvdata(dev);
	if (sammy->has_kbdlight)
		samsung_kbd_backlight_destroy(sammy);
	kfree(sammy);

	pr_info("Platform device removed\n");
	return 0;
}

/* Platform driver definition and device pointer */
static struct platform_driver samsung_wmi_driver = {
	.driver = {
		.name = SAMSUNG_WMI_DRIVER,
		.owner = THIS_MODULE,
	},
	.probe = samsung_wmi_probe,
	.remove = samsung_wmi_remove,
};
static struct platform_device *samsung_device;

/*
 * samsung_platform_init
 *
 * Registers the platform driver and creates a platform device to bind to it.
 * Features will be attached to this device.
 */
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
	samsung_device = platform_device_alloc(SAMSUNG_WMI_DRIVER, -1);
	if (!samsung_device) {
		pr_err("Failed to allocate platform device (error %d)\n", -ret);
		goto err_device_alloc;
	}

	ret = platform_device_add(samsung_device);
	if (ret) {
		pr_err("Failed to add platform device (error %d)\n", -ret);
		goto err_device_add;
	}

	return 0;

err_device_add:
	platform_device_put(samsung_device);
err_device_alloc:
	platform_driver_unregister(&samsung_wmi_driver);
	return ret;
}

/*
 * samsung_platform_destroy
 *
 * Removes the platform device and unregisters the platform driver.
 */
static void __exit
samsung_platform_destroy(void)
{
	platform_device_unregister(samsung_device);
	pr_info("Unregistered platform device\n");

	platform_driver_unregister(&samsung_wmi_driver);
	pr_info("Unregistered platform driver\n");
}

/*
 * samsung_wmi_init
 *
 * Checks for the Samsung WMI interface and initializes the driver.
 */
static int __init
samsung_wmi_init(void)
{
	int ret;

	pr_info("\n");
	pr_info("Loading module\n");

	/* Ensure that the required WMI method is present */
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

/*
 * samsung_wmi_exit
 *
 * Cleans up the Samsung WMI driver.
 */
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
