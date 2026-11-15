// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX374 CMOS Image Sensor driver
 *
 * Register sequences extracted from Samsung Galaxy S22 (r0q) CamX binary.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include "imx374-regs.h"

#define IMX374_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX374_CHIP_ID			0x0374

#define IMX374_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX374_MODE_STANDBY		0x00
#define IMX374_MODE_STREAMING		0x01

#define IMX374_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX374_EXPOSURE_MIN		4
#define IMX374_EXPOSURE_STEP		1
#define IMX374_EXPOSURE_MARGIN		10

#define IMX374_REG_ANALOGUE_GAIN	CCI_REG16(0x0204)
#define IMX374_ANA_GAIN_MIN		0
#define IMX374_ANA_GAIN_MAX		978
#define IMX374_ANA_GAIN_STEP		1
#define IMX374_ANA_GAIN_DEFAULT		0

#define IMX374_REG_VTS			CCI_REG16(0x0340)
#define IMX374_VTS_MAX			0xffff
#define IMX374_VBLANK_MIN		32

#define IMX374_XCLK_FREQ		19200000
#define IMX374_NUM_DATA_LANES		4

#define IMX374_NATIVE_WIDTH		3968
#define IMX374_NATIVE_HEIGHT		2736
#define IMX374_ACTIVE_LEFT		168
#define IMX374_ACTIVE_TOP		0
#define IMX374_ACTIVE_WIDTH		3648
#define IMX374_ACTIVE_HEIGHT		2736

static const char * const imx374_supply_names[] = {
	"vana",
	"vio",
	"vdig",
};

#define IMX374_NUM_SUPPLIES		ARRAY_SIZE(imx374_supply_names)

static const s64 imx374_link_freq_menu[] = {
	1046500000ULL,
};

struct imx374_mode {
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

static const struct imx374_mode imx374_modes[] = {
	{
		.width = 1616,
		.height = 912,
		.hts = 19504,
		.vts_def = 1432,
		.pixel_rate = 837200000ULL,
		.link_freq_idx = 0,
		.crop = {
			.left = IMX374_ACTIVE_LEFT,
			.top = IMX374_ACTIVE_TOP,
			.width = IMX374_ACTIVE_WIDTH,
			.height = IMX374_ACTIVE_HEIGHT,
		},
		.regs = imx374_mode_1616x912_30fps_regs,
		.num_regs = ARRAY_SIZE(imx374_mode_1616x912_30fps_regs),
	},
	{
		.width = 3648,
		.height = 2736,
		.hts = 9864,
		.vts_def = 2826,
		.pixel_rate = 837200000ULL,
		.link_freq_idx = 0,
		.crop = {
			.left = IMX374_ACTIVE_LEFT,
			.top = IMX374_ACTIVE_TOP,
			.width = IMX374_ACTIVE_WIDTH,
			.height = IMX374_ACTIVE_HEIGHT,
		},
		.regs = imx374_mode_3648x2736_30fps_regs,
		.num_regs = ARRAY_SIZE(imx374_mode_3648x2736_30fps_regs),
	},
};

struct imx374 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct device *dev;
	struct regmap *regmap;

	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX374_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;

	const struct imx374_mode *cur_mode;
	struct device *lens_dev;
};

static void imx374_pm_get_dep(struct device *dev, const char *prop,
			      struct device **out)
{
	struct device_node *np;
	struct i2c_client *client;
	int ret;

	if (*out)
		return;

	np = of_parse_phandle(dev->of_node, prop, 0);
	if (!np)
		return;

	client = of_find_i2c_device_by_node(np);
	of_node_put(np);
	if (!client)
		return;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0) {
		put_device(&client->dev);
		return;
	}

	*out = &client->dev;
}

static void imx374_pm_put_dep(struct device **dep)
{
	if (*dep) {
		pm_runtime_put_autosuspend(*dep);
		put_device(*dep);
		*dep = NULL;
	}
}

static inline struct imx374 *to_imx374(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx374, sd);
}

static int imx374_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx374 *imx374 = to_imx374(sd);
	int ret;

	ret = regulator_bulk_enable(IMX374_NUM_SUPPLIES, imx374->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	fsleep(1000);

	ret = clk_prepare_enable(imx374->xclk);
	if (ret) {
		dev_err(dev, "failed to enable xclk: %d\n", ret);
		goto err_regulators;
	}

	fsleep(1000);

	gpiod_set_value_cansleep(imx374->reset_gpio, 0);
	fsleep(10000);

	return 0;

err_regulators:
	regulator_bulk_disable(IMX374_NUM_SUPPLIES, imx374->supplies);
	return ret;
}

static int imx374_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx374 *imx374 = to_imx374(sd);

	gpiod_set_value_cansleep(imx374->reset_gpio, 1);
	clk_disable_unprepare(imx374->xclk);
	regulator_bulk_disable(IMX374_NUM_SUPPLIES, imx374->supplies);

	return 0;
}

static int imx374_identify_module(struct imx374 *imx374)
{
	u64 val;
	int ret;

	ret = cci_read(imx374->regmap, IMX374_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(imx374->dev, "failed to read chip ID: %d\n", ret);
		return ret;
	}

	if (val != IMX374_CHIP_ID) {
		dev_err(imx374->dev, "chip ID mismatch: expected 0x%04x, got 0x%04llx\n",
			IMX374_CHIP_ID, val);
		return -ENXIO;
	}

	dev_info(imx374->dev, "detected IMX374 sensor (ID 0x%04llx)\n", val);
	return 0;
}

static int imx374_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx374 *imx374 = container_of(ctrl->handler, struct imx374,
					     ctrl_handler);
	const struct imx374_mode *mode = imx374->cur_mode;
	s64 exposure_max;
	int pm_status;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = mode->height + ctrl->val - IMX374_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(imx374->exposure, IMX374_EXPOSURE_MIN,
					 exposure_max, IMX374_EXPOSURE_STEP,
					 min_t(s64, imx374->exposure->val, exposure_max));
	}

	pm_status = pm_runtime_get_if_in_use(imx374->dev);
	if (pm_status <= 0)
		return pm_status < 0 ? pm_status : 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(imx374->regmap, IMX374_REG_ANALOGUE_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(imx374->regmap, IMX374_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(imx374->regmap, IMX374_REG_VTS,
			  mode->height + ctrl->val, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(imx374->dev);
	return ret;
}

static const struct v4l2_ctrl_ops imx374_ctrl_ops = {
	.s_ctrl = imx374_set_ctrl,
};

static int imx374_init_controls(struct imx374 *imx374)
{
	struct v4l2_ctrl_handler *hdlr = &imx374->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	const struct imx374_mode *mode = imx374->cur_mode;
	s64 hblank_def = mode->hts - mode->width;
	s64 vblank_def = mode->vts_def - mode->height;
	s64 exposure_max = mode->vts_def - IMX374_EXPOSURE_MARGIN;
	int ret;

	ret = v4l2_ctrl_handler_init(hdlr, 8);
	if (ret)
		return ret;

	imx374->link_freq = v4l2_ctrl_new_int_menu(hdlr, &imx374_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(imx374_link_freq_menu) - 1,
						   mode->link_freq_idx,
						   imx374_link_freq_menu);
	if (imx374->link_freq)
		imx374->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx374->pixel_rate = v4l2_ctrl_new_std(hdlr, &imx374_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       mode->pixel_rate, mode->pixel_rate,
					       1, mode->pixel_rate);

	imx374->hblank = v4l2_ctrl_new_std(hdlr, &imx374_ctrl_ops,
					   V4L2_CID_HBLANK, hblank_def, hblank_def,
					   1, hblank_def);
	if (imx374->hblank)
		imx374->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx374->vblank = v4l2_ctrl_new_std(hdlr, &imx374_ctrl_ops,
					   V4L2_CID_VBLANK, IMX374_VBLANK_MIN,
					   IMX374_VTS_MAX - mode->height, 1,
					   vblank_def);

	imx374->exposure = v4l2_ctrl_new_std(hdlr, &imx374_ctrl_ops,
					     V4L2_CID_EXPOSURE, IMX374_EXPOSURE_MIN,
					     exposure_max, IMX374_EXPOSURE_STEP,
					     min_t(s64, 800, exposure_max));

	imx374->gain = v4l2_ctrl_new_std(hdlr, &imx374_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 IMX374_ANA_GAIN_MIN, IMX374_ANA_GAIN_MAX,
					 IMX374_ANA_GAIN_STEP, IMX374_ANA_GAIN_DEFAULT);

	ret = v4l2_fwnode_device_parse(imx374->dev, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	ret = v4l2_ctrl_new_fwnode_properties(hdlr, &imx374_ctrl_ops, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	if (hdlr->error) {
		ret = hdlr->error;
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	imx374->sd.ctrl_handler = hdlr;
	return 0;
}

static void imx374_fill_format(const struct imx374_mode *mode,
			       struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx374_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	const struct imx374_mode *mode = &imx374_modes[0];

	imx374_fill_format(mode, v4l2_subdev_state_get_format(state, 0));
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	return 0;
}

static int imx374_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	return 0;
}

static int imx374_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(imx374_modes) ||
	    fse->code != MEDIA_BUS_FMT_SGBRG10_1X10)
		return -EINVAL;

	fse->min_width = imx374_modes[fse->index].width;
	fse->max_width = imx374_modes[fse->index].width;
	fse->min_height = imx374_modes[fse->index].height;
	fse->max_height = imx374_modes[fse->index].height;

	return 0;
}

static int imx374_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx374 *imx374 = to_imx374(sd);
	const struct imx374_mode *mode;

	mode = v4l2_find_nearest_size(imx374_modes, ARRAY_SIZE(imx374_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	imx374_fill_format(mode, &fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (imx374->cur_mode != mode) {
		s64 hblank = mode->hts - mode->width;
		s64 vblank = mode->vts_def - mode->height;
		s64 exp_max = mode->vts_def - IMX374_EXPOSURE_MARGIN;
		s64 exp_def = min_t(s64, 800, exp_max);

		imx374->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(imx374->link_freq, mode->link_freq_idx);
		__v4l2_ctrl_modify_range(imx374->pixel_rate, mode->pixel_rate,
					 mode->pixel_rate, 1, mode->pixel_rate);
		__v4l2_ctrl_modify_range(imx374->hblank, hblank, hblank, 1, hblank);
		__v4l2_ctrl_modify_range(imx374->vblank, IMX374_VBLANK_MIN,
					 IMX374_VTS_MAX - mode->height, 1, vblank);
		__v4l2_ctrl_s_ctrl(imx374->vblank, vblank);
		__v4l2_ctrl_modify_range(imx374->exposure, IMX374_EXPOSURE_MIN,
					 exp_max, IMX374_EXPOSURE_STEP, exp_def);
		__v4l2_ctrl_s_ctrl(imx374->exposure, exp_def);
	}

	return imx374->ctrl_handler.error;
}

static int imx374_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r = (struct v4l2_rect){
			.left = 0,
			.top = 0,
			.width = IMX374_NATIVE_WIDTH,
			.height = IMX374_NATIVE_HEIGHT,
		};
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = (struct v4l2_rect){
			.left = IMX374_ACTIVE_LEFT,
			.top = IMX374_ACTIVE_TOP,
			.width = IMX374_ACTIVE_WIDTH,
			.height = IMX374_ACTIVE_HEIGHT,
		};
		return 0;
	default:
		return -EINVAL;
	}
}

static int imx374_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct imx374 *imx374 = to_imx374(sd);
	const struct imx374_mode *mode = imx374->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(imx374->dev);
	if (ret < 0)
		return ret;

	imx374_pm_get_dep(imx374->dev, "lens-focus", &imx374->lens_dev);

	ret = cci_multi_reg_write(imx374->regmap, imx374_init_regs,
				  ARRAY_SIZE(imx374_init_regs), NULL);
	if (ret)
		goto err_rpm;

	ret = cci_multi_reg_write(imx374->regmap, mode->regs, mode->num_regs, NULL);
	if (ret)
		goto err_rpm;

	ret = __v4l2_ctrl_handler_setup(&imx374->ctrl_handler);
	if (ret)
		goto err_rpm;

	ret = cci_write(imx374->regmap, IMX374_REG_MODE_SELECT,
			IMX374_MODE_STREAMING, NULL);
	if (ret)
		goto err_rpm;

	return 0;

err_rpm:
	imx374_pm_put_dep(&imx374->lens_dev);
	pm_runtime_put_autosuspend(imx374->dev);
	return ret;
}

static int imx374_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct imx374 *imx374 = to_imx374(sd);
	int ret;

	ret = cci_write(imx374->regmap, IMX374_REG_MODE_SELECT,
			IMX374_MODE_STANDBY, NULL);
	if (ret)
		dev_err(imx374->dev, "failed to stop streaming: %d\n", ret);

	imx374_pm_put_dep(&imx374->lens_dev);
	pm_runtime_put_autosuspend(imx374->dev);
	return ret;
}

static const struct v4l2_subdev_video_ops imx374_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx374_pad_ops = {
	.enum_mbus_code = imx374_enum_mbus_code,
	.enum_frame_size = imx374_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx374_set_pad_format,
	.get_selection = imx374_get_selection,
	.enable_streams = imx374_enable_streams,
	.disable_streams = imx374_disable_streams,
};

static const struct v4l2_subdev_ops imx374_subdev_ops = {
	.video = &imx374_video_ops,
	.pad = &imx374_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx374_internal_ops = {
	.init_state = imx374_init_state,
};

static int imx374_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
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

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX374_NUM_DATA_LANES) {
		dev_err(dev, "expected %d data lanes, got %u\n",
			IMX374_NUM_DATA_LANES, bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link-frequencies specified in DT\n");
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < ARRAY_SIZE(imx374_link_freq_menu); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++)
			if (bus_cfg.link_frequencies[j] == imx374_link_freq_menu[i])
				break;
		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "missing expected link frequency %lld Hz\n",
				imx374_link_freq_menu[i]);
			ret = -EINVAL;
			goto out_free;
		}
	}

out_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int imx374_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx374 *imx374;
	unsigned int i;
	int ret;

	ret = imx374_check_hwcfg(dev);
	if (ret)
		return ret;

	imx374 = devm_kzalloc(dev, sizeof(*imx374), GFP_KERNEL);
	if (!imx374)
		return -ENOMEM;

	imx374->dev = dev;
	imx374->cur_mode = &imx374_modes[0];

	v4l2_i2c_subdev_init(&imx374->sd, client, &imx374_subdev_ops);

	imx374->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx374->regmap))
		return dev_err_probe(dev, PTR_ERR(imx374->regmap),
				     "failed to init CCI regmap\n");

	imx374->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx374->xclk))
		return dev_err_probe(dev, PTR_ERR(imx374->xclk),
				     "failed to get xclk\n");

	ret = clk_set_rate(imx374->xclk, IMX374_XCLK_FREQ);
	if (ret)
		dev_warn(dev, "failed to set xclk rate to %u Hz: %d\n",
			 IMX374_XCLK_FREQ, ret);

	for (i = 0; i < IMX374_NUM_SUPPLIES; i++)
		imx374->supplies[i].supply = imx374_supply_names[i];

	ret = devm_regulator_bulk_get(dev, IMX374_NUM_SUPPLIES, imx374->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	imx374->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(imx374->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx374->reset_gpio),
				     "failed to get reset gpio\n");

	ret = imx374_power_on(dev);
	if (ret)
		return ret;

	ret = imx374_identify_module(imx374);
	if (ret)
		goto err_power_off;

	ret = imx374_init_controls(imx374);
	if (ret)
		goto err_power_off;

	imx374->sd.internal_ops = &imx374_internal_ops;
	imx374->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx374->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	imx374->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx374->sd.entity, 1, &imx374->pad);
	if (ret)
		goto err_ctrl_free;

	imx374->sd.state_lock = imx374->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx374->sd);
	if (ret)
		goto err_media_cleanup;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&imx374->sd);
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
	v4l2_subdev_cleanup(&imx374->sd);
err_media_cleanup:
	media_entity_cleanup(&imx374->sd.entity);
err_ctrl_free:
	v4l2_ctrl_handler_free(&imx374->ctrl_handler);
err_power_off:
	imx374_power_off(dev);
	return ret;
}

static void imx374_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx374 *imx374 = to_imx374(sd);

	imx374_pm_put_dep(&imx374->lens_dev);
	v4l2_async_unregister_subdev(sd);
	pm_runtime_dont_use_autosuspend(imx374->dev);
	pm_runtime_disable(imx374->dev);
	if (!pm_runtime_status_suspended(imx374->dev))
		imx374_power_off(imx374->dev);
	pm_runtime_set_suspended(imx374->dev);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&imx374->ctrl_handler);
}

static const struct dev_pm_ops imx374_pm_ops = {
	SET_RUNTIME_PM_OPS(imx374_power_off, imx374_power_on, NULL)
};

static const struct of_device_id imx374_dt_ids[] = {
	{ .compatible = "sony,imx374" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx374_dt_ids);

static struct i2c_driver imx374_i2c_driver = {
	.driver = {
		.name = "imx374",
		.pm = &imx374_pm_ops,
		.of_match_table = imx374_dt_ids,
	},
	.probe = imx374_probe,
	.remove = imx374_remove,
};
module_i2c_driver(imx374_i2c_driver);

MODULE_DESCRIPTION("Sony IMX374 CMOS image sensor driver");
MODULE_LICENSE("GPL");
