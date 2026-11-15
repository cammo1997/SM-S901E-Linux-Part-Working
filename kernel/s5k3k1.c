// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S5K3K1 CMOS Image Sensor driver
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
#include <linux/unaligned.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include "s5k3k1-regs.h"

#define S5K3K1_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5K3K1_CHIP_ID			0x30b1

#define S5K3K1_REG_MODE_SELECT		CCI_REG16(0x0100)
#define S5K3K1_MODE_STANDBY		0x0000
#define S5K3K1_MODE_STREAMING		0x0103

#define S5K3K1_REG_BURST		0x6f12
#define S5K3K1_BURST_MAX_WORDS		128

#define S5K3K1_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5K3K1_EXPOSURE_MIN		4
#define S5K3K1_EXPOSURE_STEP		1
#define S5K3K1_EXPOSURE_MARGIN		8

#define S5K3K1_REG_ANALOGUE_GAIN	CCI_REG16(0x0204)
#define S5K3K1_ANA_GAIN_MIN		0x0020
#define S5K3K1_ANA_GAIN_MAX		0x0200
#define S5K3K1_ANA_GAIN_STEP		1
#define S5K3K1_ANA_GAIN_DEFAULT		0x0020

#define S5K3K1_REG_VTS			CCI_REG16(0x0340)
#define S5K3K1_VTS_MAX			0xffff
#define S5K3K1_VBLANK_MIN		32

#define S5K3K1_XCLK_FREQ		19200000
#define S5K3K1_NUM_DATA_LANES		4

#define S5K3K1_NATIVE_WIDTH		3840
#define S5K3K1_NATIVE_HEIGHT		2880
#define S5K3K1_ACTIVE_LEFT		88
#define S5K3K1_ACTIVE_TOP		72
#define S5K3K1_ACTIVE_WIDTH		3648
#define S5K3K1_ACTIVE_HEIGHT		2736

static const char * const s5k3k1_supply_names[] = {
	"vio",
	"vana",
	"vdig",
};

#define S5K3K1_NUM_SUPPLIES		ARRAY_SIZE(s5k3k1_supply_names)

static const s64 s5k3k1_link_freq_menu[] = {
	1151700000ULL,
};

struct s5k3k1_mode {
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

static const struct s5k3k1_mode s5k3k1_modes[] = {
	{
		.width = 3648,
		.height = 2736,
		.hts = 4320,
		.vts_def = 6146,
		.pixel_rate = 921360000ULL,
		.link_freq_idx = 0,
		.crop = {
			.left = 88,
			.top = 72,
			.width = 3648,
			.height = 2736,
		},
		.regs = s5k3k1_mode_3648x2736_30fps_regs,
		.num_regs = ARRAY_SIZE(s5k3k1_mode_3648x2736_30fps_regs),
	},
	{
		.width = 1824,
		.height = 1368,
		.hts = 2375,
		.vts_def = 11178,
		.pixel_rate = 921360000ULL,
		.link_freq_idx = 0,
		.crop = {
			.left = 88,
			.top = 64,
			.width = 3648,
			.height = 2736,
		},
		.regs = s5k3k1_mode_1824x1368_30fps_regs,
		.num_regs = ARRAY_SIZE(s5k3k1_mode_1824x1368_30fps_regs),
	},
};

struct s5k3k1 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct device *dev;
	struct regmap *regmap;

	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[S5K3K1_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;

	const struct s5k3k1_mode *cur_mode;
	const struct s5k3k1_mode *streamed_mode;
	struct device *lens_dev;
	struct device *ois_dev;
};

static inline struct s5k3k1 *to_s5k3k1(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k3k1, sd);
}

static unsigned int s5k3k1_burst_max_words(struct i2c_client *client)
{
	const struct i2c_adapter_quirks *quirks = client->adapter->quirks;

	if (quirks && quirks->max_write_len) {
		if (quirks->max_write_len <= 2)
			return 1;
		return min_t(unsigned int, S5K3K1_BURST_MAX_WORDS,
			     (quirks->max_write_len - 2) / 2);
	}

	return S5K3K1_BURST_MAX_WORDS;
}

static int s5k3k1_send_burst(struct i2c_client *client, u8 *buf,
			     unsigned int words)
{
	int len = 2 + words * 2;
	int ret;

	put_unaligned_be16(S5K3K1_REG_BURST, buf);
	ret = i2c_master_send(client, buf, len);
	if (ret < 0)
		return ret;

	return ret == len ? 0 : -EIO;
}

static int s5k3k1_write_seq(struct s5k3k1 *s5k3k1,
			    const struct s5k3k1_reg_seq *regs,
			    unsigned int count)
{
	struct i2c_client *client = to_i2c_client(s5k3k1->dev);
	unsigned int max_words = s5k3k1_burst_max_words(client);
	u8 buf[2 + S5K3K1_BURST_MAX_WORDS * 2];
	unsigned int i = 0;
	int ret;

	while (i < count) {
		if (regs[i].reg == CCI_REG16(S5K3K1_REG_BURST) &&
		    !regs[i].delay_us && max_words > 1) {
			unsigned int chunk = 0;

			while (chunk < max_words && i + chunk < count &&
			       regs[i + chunk].reg == CCI_REG16(S5K3K1_REG_BURST) &&
			       !regs[i + chunk].delay_us) {
				put_unaligned_be16(regs[i + chunk].val,
						   &buf[2 + chunk * 2]);
				chunk++;
			}

			if (chunk > 1) {
				ret = s5k3k1_send_burst(client, buf, chunk);
				if (ret)
					return ret;
				i += chunk;
				continue;
			}
		}

		ret = cci_write(s5k3k1->regmap, regs[i].reg, regs[i].val, NULL);
		if (ret)
			return ret;

		if (regs[i].delay_us)
			fsleep(regs[i].delay_us);

		i++;
	}

	return 0;
}

static void s5k3k1_pm_get_dep(struct device *dev, const char *prop,
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

static void s5k3k1_pm_put_dep(struct device **dep)
{
	if (*dep) {
		pm_runtime_put_autosuspend(*dep);
		put_device(*dep);
		*dep = NULL;
	}
}

static int s5k3k1_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);
	int ret;

	ret = regulator_bulk_enable(S5K3K1_NUM_SUPPLIES, s5k3k1->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	fsleep(1000);

	ret = clk_prepare_enable(s5k3k1->xclk);
	if (ret) {
		dev_err(dev, "failed to enable xclk: %d\n", ret);
		goto err_regulators;
	}

	fsleep(1000);

	gpiod_set_value_cansleep(s5k3k1->reset_gpio, 0);
	fsleep(10000);

	return 0;

err_regulators:
	regulator_bulk_disable(S5K3K1_NUM_SUPPLIES, s5k3k1->supplies);
	return ret;
}

static int s5k3k1_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);

	s5k3k1->streamed_mode = NULL;
	gpiod_set_value_cansleep(s5k3k1->reset_gpio, 1);
	clk_disable_unprepare(s5k3k1->xclk);
	regulator_bulk_disable(S5K3K1_NUM_SUPPLIES, s5k3k1->supplies);

	return 0;
}

static int s5k3k1_identify_module(struct s5k3k1 *s5k3k1)
{
	u64 val;
	int ret;

	ret = cci_read(s5k3k1->regmap, S5K3K1_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(s5k3k1->dev, "failed to read chip ID: %d\n", ret);
		return ret;
	}

	if (val != S5K3K1_CHIP_ID) {
		dev_err(s5k3k1->dev, "chip ID mismatch: expected 0x%04x, got 0x%04llx\n",
			S5K3K1_CHIP_ID, val);
		return -ENXIO;
	}

	dev_info(s5k3k1->dev, "detected S5K3K1 sensor (ID 0x%04llx)\n", val);
	return 0;
}

static int s5k3k1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k3k1 *s5k3k1 = container_of(ctrl->handler, struct s5k3k1,
					     ctrl_handler);
	const struct s5k3k1_mode *mode = s5k3k1->cur_mode;
	s64 exposure_max;
	int pm_status;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = mode->height + ctrl->val - S5K3K1_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(s5k3k1->exposure, S5K3K1_EXPOSURE_MIN,
					 exposure_max, S5K3K1_EXPOSURE_STEP,
					 min_t(s64, s5k3k1->exposure->val, exposure_max));
	}

	pm_status = pm_runtime_get_if_in_use(s5k3k1->dev);
	if (pm_status <= 0)
		return pm_status < 0 ? pm_status : 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(s5k3k1->regmap, S5K3K1_REG_ANALOGUE_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(s5k3k1->regmap, S5K3K1_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(s5k3k1->regmap, S5K3K1_REG_VTS,
			  mode->height + ctrl->val, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(s5k3k1->dev);
	return ret;
}

static const struct v4l2_ctrl_ops s5k3k1_ctrl_ops = {
	.s_ctrl = s5k3k1_set_ctrl,
};

static int s5k3k1_init_controls(struct s5k3k1 *s5k3k1)
{
	struct v4l2_ctrl_handler *hdlr = &s5k3k1->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	const struct s5k3k1_mode *mode = s5k3k1->cur_mode;
	s64 hblank_def = mode->hts - mode->width;
	s64 vblank_def = mode->vts_def - mode->height;
	s64 exposure_max = mode->vts_def - S5K3K1_EXPOSURE_MARGIN;
	int ret;

	ret = v4l2_ctrl_handler_init(hdlr, 8);
	if (ret)
		return ret;

	s5k3k1->link_freq = v4l2_ctrl_new_int_menu(hdlr, &s5k3k1_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(s5k3k1_link_freq_menu) - 1,
						   mode->link_freq_idx,
						   s5k3k1_link_freq_menu);
	if (s5k3k1->link_freq)
		s5k3k1->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k3k1->pixel_rate = v4l2_ctrl_new_std(hdlr, &s5k3k1_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       mode->pixel_rate, mode->pixel_rate,
					       1, mode->pixel_rate);

	s5k3k1->hblank = v4l2_ctrl_new_std(hdlr, &s5k3k1_ctrl_ops,
					   V4L2_CID_HBLANK, hblank_def, hblank_def,
					   1, hblank_def);
	if (s5k3k1->hblank)
		s5k3k1->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k3k1->vblank = v4l2_ctrl_new_std(hdlr, &s5k3k1_ctrl_ops,
					   V4L2_CID_VBLANK, S5K3K1_VBLANK_MIN,
					   S5K3K1_VTS_MAX - mode->height, 1,
					   vblank_def);

	s5k3k1->exposure = v4l2_ctrl_new_std(hdlr, &s5k3k1_ctrl_ops,
					     V4L2_CID_EXPOSURE, S5K3K1_EXPOSURE_MIN,
					     exposure_max, S5K3K1_EXPOSURE_STEP,
					     min_t(s64, 1000, exposure_max));

	s5k3k1->gain = v4l2_ctrl_new_std(hdlr, &s5k3k1_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 S5K3K1_ANA_GAIN_MIN, S5K3K1_ANA_GAIN_MAX,
					 S5K3K1_ANA_GAIN_STEP, S5K3K1_ANA_GAIN_DEFAULT);

	ret = v4l2_fwnode_device_parse(s5k3k1->dev, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	ret = v4l2_ctrl_new_fwnode_properties(hdlr, &s5k3k1_ctrl_ops, &props);
	if (ret) {
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	if (hdlr->error) {
		ret = hdlr->error;
		v4l2_ctrl_handler_free(hdlr);
		return ret;
	}

	s5k3k1->sd.ctrl_handler = hdlr;
	return 0;
}

static void s5k3k1_fill_format(const struct s5k3k1_mode *mode,
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

static int s5k3k1_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	const struct s5k3k1_mode *mode = &s5k3k1_modes[0];

	s5k3k1_fill_format(mode, v4l2_subdev_state_get_format(state, 0));
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	return 0;
}

static int s5k3k1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	return 0;
}

static int s5k3k1_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(s5k3k1_modes) ||
	    fse->code != MEDIA_BUS_FMT_SGBRG10_1X10)
		return -EINVAL;

	fse->min_width = s5k3k1_modes[fse->index].width;
	fse->max_width = s5k3k1_modes[fse->index].width;
	fse->min_height = s5k3k1_modes[fse->index].height;
	fse->max_height = s5k3k1_modes[fse->index].height;

	return 0;
}

static int s5k3k1_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);
	const struct s5k3k1_mode *mode;

	mode = v4l2_find_nearest_size(s5k3k1_modes, ARRAY_SIZE(s5k3k1_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	s5k3k1_fill_format(mode, &fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (s5k3k1->cur_mode != mode) {
		s64 hblank = mode->hts - mode->width;
		s64 vblank = mode->vts_def - mode->height;
		s64 exp_max = mode->vts_def - S5K3K1_EXPOSURE_MARGIN;
		s64 exp_def = min_t(s64, 1000, exp_max);

		s5k3k1->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(s5k3k1->link_freq, mode->link_freq_idx);
		__v4l2_ctrl_modify_range(s5k3k1->pixel_rate, mode->pixel_rate,
					 mode->pixel_rate, 1, mode->pixel_rate);
		__v4l2_ctrl_modify_range(s5k3k1->hblank, hblank, hblank, 1, hblank);
		__v4l2_ctrl_modify_range(s5k3k1->vblank, S5K3K1_VBLANK_MIN,
					 S5K3K1_VTS_MAX - mode->height, 1, vblank);
		__v4l2_ctrl_s_ctrl(s5k3k1->vblank, vblank);
		__v4l2_ctrl_modify_range(s5k3k1->exposure, S5K3K1_EXPOSURE_MIN,
					 exp_max, S5K3K1_EXPOSURE_STEP, exp_def);
		__v4l2_ctrl_s_ctrl(s5k3k1->exposure, exp_def);
	}

	return s5k3k1->ctrl_handler.error;
}

static int s5k3k1_get_selection(struct v4l2_subdev *sd,
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
			.width = S5K3K1_NATIVE_WIDTH,
			.height = S5K3K1_NATIVE_HEIGHT,
		};
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = (struct v4l2_rect){
			.left = S5K3K1_ACTIVE_LEFT,
			.top = S5K3K1_ACTIVE_TOP,
			.width = S5K3K1_ACTIVE_WIDTH,
			.height = S5K3K1_ACTIVE_HEIGHT,
		};
		return 0;
	default:
		return -EINVAL;
	}
}

static int s5k3k1_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);
	const struct s5k3k1_mode *mode = s5k3k1->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(s5k3k1->dev);
	if (ret < 0)
		return ret;

	s5k3k1_pm_get_dep(s5k3k1->dev, "lens-focus", &s5k3k1->lens_dev);
	s5k3k1_pm_get_dep(s5k3k1->dev, "samsung,ois", &s5k3k1->ois_dev);

	if (s5k3k1->streamed_mode != mode) {
		ret = s5k3k1_write_seq(s5k3k1, s5k3k1_init_regs,
				       ARRAY_SIZE(s5k3k1_init_regs));
		if (ret)
			goto err_rpm;
	}

	ret = cci_multi_reg_write(s5k3k1->regmap, s5k3k1_master_regs,
				  ARRAY_SIZE(s5k3k1_master_regs), NULL);
	if (ret)
		goto err_rpm;

	ret = cci_multi_reg_write(s5k3k1->regmap, mode->regs, mode->num_regs, NULL);
	if (ret)
		goto err_rpm;

	ret = __v4l2_ctrl_handler_setup(&s5k3k1->ctrl_handler);
	if (ret)
		goto err_rpm;

	ret = cci_write(s5k3k1->regmap, S5K3K1_REG_MODE_SELECT,
			S5K3K1_MODE_STREAMING, NULL);
	if (ret)
		goto err_rpm;

	s5k3k1->streamed_mode = mode;

	return 0;

err_rpm:
	s5k3k1_pm_put_dep(&s5k3k1->ois_dev);
	s5k3k1_pm_put_dep(&s5k3k1->lens_dev);
	pm_runtime_put_autosuspend(s5k3k1->dev);
	return ret;
}

static int s5k3k1_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);
	int ret;

	ret = cci_write(s5k3k1->regmap, S5K3K1_REG_MODE_SELECT,
			S5K3K1_MODE_STANDBY, NULL);
	if (ret)
		dev_err(s5k3k1->dev, "failed to stop streaming: %d\n", ret);

	s5k3k1_pm_put_dep(&s5k3k1->ois_dev);
	s5k3k1_pm_put_dep(&s5k3k1->lens_dev);
	pm_runtime_put_autosuspend(s5k3k1->dev);
	return ret;
}

static const struct v4l2_subdev_video_ops s5k3k1_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops s5k3k1_pad_ops = {
	.enum_mbus_code = s5k3k1_enum_mbus_code,
	.enum_frame_size = s5k3k1_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = s5k3k1_set_pad_format,
	.get_selection = s5k3k1_get_selection,
	.enable_streams = s5k3k1_enable_streams,
	.disable_streams = s5k3k1_disable_streams,
};

static const struct v4l2_subdev_ops s5k3k1_subdev_ops = {
	.video = &s5k3k1_video_ops,
	.pad = &s5k3k1_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k3k1_internal_ops = {
	.init_state = s5k3k1_init_state,
};

static int s5k3k1_check_hwcfg(struct device *dev)
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

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5K3K1_NUM_DATA_LANES) {
		dev_err(dev, "expected %d data lanes, got %u\n",
			S5K3K1_NUM_DATA_LANES, bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link-frequencies specified in DT\n");
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < ARRAY_SIZE(s5k3k1_link_freq_menu); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++)
			if (bus_cfg.link_frequencies[j] == s5k3k1_link_freq_menu[i])
				break;
		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "missing expected link frequency %lld Hz\n",
				s5k3k1_link_freq_menu[i]);
			ret = -EINVAL;
			goto out_free;
		}
	}

out_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int s5k3k1_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s5k3k1 *s5k3k1;
	unsigned int i;
	int ret;

	ret = s5k3k1_check_hwcfg(dev);
	if (ret)
		return ret;

	s5k3k1 = devm_kzalloc(dev, sizeof(*s5k3k1), GFP_KERNEL);
	if (!s5k3k1)
		return -ENOMEM;

	s5k3k1->dev = dev;
	s5k3k1->cur_mode = &s5k3k1_modes[0];

	v4l2_i2c_subdev_init(&s5k3k1->sd, client, &s5k3k1_subdev_ops);

	s5k3k1->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5k3k1->regmap))
		return dev_err_probe(dev, PTR_ERR(s5k3k1->regmap),
				     "failed to init CCI regmap\n");

	s5k3k1->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(s5k3k1->xclk))
		return dev_err_probe(dev, PTR_ERR(s5k3k1->xclk),
				     "failed to get xclk\n");

	ret = clk_set_rate(s5k3k1->xclk, S5K3K1_XCLK_FREQ);
	if (ret)
		dev_warn(dev, "failed to set xclk rate to %u Hz: %d\n",
			 S5K3K1_XCLK_FREQ, ret);

	for (i = 0; i < S5K3K1_NUM_SUPPLIES; i++)
		s5k3k1->supplies[i].supply = s5k3k1_supply_names[i];

	ret = devm_regulator_bulk_get(dev, S5K3K1_NUM_SUPPLIES, s5k3k1->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	s5k3k1->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s5k3k1->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(s5k3k1->reset_gpio),
				     "failed to get reset gpio\n");

	ret = s5k3k1_power_on(dev);
	if (ret)
		return ret;

	ret = s5k3k1_identify_module(s5k3k1);
	if (ret)
		goto err_power_off;

	ret = s5k3k1_init_controls(s5k3k1);
	if (ret)
		goto err_power_off;

	s5k3k1->sd.internal_ops = &s5k3k1_internal_ops;
	s5k3k1->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k3k1->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5k3k1->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&s5k3k1->sd.entity, 1, &s5k3k1->pad);
	if (ret)
		goto err_ctrl_free;

	s5k3k1->sd.state_lock = s5k3k1->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&s5k3k1->sd);
	if (ret)
		goto err_media_cleanup;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&s5k3k1->sd);
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
	v4l2_subdev_cleanup(&s5k3k1->sd);
err_media_cleanup:
	media_entity_cleanup(&s5k3k1->sd.entity);
err_ctrl_free:
	v4l2_ctrl_handler_free(&s5k3k1->ctrl_handler);
err_power_off:
	s5k3k1_power_off(dev);
	return ret;
}

static void s5k3k1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3k1 *s5k3k1 = to_s5k3k1(sd);

	s5k3k1_pm_put_dep(&s5k3k1->ois_dev);
	s5k3k1_pm_put_dep(&s5k3k1->lens_dev);
	v4l2_async_unregister_subdev(sd);
	pm_runtime_dont_use_autosuspend(s5k3k1->dev);
	pm_runtime_disable(s5k3k1->dev);
	if (!pm_runtime_status_suspended(s5k3k1->dev))
		s5k3k1_power_off(s5k3k1->dev);
	pm_runtime_set_suspended(s5k3k1->dev);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s5k3k1->ctrl_handler);
}

static const struct dev_pm_ops s5k3k1_pm_ops = {
	SET_RUNTIME_PM_OPS(s5k3k1_power_off, s5k3k1_power_on, NULL)
};

static const struct of_device_id s5k3k1_dt_ids[] = {
	{ .compatible = "samsung,s5k3k1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k3k1_dt_ids);

static struct i2c_driver s5k3k1_i2c_driver = {
	.driver = {
		.name = "s5k3k1",
		.pm = &s5k3k1_pm_ops,
		.of_match_table = s5k3k1_dt_ids,
	},
	.probe = s5k3k1_probe,
	.remove = s5k3k1_remove,
};
module_i2c_driver(s5k3k1_i2c_driver);

MODULE_DESCRIPTION("Samsung S5K3K1 CMOS image sensor driver");
MODULE_LICENSE("GPL");
