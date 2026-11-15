// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S2MPB02 camera PMIC: LDO, Buck, and Buck-Boost regulators (Galaxy S22, r0q).
 *
 * Register layout from Samsung's GPL s2mpb02 driver:
 *  - BUCK1: enable_reg = 0x0b (mask 0xc0), vsel_reg = 0x0c (mask 0xff), 400 mV + n * 6.25 mV
 *  - BUCK2: enable_reg = 0x0d (mask 0xc0), vsel_reg = 0x0e (mask 0xff), 400 mV + n * 6.25 mV
 *  - BB1:   enable_reg = 0x0f (mask 0xc0), vsel_reg = 0x10 (mask 0xff), 2600 mV + n * 12.5 mV
 *  - LDO1..5:  ctrl_reg = 0x1d + n, bit 7 = enable, bits 6:0 = vsel, 600 mV + n * 12.5 mV
 *  - LDO6..18: ctrl_reg = 0x1d + n, bit 7 = enable, bits 6:0 = vsel, 600 mV + n * 25.0 mV
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define S2MPB02_REG_ID		0x00
#define S2MPB02_REG_B1CTRL1	0x0b
#define S2MPB02_REG_B1CTRL2	0x0c
#define S2MPB02_REG_B2CTRL1	0x0d
#define S2MPB02_REG_B2CTRL2	0x0e
#define S2MPB02_REG_BB1CTRL1	0x0f
#define S2MPB02_REG_BB1CTRL2	0x10
#define S2MPB02_REG_L1CTRL	0x1e

#define S2MPB02_NUM_LDOS	18
#define S2MPB02_NUM_BUCKS	2
#define S2MPB02_NUM_BBS		1
#define S2MPB02_NUM_REGULATORS	(S2MPB02_NUM_LDOS + S2MPB02_NUM_BUCKS + S2MPB02_NUM_BBS)

#define S2MPB02_LDO_VSEL_MASK	0x7f
#define S2MPB02_LDO_ENABLE	BIT(7)
#define S2MPB02_LDO_MIN_UV	600000
#define S2MPB02_LDO_ENABLE_US	180

#define S2MPB02_BUCK_VSEL_MASK	0xff
#define S2MPB02_BUCK_ENABLE	0xc0
#define S2MPB02_BUCK_MIN_UV	400000
#define S2MPB02_BUCK_STEP_UV	6250
#define S2MPB02_BUCK_ENABLE_US	100

#define S2MPB02_BB_MIN_UV	2600000
#define S2MPB02_BB_STEP_UV	12500
#define S2MPB02_BB_ENABLE_US	210

enum s2mpb02_regulator_id {
	S2MPB02_LDO1 = 0,
	S2MPB02_BUCK1 = S2MPB02_NUM_LDOS,
	S2MPB02_BUCK2,
	S2MPB02_BB1,
};

static const struct regulator_ops s2mpb02_reg_ops = {
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
	.ops		= &s2mpb02_reg_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= S2MPB02_LDO_MIN_UV,				\
	.uV_step	= (_step),					\
	.n_voltages	= S2MPB02_LDO_VSEL_MASK + 1,			\
	.vsel_reg	= S2MPB02_REG_L1CTRL + (_n) - 1,		\
	.vsel_mask	= S2MPB02_LDO_VSEL_MASK,			\
	.enable_reg	= S2MPB02_REG_L1CTRL + (_n) - 1,		\
	.enable_mask	= S2MPB02_LDO_ENABLE,				\
	.enable_val	= S2MPB02_LDO_ENABLE,				\
	.enable_time	= S2MPB02_LDO_ENABLE_US,			\
}

#define S2MPB02_SWITCHER(_id, _name, _min, _step, _en_reg, _vs_reg, _en_us) { \
	.name		= (_name),					\
	.of_match	= of_match_ptr(_name),				\
	.regulators_node = of_match_ptr("regulators"),			\
	.id		= (_id),					\
	.ops		= &s2mpb02_reg_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= (_min),					\
	.uV_step	= (_step),					\
	.n_voltages	= S2MPB02_BUCK_VSEL_MASK + 1,			\
	.vsel_reg	= (_vs_reg),					\
	.vsel_mask	= S2MPB02_BUCK_VSEL_MASK,			\
	.enable_reg	= (_en_reg),					\
	.enable_mask	= S2MPB02_BUCK_ENABLE,				\
	.enable_val	= S2MPB02_BUCK_ENABLE,				\
	.enable_time	= (_en_us),					\
}

static const struct regulator_desc s2mpb02_regulators[S2MPB02_NUM_REGULATORS] = {
	S2MPB02_LDO(1, 12500),  S2MPB02_LDO(2, 12500),  S2MPB02_LDO(3, 12500),
	S2MPB02_LDO(4, 12500),  S2MPB02_LDO(5, 12500),  S2MPB02_LDO(6, 25000),
	S2MPB02_LDO(7, 25000),  S2MPB02_LDO(8, 25000),  S2MPB02_LDO(9, 25000),
	S2MPB02_LDO(10, 25000), S2MPB02_LDO(11, 25000), S2MPB02_LDO(12, 25000),
	S2MPB02_LDO(13, 25000), S2MPB02_LDO(14, 25000), S2MPB02_LDO(15, 25000),
	S2MPB02_LDO(16, 25000), S2MPB02_LDO(17, 25000), S2MPB02_LDO(18, 25000),
	S2MPB02_SWITCHER(S2MPB02_BUCK1, "buck1", S2MPB02_BUCK_MIN_UV,
			 S2MPB02_BUCK_STEP_UV, S2MPB02_REG_B1CTRL1,
			 S2MPB02_REG_B1CTRL2, S2MPB02_BUCK_ENABLE_US),
	S2MPB02_SWITCHER(S2MPB02_BUCK2, "buck2", S2MPB02_BUCK_MIN_UV,
			 S2MPB02_BUCK_STEP_UV, S2MPB02_REG_B2CTRL1,
			 S2MPB02_REG_B2CTRL2, S2MPB02_BUCK_ENABLE_US),
	S2MPB02_SWITCHER(S2MPB02_BB1, "bb", S2MPB02_BB_MIN_UV,
			 S2MPB02_BB_STEP_UV, S2MPB02_REG_BB1CTRL1,
			 S2MPB02_REG_BB1CTRL2, S2MPB02_BB_ENABLE_US),
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

	for (i = 0; i < S2MPB02_NUM_REGULATORS; i++) {
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, &s2mpb02_regulators[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev), "cannot register %s\n",
					     s2mpb02_regulators[i].name);
	}

	dev_info(dev, "S2MPB02 camera PMIC, ID 0x%02x (%d regulators)\n",
		 id, S2MPB02_NUM_REGULATORS);

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

MODULE_DESCRIPTION("Samsung S2MPB02 camera PMIC regulators");
MODULE_LICENSE("GPL");
