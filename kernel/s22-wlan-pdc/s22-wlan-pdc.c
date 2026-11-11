// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Galaxy S22 (SM-S901E, r0q) QCA6490/WCN6855 2.4 GHz internal PA rail
 * enable via Qualcomm AOSS QMP.
 *
 * On the S22 (dtbo qcom,use-xPA = <0>), the QCA6490 uses its internal 2.4 GHz
 * Power Amplifier powered by the 2.2 V PMR735A S3 ("s3e") rail controlled by
 * AOSS wlan_pdc. Stock Android's cnss2 (cnss_enable_int_pow_amp_vreg) and
 * btpower (btpower_enable_ipa_vreg) configure s3e.e upval=1 over AOSS QMP
 * before WLAN cold boot calibration (mode 7). Without it, 5 GHz TX/RX and
 * 2.4 GHz RX work normally, while every 2.4 GHz TX frame fails with NO_ACK.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/qcom_aoss.h>

static const char * const s22_wlan_pdc_msgs[] = {
	"{class: wlan_pdc, res: s3e.e, upval: 1}",
	"{class: wlan_pdc, ss: rf, res: s3e.e, upval: 1}",
};

static struct qmp *s22_wlan_pdc_get_qmp(struct platform_device **out_pdev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct qmp *qmp;

	for_each_node_with_property(np, "qcom,qmp") {
		pdev = of_find_device_by_node(np);
		if (!pdev)
			continue;

		qmp = qmp_get(&pdev->dev);
		if (!IS_ERR(qmp)) {
			of_node_put(np);
			*out_pdev = pdev;
			return qmp;
		}

		put_device(&pdev->dev);
	}

	return ERR_PTR(-ENODEV);
}

static int __init s22_wlan_pdc_init(void)
{
	struct platform_device *client_pdev = NULL;
	struct qmp *qmp;
	int i, ret;

	if (!of_machine_is_compatible("samsung,r0q"))
		return 0;

	qmp = s22_wlan_pdc_get_qmp(&client_pdev);
	if (IS_ERR(qmp)) {
		pr_err("s22-wlan-pdc: failed to get AOSS QMP handle: %ld\n",
		       PTR_ERR(qmp));
		return PTR_ERR(qmp);
	}

	for (i = 0; i < ARRAY_SIZE(s22_wlan_pdc_msgs); i++) {
		ret = qmp_send(qmp, "%s", s22_wlan_pdc_msgs[i]);
		if (ret) {
			pr_err("s22-wlan-pdc: QMP '%s' failed: %d\n",
			       s22_wlan_pdc_msgs[i], ret);
			goto out_put;
		}
	}

	pr_info("s22-wlan-pdc: enabled s3e 2.4 GHz internal PA rail via AOSS QMP\n");
	ret = 0;

out_put:
	qmp_put(qmp);
	put_device(&client_pdev->dev);
	return ret;
}

static void __exit s22_wlan_pdc_exit(void)
{
}

module_init(s22_wlan_pdc_init);
module_exit(s22_wlan_pdc_exit);

MODULE_DESCRIPTION("Samsung Galaxy S22 (r0q) QCA6490 2.4 GHz iPA AOSS QMP enable");
MODULE_LICENSE("GPL");
