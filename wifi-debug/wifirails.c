// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test only: raise the WCN6855 rails to the stock S22 cnss voltages (no reflash needed).
 *   pmr735a_s2 (vdd-wlan-aon)  1.012 V   (DT min lets it sit at 0.500 V)
 *   pm8350_s12 (vdd-wlan-rfa2) 1.350 V   (DT: 1.224 V)
 *   pm8350c_s1 (vdd-wlan-rfa1) 1.900 V   (DT: 1.808 V)
 */
#include <linux/module.h>
#include <linux/regulator/consumer.h>

static const struct { const char *name; int min, max; } rails[] = {
	{ "smps2",  1012000, 1040000 },
	{ "smps12", 1350000, 1400000 },
	{ "smps1",  1900000, 2024000 },
};
static struct regulator *regs[ARRAY_SIZE(rails)];

static int __init wifirails_init(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(rails); i++) {
		regs[i] = regulator_get(NULL, rails[i].name);
		if (IS_ERR(regs[i])) {
			pr_err("wifirails: get %s: %ld\n", rails[i].name, PTR_ERR(regs[i]));
			regs[i] = NULL;
			continue;
		}
		ret = regulator_set_voltage(regs[i], rails[i].min, rails[i].max);
		pr_info("wifirails: %s set %d..%d -> ret %d, now %d uV\n", rails[i].name,
			rails[i].min, rails[i].max, ret, regulator_get_voltage(regs[i]));
		ret = regulator_enable(regs[i]);
		if (ret)
			pr_err("wifirails: enable %s: %d\n", rails[i].name, ret);
	}
	return 0;
}

static void __exit wifirails_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rails); i++) {
		if (!regs[i])
			continue;
		regulator_disable(regs[i]);
		regulator_put(regs[i]);
	}
}

module_init(wifirails_init);
module_exit(wifirails_exit);
MODULE_LICENSE("GPL");
