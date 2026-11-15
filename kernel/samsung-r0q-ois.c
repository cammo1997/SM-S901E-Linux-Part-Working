// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Galaxy S22 (r0q) Dual-OIS MCU power and centering driver
 *
 * Manages runtime power, reset sequencing, and centering initialization for the
 * shared wide/telephoto OIS controller (0x62 on QUP I2C18) so the actuator
 * remains powered down when no rear OIS camera is streaming.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define R0Q_OIS_REG_CTRL		0x0000
#define R0Q_OIS_CTRL_STANDBY		0x00
#define R0Q_OIS_CTRL_ACTIVE		0x01

#define R0Q_OIS_REG_MODE		0x0002
#define R0Q_OIS_MODE_CENTERING		0x05

#define R0Q_OIS_REG_CH_MUX		0x00be
#define R0Q_OIS_REG_FW_ID		0x00f8

static const char * const r0q_ois_supply_names[] = {
	"vio",
	"vana",
	"vdd",
};

#define R0Q_OIS_NUM_SUPPLIES		ARRAY_SIZE(r0q_ois_supply_names)

static const u8 r0q_ois_mux_seq[] = { 0x01, 0x03, 0x02, 0x03 };

struct r0q_ois {
	struct device *dev;
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[R0Q_OIS_NUM_SUPPLIES];
};

static int r0q_ois_write8(struct i2c_client *client, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return ret == sizeof(buf) ? 0 : -EIO;
}

static int r0q_ois_read(struct i2c_client *client, u16 reg, u8 *buf, size_t len)
{
	u8 reg_buf[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.len = sizeof(reg_buf),
			.buf = reg_buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

static int r0q_ois_hw_enable(struct r0q_ois *ois)
{
	int ret;

	ret = regulator_bulk_enable(R0Q_OIS_NUM_SUPPLIES, ois->supplies);
	if (ret) {
		dev_err(ois->dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	fsleep(5000);
	gpiod_set_value_cansleep(ois->reset_gpio, 0);
	fsleep(30000);

	return 0;
}

static void r0q_ois_hw_disable(struct r0q_ois *ois)
{
	gpiod_set_value_cansleep(ois->reset_gpio, 1);
	regulator_bulk_disable(R0Q_OIS_NUM_SUPPLIES, ois->supplies);
}

static int r0q_ois_power_on(struct device *dev)
{
	struct r0q_ois *ois = dev_get_drvdata(dev);
	unsigned int i;
	int ret;

	ret = r0q_ois_hw_enable(ois);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(r0q_ois_mux_seq); i++) {
		ret = r0q_ois_write8(ois->client, R0Q_OIS_REG_CH_MUX,
				     r0q_ois_mux_seq[i]);
		if (ret)
			goto err_hw_off;
	}

	ret = r0q_ois_write8(ois->client, R0Q_OIS_REG_CTRL,
			     R0Q_OIS_CTRL_ACTIVE);
	if (ret)
		goto err_hw_off;

	fsleep(50000);

	ret = r0q_ois_write8(ois->client, R0Q_OIS_REG_MODE,
			     R0Q_OIS_MODE_CENTERING);
	if (ret)
		goto err_hw_off;

	ret = r0q_ois_write8(ois->client, R0Q_OIS_REG_CTRL,
			     R0Q_OIS_CTRL_ACTIVE);
	if (ret)
		goto err_hw_off;

	return 0;

err_hw_off:
	dev_err(dev, "failed to initialize OIS: %d\n", ret);
	r0q_ois_hw_disable(ois);
	return ret;
}

static int r0q_ois_power_off(struct device *dev)
{
	struct r0q_ois *ois = dev_get_drvdata(dev);
	int ret;

	ret = r0q_ois_write8(ois->client, R0Q_OIS_REG_CTRL,
			     R0Q_OIS_CTRL_STANDBY);
	if (ret)
		dev_warn(dev, "failed to enter OIS standby: %d\n", ret);

	r0q_ois_hw_disable(ois);
	return 0;
}

static int r0q_ois_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct r0q_ois *ois;
	unsigned int i;
	u8 fw_id[4];
	int ret;

	ois = devm_kzalloc(dev, sizeof(*ois), GFP_KERNEL);
	if (!ois)
		return -ENOMEM;

	ois->dev = dev;
	ois->client = client;
	dev_set_drvdata(dev, ois);

	for (i = 0; i < R0Q_OIS_NUM_SUPPLIES; i++)
		ois->supplies[i].supply = r0q_ois_supply_names[i];

	ret = devm_regulator_bulk_get(dev, R0Q_OIS_NUM_SUPPLIES, ois->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ois->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ois->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ois->reset_gpio),
				     "failed to get reset gpio\n");

	ret = r0q_ois_hw_enable(ois);
	if (ret)
		return ret;

	ret = r0q_ois_read(client, R0Q_OIS_REG_FW_ID, fw_id, sizeof(fw_id));
	if (ret) {
		dev_err_probe(dev, ret, "failed to read OIS ID\n");
		r0q_ois_hw_disable(ois);
		return ret;
	}

	dev_info(dev, "detected r0q OIS MCU (ID %4ph)\n", fw_id);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_idle(dev);

	return 0;
}

static void r0q_ois_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		r0q_ois_power_off(dev);
	pm_runtime_set_suspended(dev);
}

static const struct dev_pm_ops r0q_ois_pm_ops = {
	SET_RUNTIME_PM_OPS(r0q_ois_power_off, r0q_ois_power_on, NULL)
};

static const struct of_device_id r0q_ois_dt_ids[] = {
	{ .compatible = "samsung,r0q-ois" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, r0q_ois_dt_ids);

static struct i2c_driver r0q_ois_i2c_driver = {
	.driver = {
		.name = "samsung-r0q-ois",
		.pm = &r0q_ois_pm_ops,
		.of_match_table = r0q_ois_dt_ids,
	},
	.probe = r0q_ois_probe,
	.remove = r0q_ois_remove,
};
module_i2c_driver(r0q_ois_i2c_driver);

MODULE_DESCRIPTION("Samsung Galaxy S22 (r0q) OIS MCU runtime power driver");
MODULE_LICENSE("GPL");
