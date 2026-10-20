// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S2MPB02 camera PMIC: LDO regulators only (Galaxy S22, r0q).
 * Register layout from Samsung's GPL s2mpb02 driver: LDOn control register at 0x1d + n,
 * bit 7 = enable, bits 6:0 = voltage selector, 600 mV + n * 12.5 mV (LDO1-5) or 25 mV (LDO6-18).
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define S2MPB02_REG_ID		0x00
#define S2MPB02_REG_L1CTRL	0x1e
#define S2MPB02_NUM_LDOS	18
#define S2MPB02_LDO_VSEL_MASK	0x7f
#define S2MPB02_LDO_ENABLE	BIT(7)
#define S2MPB02_LDO_MIN_UV	600000
#define S2MPB02_ENABLE_TIME_US	180

static const struct regulator_ops s2mpb02_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

#define S2MPB02_LDO(_n, _step) {					\
	.name		= "ldo" #_n,					\
	.of_match	= of_match_ptr("ldo" #_n),			\
	.regulators_node = of_match_ptr("regulators"),			\
	.id		= (_n) - 1,					\
	.ops		= &s2mpb02_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= S2MPB02_LDO_MIN_UV,				\
	.uV_step	= (_step),					\
	.n_voltages	= S2MPB02_LDO_VSEL_MASK + 1,			\
	.vsel_reg	= S2MPB02_REG_L1CTRL + (_n) - 1,		\
	.vsel_mask	= S2MPB02_LDO_VSEL_MASK,			\
	.enable_reg	= S2MPB02_REG_L1CTRL + (_n) - 1,		\
	.enable_mask	= S2MPB02_LDO_ENABLE,				\
	.enable_time	= S2MPB02_ENABLE_TIME_US,			\
}

static const struct regulator_desc s2mpb02_ldos[S2MPB02_NUM_LDOS] = {
	S2MPB02_LDO(1, 12500),  S2MPB02_LDO(2, 12500),  S2MPB02_LDO(3, 12500),
	S2MPB02_LDO(4, 12500),  S2MPB02_LDO(5, 12500),  S2MPB02_LDO(6, 25000),
	S2MPB02_LDO(7, 25000),  S2MPB02_LDO(8, 25000),  S2MPB02_LDO(9, 25000),
	S2MPB02_LDO(10, 25000), S2MPB02_LDO(11, 25000), S2MPB02_LDO(12, 25000),
	S2MPB02_LDO(13, 25000), S2MPB02_LDO(14, 25000), S2MPB02_LDO(15, 25000),
	S2MPB02_LDO(16, 25000), S2MPB02_LDO(17, 25000), S2MPB02_LDO(18, 25000),
};

static const struct regmap_config s2mpb02_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= S2MPB02_REG_L1CTRL + S2MPB02_NUM_LDOS - 1,
};

static int s2mpb02_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config config = { };
	struct regmap *regmap;
	unsigned int id;
	int i, ret;

	regmap = devm_regmap_init_i2c(client, &s2mpb02_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "regmap init failed\n");

	ret = regmap_read(regmap, S2MPB02_REG_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read chip ID\n");

	config.dev = dev;
	config.regmap = regmap;

	for (i = 0; i < S2MPB02_NUM_LDOS; i++) {
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, &s2mpb02_ldos[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev), "cannot register %s\n",
					     s2mpb02_ldos[i].name);
	}

	dev_info(dev, "S2MPB02 camera PMIC, ID 0x%02x\n", id);

	return 0;
}

static const struct of_device_id s2mpb02_of_match[] = {
	{ .compatible = "samsung,s2mpb02" },
	{ }
};
MODULE_DEVICE_TABLE(of, s2mpb02_of_match);

static struct i2c_driver s2mpb02_driver = {
	.driver = {
		.name = "s2mpb02-regulator",
		.of_match_table = s2mpb02_of_match,
	},
	.probe = s2mpb02_probe,
};
module_i2c_driver(s2mpb02_driver);

MODULE_DESCRIPTION("Samsung S2MPB02 camera PMIC LDO regulators");
MODULE_LICENSE("GPL");
