// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3FAC DDIC with AMB606AW01 6.1" 1080x2340 AMOLED panel (Galaxy S22, r0q).
 *
 * Command mode, 4 lanes, DSC 1.1 (2 slices of 540x117, 8 bpc/8 bpp).
 * Init sequence generated with linux-mdss-dsi-panel-driver-generator from the stock
 * device tree (ss_dsi_panel_S6E3FAC_AMB606AW01_FHD, fhd60hs timing).
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e3fac {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

/* Stock power-on order: vdd3 (1.8 V), vddr (1.3 V, +1 ms), vci (3.0 V, +12 ms) */
static const struct regulator_bulk_data s6e3fac_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vdd3" },
	{ .supply = "vddr" },
	{ .supply = "vci" },
};

static inline struct s6e3fac *to_s6e3fac(struct drm_panel *panel)
{
	return container_of_const(panel, struct s6e3fac, panel);
}

static void s6e3fac_reset(struct s6e3fac *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

/* Refresh rate: 120 (fhd120hs, default) or 60 (fhd60hs); same pixel clock, only VFP differs */
static unsigned int refresh = 120;
module_param(refresh, uint, 0444);
MODULE_PARM_DESC(refresh, "Refresh rate in Hz: 120 (default) or 60");

static bool s6e3fac_is_120hz(void)
{
	return refresh != 60;
}

static void s6e3fac_write_u8(struct mipi_dsi_multi_context *dsi_ctx, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };

	mipi_dsi_generic_write_multi(dsi_ctx, buf, sizeof(buf));
}

static int s6e3fac_on(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct drm_dsc_picture_parameter_set pps;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2c, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x4f, 0x98);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x09, 0x40);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0xa0, 0x98);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x0c, 0x50);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x85, 0x93);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x93, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0xac, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0x25);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x09, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0f, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x44);
	s6e3fac_write_u8(&dsi_ctx, 0x60, s6e3fac_is_120hz() ? 0x00 : 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_msleep(&dsi_ctx, 110);
	/* TE on */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0e, 0x94);
	s6e3fac_write_u8(&dsi_ctx, 0x94, s6e3fac_is_120hz() ? 0x18 : 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x19, 0xa3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xa3, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x1a, 0xcb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x2e, 0xcb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x33, 0xf2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0xf0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x02, 0xf8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5, 0x15);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x3d, 0xf4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xed, 0x00, 0x40, 0x42, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0x95, 0x92);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x92, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0f, 0xf6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf6, 0x00);
	/* Stock sends compression mode, then its own 89-byte PPS; ours is built from ctx->dsc */
	mipi_dsi_compression_mode_multi(&dsi_ctx, true);
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x21, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x2f, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	s6e3fac_write_u8(&dsi_ctx, 0xf2, s6e3fac_is_120hz() ? 0x01 : 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x08, 0xf2);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x00, 0x0c);
	s6e3fac_write_u8(&dsi_ctx, 0x60, s6e3fac_is_120hz() ? 0x00 : 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x8b, 0x98);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98,
					 0x09, 0x40, 0x09, 0x40, 0x09, 0x40,
					 0x09, 0x40, 0x09, 0x40, 0x09, 0x40,
					 0x09, 0x40, 0x09, 0x40, 0x09, 0x40,
					 0x09, 0x40, 0x09, 0x40, 0x08, 0x54,
					 0x08, 0x54, 0x01, 0x7a, 0x01, 0x7a,
					 0x01, 0x7a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x02, 0x79, 0x92);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x92,
					 0x65, 0xff, 0x01, 0x00, 0x80, 0x01,
					 0xff, 0x10, 0x99, 0xff, 0x10, 0xff,
					 0x80, 0x86, 0xee, 0x07, 0x07, 0x07,
					 0x1e, 0x1e, 0x1e, 0x28, 0x28, 0x28,
					 0x2f, 0x2f, 0x2f, 0x32, 0x32, 0x32,
					 0x1b, 0x2f, 0xe0, 0x18, 0x64);
	/* samsung,gamma_mode2_normal_tx_cmds (minus IRC, sent above): dimming, BCTRL on, TSET, WRDISBV */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0d, 0x94);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x94, 0x20, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x01, 0xb1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x14);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);

	return dsi_ctx.accum_err;
}

static void s6e3fac_dump_state(struct s6e3fac *ctx, const char *when)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	u8 mode = 0, id[3] = {};
	int r0, r1, r2, r3;

	r0 = mipi_dsi_dcs_get_power_mode(dsi, &mode);
	r1 = mipi_dsi_dcs_read(dsi, 0xda, &id[0], 1);
	r2 = mipi_dsi_dcs_read(dsi, 0xdb, &id[1], 1);
	r3 = mipi_dsi_dcs_read(dsi, 0xdc, &id[2], 1);
	dev_info(&dsi->dev, "%s: power mode 0x%02x (ret %d), id %02x %02x %02x (ret %d %d %d)\n",
		 when, mode, r0, id[0], id[1], id[2], r1, r2, r3);
}

static int s6e3fac_prepare(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}
	usleep_range(12000, 13000);

	s6e3fac_reset(ctx);

	ret = s6e3fac_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
		return ret;
	}

	s6e3fac_dump_state(ctx, "after init");

	return 0;
}

static int s6e3fac_enable(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* samsung,display_on_tx_cmds_revA */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);
	if (dsi_ctx.accum_err)
		dev_err(&ctx->dsi->dev, "display on failed: %d\n", dsi_ctx.accum_err);
	s6e3fac_dump_state(ctx, "after display on");

	return dsi_ctx.accum_err;
}

static int s6e3fac_disable(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* samsung,display_off_tx_cmds_revA */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int s6e3fac_unprepare(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);
	mipi_dsi_msleep(&dsi_ctx, 120);
	if (dsi_ctx.accum_err)
		dev_err(&ctx->dsi->dev, "Failed to un-initialize panel: %d\n",
			dsi_ctx.accum_err);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);

	return 0;
}

/* fhd120hs and fhd60hs from the stock panel DT: 60 Hz stretches the vertical front porch */
static const struct drm_display_mode s6e3fac_mode_120 = {
	.clock = (1080 + 92 + 92 + 92) * (2340 + 467 + 84 + 84) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 92,
	.hsync_end = 1080 + 92 + 92,
	.htotal = 1080 + 92 + 92 + 92,
	.vdisplay = 2340,
	.vsync_start = 2340 + 467,
	.vsync_end = 2340 + 467 + 84,
	.vtotal = 2340 + 467 + 84 + 84,
	.width_mm = 64,
	.height_mm = 140,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode s6e3fac_mode_60 = {
	.clock = (1080 + 92 + 92 + 92) * (2340 + 3441 + 84 + 85) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 92,
	.hsync_end = 1080 + 92 + 92,
	.htotal = 1080 + 92 + 92 + 92,
	.vdisplay = 2340,
	.vsync_start = 2340 + 3441,
	.vsync_end = 2340 + 3441 + 84,
	.vtotal = 2340 + 3441 + 84 + 85,
	.width_mm = 64,
	.height_mm = 140,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int s6e3fac_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, s6e3fac_is_120hz() ?
						    &s6e3fac_mode_120 : &s6e3fac_mode_60);
}

static const struct drm_panel_funcs s6e3fac_panel_funcs = {
	.prepare = s6e3fac_prepare,
	.enable = s6e3fac_enable,
	.disable = s6e3fac_disable,
	.unprepare = s6e3fac_unprepare,
	.get_modes = s6e3fac_get_modes,
};

#define S6E3FAC_MIN_DBV	6

static int s6e3fac_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	u16 brightness = backlight_get_brightness(bl);
	u8 wrdisbv[3];

	/*
	 * DBV below S6E3FAC_MIN_DBV shows a fully black screen on this panel (checked by eye on r0q),
	 * so map the whole slider onto [S6E3FAC_MIN_DBV, max]. Blanking still writes 0.
	 */
	if (!backlight_is_blank(bl))
		brightness = S6E3FAC_MIN_DBV +
			     brightness * (bl->props.max_brightness - S6E3FAC_MIN_DBV) /
			     bl->props.max_brightness;

	wrdisbv[0] = 0x51;
	wrdisbv[1] = brightness >> 8;
	wrdisbv[2] = brightness & 0xff;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	/* Stock brightness update: level key 1, BCTRL on, WRDISBV, gamma update */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_generic_write_multi(&dsi_ctx, wrdisbv, sizeof(wrdisbv));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static const struct backlight_ops s6e3fac_bl_ops = {
	.update_status = s6e3fac_bl_update_status,
};

static struct backlight_device *s6e3fac_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	/* The init sequence writes 0x51 = 0x07ff, so treat the register as 11 bits wide */
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3fac_bl_ops, &props);
}

static int s6e3fac_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fac *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3fac, panel,
				   &s6e3fac_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(s6e3fac_supplies),
					    s6e3fac_supplies, &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/* No MIPI_DSI_MODE_VIDEO: command mode panel, frames are paced by TE */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e3fac_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret)
		return ret;

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 117;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	return devm_mipi_dsi_attach(dev, dsi);
}

static const struct of_device_id s6e3fac_of_match[] = {
	{ .compatible = "samsung,s6e3fac-amb606aw01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fac_of_match);

static struct mipi_dsi_driver s6e3fac_driver = {
	.probe = s6e3fac_probe,
	.driver = {
		.name = "panel-samsung-s6e3fac",
		.of_match_table = s6e3fac_of_match,
	},
};
module_mipi_dsi_driver(s6e3fac_driver);

MODULE_DESCRIPTION("DRM driver for Samsung S6E3FAC AMB606AW01 command mode DSI panel");
MODULE_LICENSE("GPL");
