// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal CS40L26 haptics driver for the Samsung Galaxy S22 (r0q): plays the
 * chip's built-in ROM effects through the DSP mailbox, no firmware load.
 * Register values from Cirrus Logic's CS40L26 drivers.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>

#define CS40L26_DEVID			0x00000000
#define CS40L26_DSP_MBOX_2		0x00013004
#define CS40L26_DSP_V1MBOX		0x00013020

#define CS40L26_DEVID_A			0x0040a260
#define CS40L26_MBOX_PREVENT_HIBER	0x02000003
#define CS40L26_MBOX_ALLOW_HIBER	0x02000004
#define CS40L26_MBOX_PAUSE		0x05000000
#define CS40L26_ROM_BANK_CMD		0x01800000
#define CS40L26_NUM_ROM_EFFECTS		39

static unsigned int rom_effect;
module_param(rom_effect, uint, 0644);
MODULE_PARM_DESC(rom_effect, "ROM effect played for rumble (0-38)");

static unsigned int retrigger_ms = 40;
module_param(retrigger_ms, uint, 0644);
MODULE_PARM_DESC(retrigger_ms, "Replay the ROM effect every N ms while a rumble lasts (0 = play once)");

struct cs40l26_rom {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct delayed_work retrigger;
	bool playing;
};

static int cs40l26_rom_write(struct i2c_client *client, u32 reg, u32 val)
{
	u8 buf[8];
	int ret, tries;

	put_unaligned_be32(reg, buf);
	put_unaligned_be32(val, buf + 4);

	/* The first access after hibernation is NACKed while the chip wakes up */
	for (tries = 0; tries < 3; tries++) {
		ret = i2c_master_send(client, buf, sizeof(buf));
		if (ret == sizeof(buf))
			return 0;
		usleep_range(1000, 2000);
	}

	return ret < 0 ? ret : -EIO;
}

static int cs40l26_rom_read(struct i2c_client *client, u32 reg, u32 *val)
{
	u8 addr[4], data[4];
	struct i2c_msg msgs[] = {
		{ .addr = client->addr, .len = 4, .buf = addr },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = 4, .buf = data },
	};
	int ret, tries;

	put_unaligned_be32(reg, addr);

	for (tries = 0; tries < 3; tries++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs)) {
			*val = get_unaligned_be32(data);
			return 0;
		}
		usleep_range(1000, 2000);
	}

	return ret < 0 ? ret : -EIO;
}

static int cs40l26_rom_trigger(struct i2c_client *client)
{
	return cs40l26_rom_write(client, CS40L26_DSP_V1MBOX,
				 CS40L26_ROM_BANK_CMD | min(rom_effect, CS40L26_NUM_ROM_EFFECTS - 1));
}

/* ROM effects are short; replay them so a long rumble feels like one continuous buzz */
static void cs40l26_rom_retrigger(struct work_struct *work)
{
	struct cs40l26_rom *vib = container_of(to_delayed_work(work), struct cs40l26_rom, retrigger);

	if (!READ_ONCE(vib->playing) || !retrigger_ms)
		return;

	if (cs40l26_rom_trigger(vib->client))
		return;

	schedule_delayed_work(&vib->retrigger, msecs_to_jiffies(retrigger_ms));
}

static void cs40l26_rom_work(struct work_struct *work)
{
	struct cs40l26_rom *vib = container_of(work, struct cs40l26_rom, work);
	struct i2c_client *client = vib->client;
	int ret;

	if (vib->playing) {
		ret = cs40l26_rom_write(client, CS40L26_DSP_V1MBOX, CS40L26_MBOX_PREVENT_HIBER);
		if (!ret) {
			usleep_range(2000, 3000);
			ret = cs40l26_rom_trigger(client);
		}
		if (!ret && retrigger_ms)
			schedule_delayed_work(&vib->retrigger, msecs_to_jiffies(retrigger_ms));
	} else {
		cancel_delayed_work_sync(&vib->retrigger);
		ret = cs40l26_rom_write(client, CS40L26_DSP_V1MBOX, CS40L26_MBOX_PAUSE);
		if (!ret)
			ret = cs40l26_rom_write(client, CS40L26_DSP_V1MBOX, CS40L26_MBOX_ALLOW_HIBER);
	}

	if (ret)
		dev_err_ratelimited(&client->dev, "mailbox write failed: %d\n", ret);
}

static int cs40l26_rom_play(struct input_dev *input, void *data, struct ff_effect *effect)
{
	struct cs40l26_rom *vib = input_get_drvdata(input);

	WRITE_ONCE(vib->playing, effect->u.rumble.strong_magnitude || effect->u.rumble.weak_magnitude);
	schedule_work(&vib->work);

	return 0;
}

static void cs40l26_rom_close(struct input_dev *input)
{
	struct cs40l26_rom *vib = input_get_drvdata(input);

	cancel_work_sync(&vib->work);
	WRITE_ONCE(vib->playing, false);
	cs40l26_rom_work(&vib->work);
}

static int cs40l26_rom_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cs40l26_rom *vib;
	u32 devid;
	int ret;

	vib = devm_kzalloc(dev, sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;

	vib->client = client;
	INIT_WORK(&vib->work, cs40l26_rom_work);
	INIT_DELAYED_WORK(&vib->retrigger, cs40l26_rom_retrigger);

	ret = cs40l26_rom_read(client, CS40L26_DEVID, &devid);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read DEVID\n");
	if (devid != CS40L26_DEVID_A)
		return dev_err_probe(dev, -ENODEV, "unexpected DEVID 0x%08x\n", devid);

	vib->input = devm_input_allocate_device(dev);
	if (!vib->input)
		return -ENOMEM;

	vib->input->name = "cs40l26-rom-vibra";
	vib->input->id.bustype = BUS_I2C;
	vib->input->close = cs40l26_rom_close;
	input_set_drvdata(vib->input, vib);
	input_set_capability(vib->input, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(vib->input, NULL, cs40l26_rom_play);
	if (ret)
		return dev_err_probe(dev, ret, "cannot create force feedback\n");

	ret = input_register_device(vib->input);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register input device\n");

	dev_info(dev, "CS40L26 (DEVID 0x%08x), ROM effect %u, retrigger %u ms\n",
		 devid, rom_effect, retrigger_ms);

	return 0;
}

static const struct i2c_device_id cs40l26_rom_id[] = {
	{ "cs40l26a" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs40l26_rom_id);

static const struct of_device_id cs40l26_rom_of_match[] = {
	{ .compatible = "cirrus,cs40l26a" },
	{ }
};
MODULE_DEVICE_TABLE(of, cs40l26_rom_of_match);

static struct i2c_driver cs40l26_rom_driver = {
	.driver = {
		.name = "cs40l26-rom-vibra",
		.of_match_table = cs40l26_rom_of_match,
	},
	.probe = cs40l26_rom_probe,
	.id_table = cs40l26_rom_id,
};
module_i2c_driver(cs40l26_rom_driver);

MODULE_DESCRIPTION("CS40L26 ROM-effect haptics (Galaxy S22)");
MODULE_LICENSE("GPL");
