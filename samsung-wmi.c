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
#define SAMSUNG_WMI_MAGIC	0x5843
#define SAMSUNG_RESPONSE_LEN	21

#define SAMSUNG_FN_KBDLIGHT	0x78

MODULE_ALIAS("wmi:"SAMSUNG_WMI_GUID);

struct samsung_sabi_msg {
	u16 smfn;
	u16 ssfn;
	u8 sfcf;
	u8 sabx[16];
} __attribute__((packed));

static acpi_status sabi_command(u16 command, u8 *in, u8 *out)
{
	struct samsung_sabi_msg msg;
	struct acpi_buffer sendBuf, recvBuf;
	union acpi_object *return_obj;
	struct samsung_sabi_msg *return_msg;
	acpi_status rv;

	/* Prepare SABI message */
	memset(&msg, 0, sizeof(msg));
	msg.smfn = SAMSUNG_WMI_MAGIC;
	msg.ssfn = command;
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
	rv = wmi_evaluate_method(SAMSUNG_WMI_GUID, 0, 0, &sendBuf, &recvBuf);
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

static int __init samsung_kbd_backlight_init(void)
{
	u8 query[16] = { 0xbb, 0xaa };
	acpi_status rv;

	pr_info("Checking for keyboard backlight support\n");
	rv = sabi_command(SAMSUNG_FN_KBDLIGHT, query, query);
	if (ACPI_FAILURE(rv)) {
		pr_err("Sending message failed\n");
		return -EIO;
	}

	if (*((u16 *) query) != 0xccdd) {
		pr_info("No support for keyboard backlight\n");
		return 0;
	}

	pr_info("Found support for keyboard backlight\n");

	memset(query, 0, sizeof(query));
	query[0] = 0x82;
	query[1] = 3;
	sabi_command(SAMSUNG_FN_KBDLIGHT, query, query);

	return 0;
}

static void samsung_kbd_backlight_destroy(void)
{
	pr_info("Removing keyboard backlight\n");
}

static int __init samsung_wmi_init(void)
{
	int ret;

	/* Ensure that the required interface is present */
	if (!wmi_has_guid(SAMSUNG_WMI_GUID))
		return -ENODEV;

	pr_info("Loading Samsung laptop WMI driver...\n");

	/* Set up the keyboard backlight */
	ret = samsung_kbd_backlight_init();
	if (ret) {
		pr_err("Failed to set up keyboard backlight\n");
		return ret;
	}

	pr_info("Samsung laptop WMI driver loaded\n");

	return 0;
}

static void __exit samsung_wmi_exit(void)
{
	samsung_kbd_backlight_destroy();

	pr_info("Samsung laptop WMI driver unloaded\n");
}

module_init(samsung_wmi_init);
module_exit(samsung_wmi_exit);
