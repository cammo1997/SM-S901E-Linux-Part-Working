// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX563 CMOS Image Sensor driver (C-PHY 3-trio)
 *
 * Register sequences extracted from Samsung Galaxy S22 (r0q) CamX binary.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include "imx563-regs.h"

#define IMX563_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX563_CHIP_ID			0x0563

#define IMX563_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX563_MODE_STANDBY		0x00
#define IMX563_MODE_STREAMING		0x01

#define IMX563_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX563_EXPOSURE_MIN		4
#define IMX563_EXPOSURE_STEP		1
#define IMX563_EXPOSURE_MARGIN		10

#define IMX563_REG_ANALOGUE_GAIN	CCI_REG16(0x0204)
#define IMX563_ANA_GAIN_MIN		0
#define IMX563_ANA_GAIN_MAX		978
#define IMX563_ANA_GAIN_STEP		1
#define IMX563_ANA_GAIN_DEFAULT		0

#define IMX563_REG_VTS			CCI_REG16(0x0340)
#define IMX563_VTS_MAX			0xffff
#define IMX563_VBLANK_MIN		32

#define IMX563_XCLK_FREQ		19200000
#define IMX563_NUM_DATA_LANES		3

#define IMX563_NATIVE_WIDTH		4032
#define IMX563_NATIVE_HEIGHT		3024

static const char * const imx563_supply_names[] = {
	"vcustom1",
	"vana",
	"vdig",
	"vio",
};

#define IMX563_NUM_SUPPLIES		ARRAY_SIZE(imx563_supply_names)

#define IMX563_PIXEL_RATE_MIN		1300147200ULL
#define IMX563_PIXEL_RATE_MAX		1562803200ULL

static const s64 imx563_link_freq_menu[] = {
	950400000ULL,
	1142400000ULL,
};

struct imx563_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts_def;
	u64 pixel_rate;
	u32 link_freq_idx;
	struct v4l2_rect crop;
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

static const struct imx563_mode imx563_modes[] = {
	{
		.width = 4032,
		.height = 3024,
		.hts = 17168,
		.vts_def = 8002,
		.pixel_rate = 1300147200ULL,
		.link_freq_idx = 0,
		.crop = {
			.left = 0,
			.top = 0,
			.width = 4032,
			.height = 3024,
		},
		.regs = imx563_mode_4032x3024_30fps_regs,
		.num_regs = ARRAY_SIZE(imx563_mode_4032x3024_30fps_regs),
	},
	{
		.width = 2016,
		.height = 1512,
		.hts = 13024,
		.vts_def = 10544,
		.pixel_rate = 1562803200ULL,
		.link_freq_idx = 1,
		.crop = {
			.left = 0,
			.top = 0,
			.width = 4032,
			.height = 3024,
		},
		.regs = imx563_mode_2016x1512_30fps_regs,
		.num_regs = ARRAY_SIZE(imx563_mode_2016x1512_30fps_regs),
	},
};

struct imx563 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct device *dev;
	struct regmap *regmap;

	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX563_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;

	const struct imx563_mode *cur_mode;
};

static inline struct imx563 *to_imx563(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx563, sd);
}

static int imx563_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx563 *imx563 = to_imx563(sd);
	int ret;

	ret = regulator_bulk_enable(IMX563_NUM_SUPPLIES, imx563->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	fsleep(1000);
	gpiod_set_value_cansleep(imx563->reset_gpio, 0);
	fsleep(2000);

	ret = clk_prepare_enable(imx563->xclk);
	if (ret) {
		dev_err(dev, "failed to enable xclk: %d\n", ret);
		goto err_reset;
	}

	fsleep(18000);
	return 0;

err_reset:
	gpiod_set_value_cansleep(imx563->reset_gpio, 1);
	regulator_bulk_disable(IMX563_NUM_SUPPLIES, imx563->supplies);
	return ret;
}

static int imx563_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx563 *imx563 = to_imx563(sd);

	gpiod_set_value_cansleep(imx563->reset_gpio, 1);
	clk_disable_unprepare(imx563->xclk);
	regulator_bulk_disable(IMX563_NUM_SUPPLIES, imx563->supplies);

	return 0;
}

static int imx563_identify_module(struct imx563 *imx563)
{
	u64 val;
	int ret;

	ret = cci_read(imx563->regmap, IMX563_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(imx563->dev, "failed to read chip ID: %d\n", ret);
		return ret;
	}

	if (val != IMX563_CHIP_ID) {
		dev_err(imx563->dev, "chip ID mismatch: expected 0x%04x, got 0x%04llx\n",
			IMX563_CHIP_ID, val);
		return -ENXIO;
	}

	dev_info(imx563->dev, "detected IMX563 sensor (ID 0x%04llx)\n", val);
	return 0;
}

static int imx563_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx563 *imx563 = container_of(ctrl->handler, struct imx563,
					     ctrl_handler);
	const struct imx563_mode *mode = imx563->cur_mode;
	s64 exposure_max;
	int pm_status;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = mode->height + ctrl->val - IMX563_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(imx563->exposure, IMX563_EXPOSURE_MIN,
					 exposure_max, IMX563_EXPOSURE_STEP,
					 min_t(s64, imx563->exposure->val, exposure_max));
	}

	pm_status = pm_runtime_get_if_in_use(imx563->dev);
	if (pm_status <= 0)
		return pm_status < 0 ? pm_status : 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(imx563->regmap, IMX563_REG_ANALOGUE_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(imx563->regmap, IMX563_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(imx563->regmap, IMX563_REG_VTS,
			  mode->height + ctrl->val, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(imx563->dev);
	return ret;
}

static const struct v4l2_ctrl_ops imx563_ctrl_ops = {
	.s_ctrl = imx563_set_ctrl,
};

static int imx563_init_controls(struct imx563 *imx563)
{
	struct v4l2_ctrl_handler *hdlr = &imx563->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	const struct imx563_mode *mode = imx563->cur_mode;
	s64 hblank_def = mode->hts - mode->width;
	s64 vblank_def = mode->vts_def - mode->height;
	s64 exposure_max = mode->vts_def - IMX563_EXPOSURE_MARGIN;
	int ret;

	ret = v4l2_ctrl_handler_init(hdlr, 8);
	if (ret)
		return ret;

	imx563->link_freq = v4l2_ctrl_new_int_menu(hdlr, &imx563_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(imx563_link_freq_menu) - 1,
						   mode->link_freq_idx,
						   imx563_link_freq_menu);
	if (imx563->link_freq)
		imx563->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx563->pixel_rate = v4l2_ctrl_new_std(hdlr, &imx563_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       IMX563_PIXEL_RATE_MIN,
					       IMX563_PIXEL_RATE_MAX,
					       1, mode->pixel_rate);

	imx563->hblank = v4l2_ctrl_new_std(hdlr, &imx563_ctrl_ops,
					   V4L2_CID_HBLANK, hblank_def, hblank_def,
					   1, hblank_def);
	if (imx563->hblank)
		imx563->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx563->vblank = v4l2_ctrl_new_std(hdlr, &imx563_ctrl_ops,
					   V4L2_CID_VBLANK, IMX563_VBLANK_MIN,
					   IMX563_VTS_MAX - mode->height, 1,
					   vblank_def);

	imx563->exposure = v4l2_ctrl_new_std(hdlr, &imx563_ctrl_ops,
					     V4L2_CID_EXPOSURE, IMX563_EXPOSURE_MIN,
					     exposure_max, IMX563_EXPOSURE_STEP,
					     min_t(s64, 1000, exposure_max));

	imx563->gain = v4l2_ctrl_new_std(hdlr, &imx563_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 IMX563_ANA_GAIN_MIN, IMX563_ANA_GAIN_MAX,
					 IMX563_ANA_GAIN_STEP, IMX563_ANA_GAIN_DEFAULT);

	ret = v4l2_fwnode_device_parse(imx563->dev, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	ret = v4l2_ctrl_new_fwnode_properties(hdlr, &imx563_ctrl_ops, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	if (hdlr->error) {
		ret = hdlr->error;
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	imx563->sd.ctrl_handler = hdlr;
	return 0;
}

static void imx563_fill_format(const struct imx563_mode *mode,
			       struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx563_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	const struct imx563_mode *mode = &imx563_modes[0];

	imx563_fill_format(mode, v4l2_subdev_state_get_format(state, 0));
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	return 0;
}

static int imx563_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int imx563_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(imx563_modes) ||
	    fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width = imx563_modes[fse->index].width;
	fse->max_width = imx563_modes[fse->index].width;
	fse->min_height = imx563_modes[fse->index].height;
	fse->max_height = imx563_modes[fse->index].height;

	return 0;
}

static int imx563_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx563 *imx563 = to_imx563(sd);
	const struct imx563_mode *mode;

	mode = v4l2_find_nearest_size(imx563_modes, ARRAY_SIZE(imx563_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	imx563_fill_format(mode, &fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (imx563->cur_mode != mode) {
		s64 hblank = mode->hts - mode->width;
		s64 vblank = mode->vts_def - mode->height;
		s64 exp_max = mode->vts_def - IMX563_EXPOSURE_MARGIN;
		s64 exp_def = min_t(s64, 1000, exp_max);

		imx563->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(imx563->link_freq, mode->link_freq_idx);
		__v4l2_ctrl_s_ctrl_int64(imx563->pixel_rate, mode->pixel_rate);
		__v4l2_ctrl_modify_range(imx563->hblank, hblank, hblank, 1, hblank);
		__v4l2_ctrl_modify_range(imx563->vblank, IMX563_VBLANK_MIN,
					 IMX563_VTS_MAX - mode->height, 1, vblank);
		__v4l2_ctrl_s_ctrl(imx563->vblank, vblank);
		__v4l2_ctrl_modify_range(imx563->exposure, IMX563_EXPOSURE_MIN,
					 exp_max, IMX563_EXPOSURE_STEP, exp_def);
		__v4l2_ctrl_s_ctrl(imx563->exposure, exp_def);
	}

	return imx563->ctrl_handler.error;
}

static int imx563_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = (struct v4l2_rect){
			.left = 0,
			.top = 0,
			.width = IMX563_NATIVE_WIDTH,
			.height = IMX563_NATIVE_HEIGHT,
		};
		return 0;
	default:
		return -EINVAL;
	}
}

static int imx563_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct imx563 *imx563 = to_imx563(sd);
	const struct imx563_mode *mode = imx563->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(imx563->dev);
	if (ret < 0)
		return ret;

	ret = cci_multi_reg_write(imx563->regmap, imx563_init_regs,
				  ARRAY_SIZE(imx563_init_regs), NULL);
	if (ret)
		goto err_rpm;

	ret = cci_multi_reg_write(imx563->regmap, imx563_master_regs,
				  ARRAY_SIZE(imx563_master_regs), NULL);
	if (ret)
		goto err_rpm;

	ret = cci_multi_reg_write(imx563->regmap, mode->regs, mode->num_regs, NULL);
	if (ret)
		goto err_rpm;

	ret = __v4l2_ctrl_handler_setup(&imx563->ctrl_handler);
	if (ret)
		goto err_rpm;

	ret = cci_write(imx563->regmap, IMX563_REG_MODE_SELECT,
			IMX563_MODE_STREAMING, NULL);
	if (ret)
		goto err_rpm;

	return 0;

err_rpm:
	pm_runtime_put_autosuspend(imx563->dev);
	return ret;
}

static int imx563_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct imx563 *imx563 = to_imx563(sd);
	int ret;

	ret = cci_write(imx563->regmap, IMX563_REG_MODE_SELECT,
			IMX563_MODE_STANDBY, NULL);
	if (ret)
		dev_err(imx563->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(imx563->dev);
	return ret;
}

static const struct v4l2_subdev_video_ops imx563_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx563_pad_ops = {
	.enum_mbus_code = imx563_enum_mbus_code,
	.enum_frame_size = imx563_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx563_set_pad_format,
	.get_selection = imx563_get_selection,
	.enable_streams = imx563_enable_streams,
	.disable_streams = imx563_disable_streams,
};

static const struct v4l2_subdev_ops imx563_subdev_ops = {
	.video = &imx563_video_ops,
	.pad = &imx563_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx563_internal_ops = {
	.init_state = imx563_init_state,
};

static int imx563_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_CPHY,
	};
	struct fwnode_handle *ep;
	unsigned int i, j;
	int ret;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!ep)
		return dev_err_probe(dev, -ENXIO, "no endpoint node found\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse endpoint\n");

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX563_NUM_DATA_LANES) {
		dev_err(dev, "expected %d C-PHY trios, got %u\n",
			IMX563_NUM_DATA_LANES, bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link-frequencies specified in DT\n");
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < ARRAY_SIZE(imx563_link_freq_menu); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++)
			if (bus_cfg.link_frequencies[j] == imx563_link_freq_menu[i])
				break;
		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "missing expected link frequency %lld Hz\n",
				imx563_link_freq_menu[i]);
			ret = -EINVAL;
			goto out_free;
		}
	}

out_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int imx563_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx563 *imx563;
	unsigned int i;
	int ret;

	ret = imx563_check_hwcfg(dev);
	if (ret)
		return ret;

	imx563 = devm_kzalloc(dev, sizeof(*imx563), GFP_KERNEL);
	if (!imx563)
		return -ENOMEM;

	imx563->dev = dev;
	imx563->cur_mode = &imx563_modes[0];

	v4l2_i2c_subdev_init(&imx563->sd, client, &imx563_subdev_ops);

	imx563->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx563->regmap))
		return dev_err_probe(dev, PTR_ERR(imx563->regmap),
				     "failed to init CCI regmap\n");

	imx563->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx563->xclk))
		return dev_err_probe(dev, PTR_ERR(imx563->xclk),
				     "failed to get xclk\n");

	ret = clk_set_rate(imx563->xclk, IMX563_XCLK_FREQ);
	if (ret)
		dev_warn(dev, "failed to set xclk rate to %u Hz: %d\n",
			 IMX563_XCLK_FREQ, ret);

	for (i = 0; i < IMX563_NUM_SUPPLIES; i++)
		imx563->supplies[i].supply = imx563_supply_names[i];

	ret = devm_regulator_bulk_get(dev, IMX563_NUM_SUPPLIES, imx563->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	imx563->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(imx563->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx563->reset_gpio),
				     "failed to get reset gpio\n");

	ret = imx563_power_on(dev);
	if (ret)
		return ret;

	ret = imx563_identify_module(imx563);
	if (ret)
		goto err_power_off;

	ret = imx563_init_controls(imx563);
	if (ret)
		goto err_power_off;

	imx563->sd.internal_ops = &imx563_internal_ops;
	imx563->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx563->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	imx563->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx563->sd.entity, 1, &imx563->pad);
	if (ret)
		goto err_ctrl_free;

	imx563->sd.state_lock = imx563->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx563->sd);
	if (ret)
		goto err_media_cleanup;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&imx563->sd);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register subdev\n");
		goto err_rpm_disable;
	}

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_idle(dev);

	return 0;

err_rpm_disable:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	v4l2_subdev_cleanup(&imx563->sd);
err_media_cleanup:
	media_entity_cleanup(&imx563->sd.entity);
err_ctrl_free:
	v4l2_ctrl_handler_free(&imx563->ctrl_handler);
err_power_off:
	imx563_power_off(dev);
	return ret;
}

static void imx563_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx563 *imx563 = to_imx563(sd);

	v4l2_async_unregister_subdev(sd);
	pm_runtime_dont_use_autosuspend(imx563->dev);
	pm_runtime_disable(imx563->dev);
	if (!pm_runtime_status_suspended(imx563->dev))
		imx563_power_off(imx563->dev);
	pm_runtime_set_suspended(imx563->dev);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&imx563->ctrl_handler);
}

static const struct dev_pm_ops imx563_pm_ops = {
	SET_RUNTIME_PM_OPS(imx563_power_off, imx563_power_on, NULL)
};

static const struct of_device_id imx563_dt_ids[] = {
	{ .compatible = "sony,imx563" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx563_dt_ids);

static struct i2c_driver imx563_i2c_driver = {
	.driver = {
		.name = "imx563",
		.pm = &imx563_pm_ops,
		.of_match_table = imx563_dt_ids,
	},
	.probe = imx563_probe,
	.remove = imx563_remove,
};
module_i2c_driver(imx563_i2c_driver);

MODULE_DESCRIPTION("Sony IMX563 CMOS image sensor driver");
MODULE_LICENSE("GPL");
