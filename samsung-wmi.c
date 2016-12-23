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
#include <linux/leds.h>
#include <linux/workqueue.h>

MODULE_AUTHOR("Devin J. Pohly");
MODULE_DESCRIPTION("WMI extras for Samsung laptops");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

#define SAMSUNG_WMI_GUID	"C16C47BA-50E3-444A-AF3A-B1C348380001"
#define SAMSUNG_WMI_INSTANCE	0
#define SAMSUNG_WMI_METHOD	0
#define SAMSUNG_WMI_MAGIC	0x5843
#define SAMSUNG_RESPONSE_LEN	21

#define SAMSUNG_FN_KBDLIGHT	0x78

MODULE_ALIAS("wmi:"SAMSUNG_WMI_GUID);

struct samsung_wmi {
	struct led_classdev kbd_backlight;
	struct work_struct kbd_work;
};

struct samsung_sabi_msg {
	u16 smfn;
	u16 ssfn;
	u8 sfcf;
	u8 sabx[16];
} __attribute__((packed));

/*
 * sabi_command
 *
 * Executes a command via Samsung's SABI interface.  This interface uses a
 * 16-bit function ID and a 16-byte input/output buffer for optional data or
 * return values.
 */
static acpi_status
sabi_command(u16 function, u8 *in, u8 *out)
{
	struct samsung_sabi_msg msg;
	struct acpi_buffer sendBuf, recvBuf;
	union acpi_object *return_obj;
	struct samsung_sabi_msg *return_msg;
	acpi_status rv;

	/* Prepare SABI message */
	memset(&msg, 0, sizeof(msg));
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
	print_hex_dump(KERN_INFO, "samsung send: ", DUMP_PREFIX_OFFSET, 16, 1,
			sendBuf.pointer, sendBuf.length, false);

	/* Execute WMI method */
	rv = wmi_evaluate_method(SAMSUNG_WMI_GUID, SAMSUNG_WMI_INSTANCE,
			SAMSUNG_WMI_METHOD , &sendBuf, &recvBuf);
	if (ACPI_FAILURE(rv))
		return rv;

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
		pr_err("Error in SABI communication\n");
		rv = AE_IO_ERROR;
		goto out_free;
	}

	/* XXX debug */
	print_hex_dump(KERN_INFO, "samsung recv: ", DUMP_PREFIX_OFFSET, 16, 1,
			return_msg->sabx, sizeof(return_msg->sabx), false);

	/* Return the output data */
	memcpy(out, return_msg->sabx, sizeof(return_msg->sabx));

out_free:
	kfree(recvBuf.pointer);
	return rv;
}

static void
samsung_update_kbd_brightness(struct work_struct *work)
{
	struct samsung_wmi *wmi = container_of(work, struct samsung_wmi,
			kbd_work);

	u8 cmd[16] = {0x82, wmi->kbd_backlight.brightness};
	acpi_status rv;

	rv = sabi_command(SAMSUNG_FN_KBDLIGHT, cmd, cmd);
	if (ACPI_FAILURE(rv))
		pr_warn("Problem setting keyboard brightness\n");
}


static void
samsung_kbd_brightness_set(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct samsung_wmi *wmi = container_of(led_cdev, struct samsung_wmi,
			kbd_backlight);

	if (brightness > led_cdev->max_brightness)
		brightness = led_cdev->max_brightness;
	led_cdev->brightness = brightness;
	schedule_work(&wmi->kbd_work);
}

static enum led_brightness
samsung_kbd_brightness_get(struct led_classdev *led_cdev)
{
	u8 cmd[16] = {0x81};
	acpi_status rv;

	rv = sabi_command(SAMSUNG_FN_KBDLIGHT, cmd, cmd);
	if (ACPI_FAILURE(rv)) {
		pr_warn("Problem getting keyboard brightness\n");
		return 0;
	}
	return cmd[0];
}

/* XXX move to init */
static struct led_classdev kbd_led = {
	.name = KBUILD_MODNAME "::kbd_backlight",
	.brightness = 0,
	.max_brightness = 0,
	.brightness_set = samsung_kbd_brightness_set,
	.brightness_get = samsung_kbd_brightness_get,
};

static void
samsung_kbd_backlight_work(struct work_struct *work)
{
	pr_info("Keyboard work function called");
}

static int __init
samsung_kbd_backlight_init(struct samsung_wmi *wmi)
{
	u8 cmd[16] = {0xbb, 0xaa};
	acpi_status rv;

	pr_info("Initializing keyboard backlight work");
	INIT_WORK(&wmi->kbd_work, samsung_kbd_backlight_work);

	pr_info("Initializing keyboard backlight\n");
	rv = sabi_command(SAMSUNG_FN_KBDLIGHT, cmd, cmd);
	if (ACPI_FAILURE(rv)) {
		pr_err("Sending message failed\n");
		return -EIO;
	}

	if (*((u16 *) cmd) != 0xccdd) {
		pr_info("No keyboard backlight support found\n");
		return 0;
	}

	pr_info("Found support for keyboard backlight; getting brightness\n");

	/* Get brightness parameters */
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x81;
	rv = sabi_command(SAMSUNG_FN_KBDLIGHT, cmd, cmd);
	if (ACPI_FAILURE(rv)) {
		pr_err("Failed to query keyboard brightness\n");
		return -EIO;
	}

	kbd_led.brightness = cmd[0];
	kbd_led.max_brightness = cmd[1];
	pr_info("Current brightness is %d/%d\n", cmd[0], cmd[1]);

	led_classdev_register(&samsung_device->dev, &kbd_led);

	return 0;
}

static void
samsung_kbd_backlight_destroy(struct samsung_wmi *wmi)
{
	pr_info("Removing keyboard backlight\n");
	led_classdev_unregister(&kbd_led);

	flush_work(wmi->kbd_work);
}


/* Platform driver definitions */

static int
samsung_wmi_probe(struct platform_device *dev)
{
	struct samsung_wmi *wmi;

	pr_info("Probing Samsung platform driver");

	wmi = kmalloc(sizeof(*wmi), GFP_KERNEL);

	/* Set up the keyboard backlight */
	ret = samsung_kbd_backlight_init(wmi);
	if (ret) {
		pr_err("Failed to initialize keyboard backlight\n");
		return ret;
	}

	return 0;
}

static int
samsung_wmi_remove(struct platform_device *dev)
{
	pr_info("Removing Samsung platform driver");
	samsung_kbd_backlight_destroy(wmi);
	return 0;
}

static struct platform_driver samsung_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = samsung_wmi_probe,
	.remove = samsung_wmi_remove,
};

static struct platform_device *samsung_device;


static int __init
samsung_wmi_platform_init(void)
{
	int ret;

	pr_info("Registering platform driver");
	ret = platform_driver_register(&samsung_driver);
	if (ret) {
		pr_err("Failed to register platform driver");
		return ret;
	}

	pr_info("Registering platform device");
	samsung_device = platform_device_alloc(KBUILD_MODNAME, -1);
	if (!samsung_device) {
		pr_err("Failed to allocate platform device");
		ret = -ENOMEM;
		goto err_device_alloc;
	}

	ret = platform_device_add(samsung_device);
	if (ret) {
		pr_err("Failed to add platform device");
		goto err_device_add;
	}

	return 0;

err_device_add:
		platform_device_put(samsung_device);
err_device_alloc:
		platform_driver_unregister(&samsung_driver);
		return ret;
}

static void __exit
samsung_wmi_platform_destroy(void)
{
	platform_device_unregister(samsung_device);
	platform_driver_unregister(&samsung_driver);
}

static int __init
samsung_wmi_init(void)
{
	int ret;

	/* Ensure that the required interface is present */
	if (!wmi_has_guid(SAMSUNG_WMI_GUID))
		return -ENODEV;

	pr_info("Loading module\n");

	/* Set up our basic platform driver and device */
	ret = samsung_wmi_platform_init();
	if (ret) {
		pr_err("Failed to initialize platform\n");
		return ret;
	}

	pr_info("Samsung laptop WMI driver loaded\n");

	return 0;
}

static void __exit
samsung_wmi_exit(void)
{
	/* XXX get wmi?? */
	samsung_wmi_platform_destroy(wmi);

	pr_info("Module unloaded\n");
}

module_init(samsung_wmi_init);
module_exit(samsung_wmi_exit);
