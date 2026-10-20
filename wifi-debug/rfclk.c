// Scratch test: vote RPMh RF_CLK1-4 on so the WLAN chip has its XO
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

static const int ids[] = { 6, 8, 10, 19 };
static struct clk *clks[4];

static int __init rfclk_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "qcom,sm8450-rpmh-clk");
	struct of_phandle_args a = { .np = np, .args_count = 1 };
	int i, ret;

	if (!np)
		return -ENODEV;
	for (i = 0; i < 4; i++) {
		a.args[0] = ids[i];
		clks[i] = of_clk_get_from_provider(&a);
		if (IS_ERR(clks[i])) {
			pr_info("rfclk: id %d err %ld\n", ids[i], PTR_ERR(clks[i]));
			clks[i] = NULL;
			continue;
		}
		ret = clk_prepare_enable(clks[i]);
		pr_info("rfclk: %s enable %d\n", __clk_get_name(clks[i]), ret);
	}
	of_node_put(np);
	return 0;
}

static void __exit rfclk_exit(void)
{
	int i;

	for (i = 0; i < 4; i++)
		if (clks[i]) {
			clk_disable_unprepare(clks[i]);
			clk_put(clks[i]);
		}
}

module_init(rfclk_init);
module_exit(rfclk_exit);
MODULE_LICENSE("GPL");
