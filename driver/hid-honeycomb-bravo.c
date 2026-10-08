// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID driver for the Honeycomb Bravo Throttle Quadrant (USB 294b:1901)
 *
 * The levers, buttons and switches are handled by the standard HID input
 * layer, exactly as hid-generic would.  This driver additionally exposes the
 * 28 panel lights (autopilot buttons, landing gear indicators and the
 * annunciator panel) as LED class devices under /sys/class/leds/bravo:*.
 *
 * The lights are driven by a single unnumbered vendor-page (0xFF00) feature
 * report holding 28 one-bit usages plus 4 padding bits (4 bytes total):
 *
 *   byte 0: HDG NAV APR REV ALT VS IAS AUTOPILOT                (bit 0..7)
 *   byte 1: gear L green, L red, N green, N red, R green, R red,
 *           MASTER WARNING, ENGINE FIRE                          (bit 0..7)
 *   byte 2: LOW OIL PRESS, LOW FUEL PRESS, ANTI ICE, STARTER ENGAGED,
 *           APU, MASTER CAUTION, VACUUM, LOW HYD PRESS           (bit 0..7)
 *   byte 3: AUX FUEL PUMP, PARKING BRAKE, LOW VOLTS, DOOR        (bit 0..3)
 *
 * The layout matches the public FlyWithLua/X-Plane helpers (dpeukert's
 * honeycomb-xplane-linux, jorgeuvo's Honeycomb-Bravo-Plugin) and
 * jcorbier's XHoneycombBravo.
 */

#include <linux/hid.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define USB_VENDOR_ID_HONEYCOMB			0x294b
#define USB_DEVICE_ID_HONEYCOMB_BRAVO		0x1901

#define BRAVO_LED_BYTES		4	/* size of the LED feature report */
#define BRAVO_MAX_REPORT_LEN	64

static unsigned int report_len = BRAVO_LED_BYTES;
module_param(report_len, uint, 0444);
MODULE_PARM_DESC(report_len,
		 "Bytes of LED feature report data to send, 4..64 (default 4, as declared by the report descriptor; Windows tools pad to 64)");

struct bravo_led_info {
	const char *name;
	u8 byte;
	u8 bit;
};

static const struct bravo_led_info bravo_led_info[] = {
	/* Autopilot buttons */
	{ "bravo::ap_hdg",			0, 0 },
	{ "bravo::ap_nav",			0, 1 },
	{ "bravo::ap_apr",			0, 2 },
	{ "bravo::ap_rev",			0, 3 },
	{ "bravo::ap_alt",			0, 4 },
	{ "bravo::ap_vs",			0, 5 },
	{ "bravo::ap_ias",			0, 6 },
	{ "bravo::ap_autopilot",		0, 7 },
	/* Landing gear indicators */
	{ "bravo:green:gear_left",		1, 0 },
	{ "bravo:red:gear_left",		1, 1 },
	{ "bravo:green:gear_nose",		1, 2 },
	{ "bravo:red:gear_nose",		1, 3 },
	{ "bravo:green:gear_right",		1, 4 },
	{ "bravo:red:gear_right",		1, 5 },
	/* Annunciator panel, top row */
	{ "bravo:red:master_warning",		1, 6 },
	{ "bravo:red:engine_fire",		1, 7 },
	{ "bravo::low_oil_pressure",		2, 0 },
	{ "bravo::low_fuel_pressure",		2, 1 },
	{ "bravo::anti_ice",			2, 2 },
	{ "bravo::starter_engaged",		2, 3 },
	{ "bravo::apu",				2, 4 },
	/* Annunciator panel, bottom row */
	{ "bravo:amber:master_caution",		2, 5 },
	{ "bravo::vacuum",			2, 6 },
	{ "bravo::low_hyd_pressure",		2, 7 },
	{ "bravo::aux_fuel_pump",		3, 0 },
	{ "bravo::parking_brake",		3, 1 },
	{ "bravo::low_volts",			3, 2 },
	{ "bravo::door",			3, 3 },
};

#define BRAVO_NUM_LEDS ARRAY_SIZE(bravo_led_info)

struct bravo_device;

struct bravo_led {
	struct led_classdev cdev;
	struct bravo_device *bravo;
	const struct bravo_led_info *info;
	bool registered;
};

struct bravo_device {
	struct hid_device *hdev;
	struct work_struct work;
	spinlock_t lock;		/* protects state and removing */
	u8 state[BRAVO_LED_BYTES];
	bool removing;
	u8 *buf;			/* DMA-safe transfer buffer */
	struct bravo_led leds[BRAVO_NUM_LEDS];
};

/* Send @state to the device. Sleeps; never call from atomic context. */
static int bravo_send_state(struct bravo_device *bravo,
			    const u8 state[BRAVO_LED_BYTES])
{
	int ret;

	memset(bravo->buf, 0, 1 + BRAVO_MAX_REPORT_LEN);
	bravo->buf[0] = 0;	/* unnumbered report: id byte is stripped */
	memcpy(bravo->buf + 1, state, BRAVO_LED_BYTES);

	ret = hid_hw_raw_request(bravo->hdev, 0, bravo->buf, 1 + report_len,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		return ret;
	return 0;
}

static void bravo_work(struct work_struct *work)
{
	struct bravo_device *bravo = container_of(work, struct bravo_device,
						  work);
	u8 state[BRAVO_LED_BYTES];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&bravo->lock, flags);
	if (bravo->removing) {
		spin_unlock_irqrestore(&bravo->lock, flags);
		return;
	}
	memcpy(state, bravo->state, sizeof(state));
	spin_unlock_irqrestore(&bravo->lock, flags);

	ret = bravo_send_state(bravo, state);
	if (ret)
		hid_dbg(bravo->hdev, "failed to update LEDs: %d\n", ret);
}

/* May be called from atomic context (e.g. LED triggers): defer the I/O. */
static void bravo_led_set(struct led_classdev *cdev,
			  enum led_brightness value)
{
	struct bravo_led *led = container_of(cdev, struct bravo_led, cdev);
	struct bravo_device *bravo = led->bravo;
	u8 mask = BIT(led->info->bit);
	unsigned long flags;
	bool schedule = false;

	spin_lock_irqsave(&bravo->lock, flags);
	if (value)
		bravo->state[led->info->byte] |= mask;
	else
		bravo->state[led->info->byte] &= ~mask;
	if (!bravo->removing)
		schedule = true;
	spin_unlock_irqrestore(&bravo->lock, flags);

	if (schedule)
		schedule_work(&bravo->work);
}

static enum led_brightness bravo_led_get(struct led_classdev *cdev)
{
	struct bravo_led *led = container_of(cdev, struct bravo_led, cdev);
	struct bravo_device *bravo = led->bravo;
	unsigned long flags;
	bool on;

	spin_lock_irqsave(&bravo->lock, flags);
	on = bravo->state[led->info->byte] & BIT(led->info->bit);
	spin_unlock_irqrestore(&bravo->lock, flags);

	return on ? LED_ON : LED_OFF;
}

static void bravo_unregister_leds(struct bravo_device *bravo)
{
	int i;

	for (i = BRAVO_NUM_LEDS - 1; i >= 0; i--) {
		if (bravo->leds[i].registered) {
			led_classdev_unregister(&bravo->leds[i].cdev);
			bravo->leds[i].registered = false;
		}
	}
}

static int bravo_register_leds(struct bravo_device *bravo)
{
	struct hid_device *hdev = bravo->hdev;
	unsigned int i;
	int ret;

	for (i = 0; i < BRAVO_NUM_LEDS; i++) {
		struct bravo_led *led = &bravo->leds[i];

		led->bravo = bravo;
		led->info = &bravo_led_info[i];
		led->cdev.name = led->info->name;
		led->cdev.max_brightness = 1;
		led->cdev.brightness = LED_OFF;
		led->cdev.brightness_set = bravo_led_set;
		led->cdev.brightness_get = bravo_led_get;
		led->cdev.flags = LED_HW_PLUGGABLE;

		ret = led_classdev_register(&hdev->dev, &led->cdev);
		if (ret) {
			hid_err(hdev, "failed to register LED %s: %d\n",
				led->info->name, ret);
			return ret;
		}
		led->registered = true;
	}

	return 0;
}

/* Stop all LED I/O, turn the lights off and drop the LED devices. */
static void bravo_shutdown_leds(struct bravo_device *bravo)
{
	static const u8 all_off[BRAVO_LED_BYTES];
	unsigned long flags;

	spin_lock_irqsave(&bravo->lock, flags);
	bravo->removing = true;
	spin_unlock_irqrestore(&bravo->lock, flags);

	bravo_unregister_leds(bravo);
	cancel_work_sync(&bravo->work);

	/* Best effort: fails harmlessly if the device was unplugged. */
	bravo_send_state(bravo, all_off);
}

static int bravo_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	static const u8 all_off[BRAVO_LED_BYTES];
	struct bravo_device *bravo;
	int ret;

	if (report_len < BRAVO_LED_BYTES || report_len > BRAVO_MAX_REPORT_LEN) {
		hid_warn(hdev, "invalid report_len %u, using %u\n",
			 report_len, BRAVO_LED_BYTES);
		report_len = BRAVO_LED_BYTES;
	}

	bravo = devm_kzalloc(&hdev->dev, sizeof(*bravo), GFP_KERNEL);
	if (!bravo)
		return -ENOMEM;

	bravo->buf = devm_kzalloc(&hdev->dev, 1 + BRAVO_MAX_REPORT_LEN,
				  GFP_KERNEL);
	if (!bravo->buf)
		return -ENOMEM;

	bravo->hdev = hdev;
	spin_lock_init(&bravo->lock);
	INIT_WORK(&bravo->work, bravo_work);
	hid_set_drvdata(hdev, bravo);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed: %d\n", ret);
		return ret;
	}

	/* Only USB devices are expected; the LED path is USB-only anyway. */
	if (!hid_is_usb(hdev)) {
		hid_warn(hdev, "not a USB device, LEDs disabled\n");
		return 0;
	}

	ret = bravo_send_state(bravo, all_off);
	if (ret)
		hid_warn(hdev, "failed to reset LEDs: %d\n", ret);

	ret = bravo_register_leds(bravo);
	if (ret) {
		bravo_shutdown_leds(bravo);
		hid_hw_stop(hdev);
		return ret;
	}

	hid_info(hdev, "Honeycomb Bravo: %zu LEDs registered\n",
		 BRAVO_NUM_LEDS);
	return 0;
}

static void bravo_remove(struct hid_device *hdev)
{
	struct bravo_device *bravo = hid_get_drvdata(hdev);

	if (hid_is_usb(hdev))
		bravo_shutdown_leds(bravo);
	hid_hw_stop(hdev);
}

#ifdef CONFIG_PM
static int bravo_reset_resume(struct hid_device *hdev)
{
	struct bravo_device *bravo = hid_get_drvdata(hdev);

	/* The device lost its LED state across the reset: resend it. */
	if (hid_is_usb(hdev))
		schedule_work(&bravo->work);
	return 0;
}
#endif

static const struct hid_device_id bravo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HONEYCOMB,
			 USB_DEVICE_ID_HONEYCOMB_BRAVO) },
	{ }
};
MODULE_DEVICE_TABLE(hid, bravo_devices);

static struct hid_driver bravo_driver = {
	.name = "honeycomb-bravo",
	.id_table = bravo_devices,
	.probe = bravo_probe,
	.remove = bravo_remove,
#ifdef CONFIG_PM
	.reset_resume = bravo_reset_resume,
#endif
};
module_hid_driver(bravo_driver);

MODULE_AUTHOR("Keenin Krehbiel");
MODULE_DESCRIPTION("Honeycomb Bravo Throttle Quadrant HID driver with LED support");
MODULE_LICENSE("GPL");
