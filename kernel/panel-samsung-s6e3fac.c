// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3FAC DDIC with AMB606AW01 6.1" 1080x2340 AMOLED panel (Galaxy S22, r0q).
 *
 * Command mode, 4 lanes, DSC 1.1 (2 slices of 540x117, 8 bpc/8 bpp).
 * Supports:
 *  - Multi-mode DRM refresh rates (120, 96, 60, 48, 30, 24 Hz) with seamless
 *    non-blanking mode switches and hardware adaptive LFD/PHS VRR (10-120 Hz)
 *  - High Brightness Mode (HBM, 12-bit WRDISBV 0x0800..0x0fff up to ~1300 nits)
 *  - Hardware mDNIe + IRC colour calibration, factory OTP white-point readback,
 *    RGB white balance, Night Kelvin LUT (2300K-6500K), and ACL control
 *  - Hardware Always-On Display (AOD / HLPM at 2, 10, 30, 60 nits + dynamic HLPM)
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "panel-samsung-s6e3fac-mdnie.h"

#define S6E3FAC_MIN_DBV		6
#define S6E3FAC_MAX_NORMAL_DBV	2047
#define S6E3FAC_MIN_HBM_DBV	2048
#define S6E3FAC_MID_HBM_DBV	3071
#define S6E3FAC_MAX_HBM_DBV	4095

#define S6E3FAC_VRR_CHECK_MS		5000
#define S6E3FAC_VRR_SHORT_IDLE_MS	5000
#define S6E3FAC_VRR_STATIC_24HZ_MS	45000
#define S6E3FAC_STATIC_MAX_VBLANKS	6

enum s6e3fac_vrr_policy {
	S6E3FAC_VRR_DYNAMIC = 0,	/* 120<->60 Hz dynamic + 24 Hz on long static idle */
	S6E3FAC_VRR_FIXED,		/* Follow DRM 120/60 Hz (60 Hz during video playback) */
	S6E3FAC_VRR_GAME,		/* Game mode (96HS / 90 Hz class) */
	S6E3FAC_VRR_FORCE_120,
	S6E3FAC_VRR_FORCE_60,
	S6E3FAC_VRR_FORCE_24,
};

enum s6e3fac_colour_mode {
	S6E3FAC_COLOUR_VIVID = 0,
	S6E3FAC_COLOUR_NATURAL,
	S6E3FAC_COLOUR_STANDARD,
	S6E3FAC_COLOUR_AUTO,
	S6E3FAC_COLOUR_BYPASS,
	S6E3FAC_COLOUR_GRAYSCALE,
	S6E3FAC_COLOUR_NIGHT,
	S6E3FAC_COLOUR_OUTDOOR,
	S6E3FAC_COLOUR_MAX,
};

static const char * const s6e3fac_colour_names[S6E3FAC_COLOUR_MAX] = {
	[S6E3FAC_COLOUR_VIVID]		= "vivid",
	[S6E3FAC_COLOUR_NATURAL]	= "natural",
	[S6E3FAC_COLOUR_STANDARD]	= "standard",
	[S6E3FAC_COLOUR_AUTO]		= "auto",
	[S6E3FAC_COLOUR_BYPASS]		= "bypass",
	[S6E3FAC_COLOUR_GRAYSCALE]	= "grayscale",
	[S6E3FAC_COLOUR_NIGHT]		= "night",
	[S6E3FAC_COLOUR_OUTDOOR]	= "outdoor",
};

struct s6e3fac {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_connector *connector;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;

	struct mutex lock; /* Serialises DSI state transitions and sysfs updates */
	bool prepared;
	bool enabled;
	bool seamless_transition;
	bool in_aod;

	unsigned int cur_vrefresh;
	unsigned int effective_rr;
	unsigned int applied_rr;
	bool applied_phs;
	enum s6e3fac_vrr_policy vrr_policy;
	bool video_mode;
	bool game_mode;

	unsigned long last_input_jiffies;
	u64 last_vblank_count;
	unsigned int static_idle_ms;
	struct work_struct vrr_boost_work;
	struct delayed_work vrr_idle_work;
	struct input_handler input_handler;
	bool input_registered;

	unsigned int hbm_mode;	/* 0 = off, 1 = medium boost, 2 = peak sunlight */
	u16 hbm_dbv;		/* Custom HBM DBV [2048..4095] */
	u16 last_normal_dbv;

	enum s6e3fac_colour_mode colour_mode;
	u16 night_kelvin;
	u8 acl_mode;		/* 0 = off, 1 = 8%, 2 = 15%, 3 = 30% */

	bool otp_read;
	u16 mdnie_x;
	u16 mdnie_y;
	u8 cal_r;
	u8 cal_g;
	u8 cal_b;
	u8 wb_r;
	u8 wb_g;
	u8 wb_b;

	bool aod_enabled;
	unsigned int aod_nits;	/* 2, 10, 30, or 60 */
};

/* Stock power-on order: vdd3 (1.8 V), vddr (1.3 V, +1 ms), vci (3.0 V, +12 ms) */
static const struct regulator_bulk_data s6e3fac_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vdd3" },
	{ .supply = "vddr" },
	{ .supply = "vci" },
};

/* Preferred boot refresh rate: 120 (default), 96, 60, 48, 30, or 24 */
static unsigned int refresh = 120;
module_param(refresh, uint, 0444);
MODULE_PARM_DESC(refresh, "Preferred boot refresh rate in Hz (default: 120)");

/* IRC tables from ss_dsi_panel_S6E3FAC_AMB606AW01.h */
static const u8 s6e3fac_irc_moderato[36] = {
	0x92,
	0x65, 0xff, 0x01, 0x00, 0x80, 0x01, 0xff, 0x10, 0x99, 0xff, 0x10, 0xff,
	0x80, 0x86, 0xee, 0x07, 0x07, 0x07, 0x1e, 0x1e, 0x1e, 0x28, 0x28, 0x28,
	0x2f, 0x2f, 0x2f, 0x32, 0x32, 0x32, 0x1b, 0x2f, 0xe0, 0x18, 0x64,
};

static const u8 s6e3fac_irc_flat[36] = {
	0x92,
	0x25, 0xcf, 0x03, 0x63, 0x80, 0x01, 0xff, 0x10, 0x99, 0xff, 0x10, 0xff,
	0x80, 0x86, 0xee, 0x07, 0x07, 0x07, 0x1e, 0x1e, 0x1e, 0x28, 0x28, 0x28,
	0x2f, 0x2f, 0x2f, 0x32, 0x32, 0x32, 0x1b, 0x2f, 0xe0, 0x18, 0x64,
};

/* AOR tables per base refresh mode */
static const u8 s6e3fac_aor_120_60[33] = {
	0x98,
	0x09, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x40,
	0x09, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x40,
	0x09, 0x40, 0x09, 0x40, 0x09, 0x40, 0x08, 0x54,
	0x08, 0x54, 0x01, 0x7a, 0x01, 0x7a, 0x01, 0x7a,
};

static const u8 s6e3fac_aor_96_48[33] = {
	0x98,
	0x0b, 0x90, 0x0b, 0x90, 0x0b, 0x90, 0x0b, 0x90,
	0x0b, 0x90, 0x0b, 0x90, 0x0b, 0x90, 0x0b, 0x90,
	0x0b, 0x90, 0x0b, 0x90, 0x0b, 0x90, 0x0a, 0x68,
	0x0a, 0x68, 0x01, 0xda, 0x01, 0xda, 0x01, 0xda,
};

/* Factory OTP CIE white-point calibration tables from ss_dsi_panel_S6E3FAC_AMB606AW01.c */
static const u8 s6e3fac_coord_tune[10][3] = {
	{ 0xff, 0xff, 0xff },
	{ 0xff, 0xf8, 0xf0 },
	{ 0xff, 0xf9, 0xf6 },
	{ 0xff, 0xfb, 0xfd },
	{ 0xff, 0xfb, 0xf0 },
	{ 0xff, 0xfc, 0xf6 },
	{ 0xff, 0xff, 0xfd },
	{ 0xff, 0xfe, 0xf0 },
	{ 0xff, 0xff, 0xf6 },
	{ 0xfc, 0xff, 0xfa },
};

static const u8 s6e3fac_rgb_idx[5][4] = {
	{ 5, 5, 5, 5 },
	{ 5, 2, 6, 3 },
	{ 5, 2, 4, 1 },
	{ 5, 8, 4, 7 },
	{ 5, 8, 6, 9 },
};

static const s32 s6e3fac_coord_coeff[5][8] = {
	{       0,       0,      0,      0,      0,       0,      0,      0 },
	{  -52615,  -61905,  21249,  15603,  40775,   80902, -19651, -19618 },
	{ -212096, -186041,  61987,  65143, -75083,  -27237,  16637,  15737 },
	{   69454,   77493, -27852, -19429, -93856, -133061,  37638,  35353 },
	{  192949,  174780, -56853, -60597,  57592,   13018, -11491, -10757 },
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

static void s6e3fac_write_u8(struct mipi_dsi_multi_context *dsi_ctx, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };

	mipi_dsi_generic_write_multi(dsi_ctx, buf, sizeof(buf));
}

static bool s6e3fac_crtc_stays_active(struct s6e3fac *ctx)
{
	struct drm_connector *conn = ctx->connector;
	struct drm_crtc *crtc;

	if (!conn || !conn->state)
		return false;
	crtc = conn->state->crtc;
	if (!crtc || !crtc->state)
		return false;

	return crtc->state->active;
}

static unsigned int s6e3fac_get_target_vrefresh(struct s6e3fac *ctx)
{
	struct drm_connector *conn = ctx->connector;
	struct drm_crtc *crtc;
	int vr;

	if (conn && conn->state) {
		crtc = conn->state->crtc;
		if (crtc && crtc->state && crtc->state->adjusted_mode.htotal) {
			vr = drm_mode_vrefresh(&crtc->state->adjusted_mode);
			if (vr > 0)
				return vr;
		}
	}

	return ctx->cur_vrefresh ? ctx->cur_vrefresh : refresh;
}

static void s6e3fac_compute_otp_calibration(struct s6e3fac *ctx, const u8 *buf)
{
	s64 f1, f2, cx, cy;
	int x = (buf[0] << 8) | buf[1];
	int y = (buf[2] << 8) | buf[3];
	int tune_idx;
	const u8 *idx4;
	int c;
	u8 out[3];

	ctx->mdnie_x = x;
	ctx->mdnie_y = y;

	/* Sanity check factory coordinates before applying adjustment */
	if (x < 2500 || x > 3600 || y < 2700 || y > 3700)
		return;

	if ((x - 3050) * (x - 3050) + (y - 3210) * (y - 3210) <= 225) {
		cx = 0;
		cy = 0;
		tune_idx = 1;
	} else {
		f1 = (((s64)y << 10) - (((s64)x << 10) * 56) / 55 - (102LL << 10)) >> 10;
		f2 = (((s64)y << 10) + ((s64)x << 10) * 5 - (18483LL << 10)) >> 10;
		if (f1 > 0)
			tune_idx = (f2 > 0) ? 1 : 2;
		else
			tune_idx = (f2 > 0) ? 4 : 3;

		cx = (s64)s6e3fac_coord_coeff[tune_idx][0] * x +
		     (s64)s6e3fac_coord_coeff[tune_idx][1] * y +
		     ((((s64)s6e3fac_coord_coeff[tune_idx][2] * x + 512) >> 10) * y) +
		     (s64)s6e3fac_coord_coeff[tune_idx][3] * 10000;
		cx = clamp_t(s64, (cx + 512) >> 10, 0, 1024);

		cy = (s64)s6e3fac_coord_coeff[tune_idx][4] * x +
		     (s64)s6e3fac_coord_coeff[tune_idx][5] * y +
		     ((((s64)s6e3fac_coord_coeff[tune_idx][6] * x + 512) >> 10) * y) +
		     (s64)s6e3fac_coord_coeff[tune_idx][7] * 10000;
		cy = clamp_t(s64, (cy + 512) >> 10, 0, 1024);
	}

	idx4 = s6e3fac_rgb_idx[tune_idx];
	for (c = 0; c < 3; c++) {
		s64 v00 = s6e3fac_coord_tune[idx4[0]][c];
		s64 v01 = s6e3fac_coord_tune[idx4[1]][c];
		s64 v10 = s6e3fac_coord_tune[idx4[2]][c];
		s64 v11 = s6e3fac_coord_tune[idx4[3]][c];
		s64 val = ((v00 * (1024 - cx) + v10 * cx) * (1024 - cy) +
			   (v01 * (1024 - cx) + v11 * cx) * cy + 524288) >> 20;

		out[c] = clamp_t(s64, val, 220, 255);
	}

	ctx->cal_r = out[0];
	ctx->cal_g = out[1];
	ctx->cal_b = out[2];
}

static void s6e3fac_read_otp_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	u8 mod_info[11] = {};
	int ret;

	if (ctx->otp_read)
		return;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	ret = mipi_dsi_dcs_read(ctx->dsi, 0xa1, mod_info, sizeof(mod_info));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	if (ret == sizeof(mod_info) && (mod_info[0] || mod_info[1])) {
		s6e3fac_compute_otp_calibration(ctx, mod_info);
		ctx->otp_read = true;
		dev_info(&ctx->dsi->dev,
			 "OTP white-point (%u, %u) -> calibrated RGB (%u, %u, %u)\n",
			 ctx->mdnie_x, ctx->mdnie_y,
			 ctx->cal_r, ctx->cal_g, ctx->cal_b);
	}
}

static u64 s6e3fac_read_vblank_count(struct s6e3fac *ctx)
{
	struct drm_connector *conn = ctx->connector;

	if (!conn || !conn->state || !conn->state->crtc)
		return 0;

	return drm_crtc_vblank_count(conn->state->crtc);
}

static void s6e3fac_schedule_idle_check_locked(struct s6e3fac *ctx)
{
	if (!ctx->prepared || ctx->in_aod)
		return;

	if (ctx->vrr_policy == S6E3FAC_VRR_DYNAMIC && !ctx->game_mode &&
	    ctx->cur_vrefresh >= 120) {
		mod_delayed_work(system_wq, &ctx->vrr_idle_work,
				 msecs_to_jiffies(S6E3FAC_VRR_CHECK_MS));
	} else {
		cancel_delayed_work(&ctx->vrr_idle_work);
	}
}

static void s6e3fac_select_vrr_target_locked(struct s6e3fac *ctx,
					     unsigned int *out_rr,
					     bool *out_phs)
{
	unsigned int drm_rr = ctx->cur_vrefresh ? ctx->cur_vrefresh : refresh;
	unsigned long idle_ms = jiffies_to_msecs(jiffies - ctx->last_input_jiffies);

	if (ctx->game_mode || ctx->vrr_policy == S6E3FAC_VRR_GAME || drm_rr == 96) {
		*out_rr = 96;
		*out_phs = false;
		return;
	}

	switch (ctx->vrr_policy) {
	case S6E3FAC_VRR_FORCE_120:
		*out_rr = 120;
		*out_phs = false;
		return;
	case S6E3FAC_VRR_FORCE_60:
		*out_rr = 60;
		*out_phs = (drm_rr >= 120);
		return;
	case S6E3FAC_VRR_FORCE_24:
		*out_rr = 24;
		*out_phs = true;
		return;
	case S6E3FAC_VRR_FIXED:
		if (drm_rr >= 120 && ctx->video_mode) {
			*out_rr = 60;
			*out_phs = true;
		} else if (drm_rr <= 60) {
			*out_rr = 60;
			*out_phs = false;
		} else {
			*out_rr = 120;
			*out_phs = false;
		}
		return;
	case S6E3FAC_VRR_DYNAMIC:
	default:
		if (drm_rr <= 60) {
			*out_rr = 60;
			*out_phs = false;
		} else if (ctx->video_mode) {
			*out_rr = 60;
			*out_phs = true;
		} else if (idle_ms < S6E3FAC_VRR_SHORT_IDLE_MS) {
			*out_rr = 120;
			*out_phs = false;
		} else if (idle_ms >= S6E3FAC_VRR_STATIC_24HZ_MS &&
			   ctx->static_idle_ms >= S6E3FAC_VRR_STATIC_24HZ_MS) {
			*out_rr = 24;
			*out_phs = true;
		} else {
			*out_rr = 60;
			*out_phs = true;
		}
		return;
	}
}

/*
 * Build and send Samsung's VRR / PHS register update (ss_vrr in OEM driver).
 * Can be called live in HS mode without blanking or resetting the panel.
 */
static int s6e3fac_apply_vrr_locked(struct s6e3fac *ctx, bool hs_transfer)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	unsigned int rr = 120;
	bool phs = false;
	unsigned int base_rr;
	u8 osc, freq, glut_en;
	u8 b9[21] = { 0xb9 };
	u8 vfp_gpara[4] = { 0xb0, 0x00, 0x08, 0xf2 };
	u8 vfp_cmd[3] = { 0xf2, 0x00, 0x0c };
	u8 aor_gpara[4] = { 0xb0, 0x00, 0x8b, 0x98 };
	const u8 *aor_tbl = s6e3fac_aor_120_60;

	s6e3fac_select_vrr_target_locked(ctx, &rr, &phs);
	ctx->effective_rr = rr;
	if (hs_transfer && ctx->applied_rr == rr && ctx->applied_phs == phs)
		return 0;

	/* Determine base timing family (ss_get_vrr_mode_base) */
	if (phs || rr == 120)
		base_rr = 120;
	else if (rr == 96)
		base_rr = 96;
	else if (rr == 60 || rr == 30)
		base_rr = 60;
	else
		base_rr = 48;

	glut_en = (base_rr == 120) ? 0x00 : 0x02;
	osc = (base_rr == 60 || base_rr == 48) ? 0x00 : 0x01;
	freq = (base_rr == 60 || base_rr == 48) ? 0x08 : 0x00;

	/* Populate TE_SEL (0xB9) per mode */
	if (!phs && (rr == 60 || rr == 48)) {
		b9[1] = 0x04;
		b9[5] = 0x09;
		b9[6] = 0x23;
		b9[8] = 0x0f;
		b9[14] = 0x01;
	} else {
		b9[1] = 0x61;
		b9[6] = 0x01;
		b9[20] = 0x10;
		if (phs && rr == 60) {
			b9[3] = 0x03;
			b9[13] = 0x04;
			b9[14] = 0x94;
		} else if (phs && rr == 30) {
			b9[3] = 0x07;
			b9[13] = 0x04;
			b9[14] = 0x94;
		} else if (phs && rr == 24) {
			b9[3] = 0x09;
			b9[13] = 0x04;
			b9[14] = 0x94;
		} else if (phs && rr == 10) {
			b9[3] = 0x17;
			b9[13] = 0x04;
			b9[14] = 0x94;
		} else if (rr == 30 || rr == 24) {
			b9[3] = 0x07;
			b9[14] = 0x11;
		} else if (rr == 10) {
			b9[3] = 0x13;
			b9[14] = 0x11;
		} else if (rr == 96) {
			b9[3] = 0x01;
			b9[13] = 0x03;
			b9[14] = 0x6c;
		} else {
			/* 120HS */
			b9[3] = 0x01;
			b9[13] = 0x04;
			b9[14] = 0x94;
		}
	}

	if (base_rr == 96 || base_rr == 48) {
		vfp_cmd[1] = 0x02;
		vfp_cmd[2] = 0x5c;
		aor_tbl = s6e3fac_aor_96_48;
	}
	if (base_rr == 60 || base_rr == 48)
		aor_gpara[2] = 0xab;

	if (hs_transfer)
		ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);

	s6e3fac_write_u8(&dsi_ctx, 0xf2, osc);
	s6e3fac_write_u8(&dsi_ctx, 0x60, freq);
	mipi_dsi_generic_write_multi(&dsi_ctx, b9, sizeof(b9));
	mipi_dsi_generic_write_multi(&dsi_ctx, vfp_gpara, sizeof(vfp_gpara));
	mipi_dsi_generic_write_multi(&dsi_ctx, vfp_cmd, sizeof(vfp_cmd));
	mipi_dsi_generic_write_multi(&dsi_ctx, aor_gpara, sizeof(aor_gpara));
	mipi_dsi_generic_write_multi(&dsi_ctx, aor_tbl, 33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x85, 0x93);
	s6e3fac_write_u8(&dsi_ctx, 0x93, glut_en);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);

	if (hs_transfer)
		ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	if (!dsi_ctx.accum_err) {
		ctx->applied_rr = rr;
		ctx->applied_phs = phs;
	}

	return dsi_ctx.accum_err;
}

static void s6e3fac_vrr_boost_work_fn(struct work_struct *work)
{
	struct s6e3fac *ctx = container_of(work, struct s6e3fac, vrr_boost_work);

	mutex_lock(&ctx->lock);
	ctx->static_idle_ms = 0;
	if (ctx->prepared && !ctx->in_aod) {
		ctx->last_vblank_count = s6e3fac_read_vblank_count(ctx);
		s6e3fac_apply_vrr_locked(ctx, true);
		s6e3fac_schedule_idle_check_locked(ctx);
	}
	mutex_unlock(&ctx->lock);
}

static void s6e3fac_vrr_idle_work_fn(struct work_struct *work)
{
	struct s6e3fac *ctx = container_of(work, struct s6e3fac, vrr_idle_work.work);
	u64 cur_vb, delta_vb;

	mutex_lock(&ctx->lock);
	if (!ctx->prepared || ctx->in_aod) {
		mutex_unlock(&ctx->lock);
		return;
	}

	cur_vb = s6e3fac_read_vblank_count(ctx);
	delta_vb = (cur_vb >= ctx->last_vblank_count) ?
		   (cur_vb - ctx->last_vblank_count) : 0;
	ctx->last_vblank_count = cur_vb;

	if (!ctx->video_mode && delta_vb <= S6E3FAC_STATIC_MAX_VBLANKS) {
		if (ctx->static_idle_ms < S6E3FAC_VRR_STATIC_24HZ_MS)
			ctx->static_idle_ms += S6E3FAC_VRR_CHECK_MS;
	} else {
		ctx->static_idle_ms = 0;
	}

	s6e3fac_apply_vrr_locked(ctx, true);
	s6e3fac_schedule_idle_check_locked(ctx);
	mutex_unlock(&ctx->lock);
}

static void s6e3fac_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	struct s6e3fac *ctx = handle->handler->private;

	if (type != EV_ABS && type != EV_KEY && type != EV_REL)
		return;

	WRITE_ONCE(ctx->last_input_jiffies, jiffies);

	if (READ_ONCE(ctx->prepared) && !READ_ONCE(ctx->in_aod) &&
	    READ_ONCE(ctx->vrr_policy) == S6E3FAC_VRR_DYNAMIC &&
	    !READ_ONCE(ctx->game_mode) && !READ_ONCE(ctx->video_mode) &&
	    READ_ONCE(ctx->effective_rr) != 120)
		queue_work(system_highpri_wq, &ctx->vrr_boost_work);
}

static int s6e3fac_input_connect(struct input_handler *handler,
				 struct input_dev *dev,
				 const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc_obj(*handle);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "s6e3fac-vrr";

	ret = input_register_handle(handle);
	if (ret) {
		kfree(handle);
		return ret;
	}

	ret = input_open_device(handle);
	if (ret) {
		input_unregister_handle(handle);
		kfree(handle);
		return ret;
	}

	return 0;
}

static void s6e3fac_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id s6e3fac_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_ABS) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ }
};

static int s6e3fac_apply_acl_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	u8 acl_val;

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);

	if (ctx->acl_mode == 0) {
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0x00);
	} else {
		acl_val = (ctx->acl_mode == 3) ? 0x03 :
			  (ctx->acl_mode == 2) ? 0x02 : 0x01;
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0x47, 0x98);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x55, 0x80, 0x01);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0x56, 0x98);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x00, 0x1f);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0x4d, 0x98);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x15, 0x33, 0x33, 0x99);
		s6e3fac_write_u8(&dsi_ctx, 0x55, acl_val);
	}

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static const u8 *s6e3fac_find_night_scr(u16 kelvin)
{
	unsigned int best = 0;
	unsigned int best_diff = UINT_MAX;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(s6e3fac_night_kelvin_lut); i++) {
		unsigned int d = abs((int)kelvin - (int)s6e3fac_night_kelvin_lut[i].kelvin);

		if (d < best_diff) {
			best_diff = d;
			best = i;
		}
	}

	return s6e3fac_night_kelvin_lut[best].scr;
}

static int s6e3fac_apply_mdnie_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	enum s6e3fac_colour_mode mode = ctx->colour_mode;
	const u8 *s1, *s2, *s3;
	size_t len1, len2, len3;
	u8 s1_buf[225];
	bool apply_wb = false;

	/* In Peak Sunlight HBM, auto-engage Outdoor CE unless user chose a fixed filter */
	if (ctx->hbm_mode >= 2 &&
	    (mode == S6E3FAC_COLOUR_VIVID || mode == S6E3FAC_COLOUR_AUTO))
		mode = S6E3FAC_COLOUR_OUTDOOR;

	switch (mode) {
	case S6E3FAC_COLOUR_NATURAL:
		s1 = s6e3fac_mdnie_natural_1;
		len1 = sizeof(s6e3fac_mdnie_natural_1);
		s2 = s6e3fac_mdnie_natural_2;
		len2 = sizeof(s6e3fac_mdnie_natural_2);
		s3 = s6e3fac_mdnie_natural_3;
		len3 = sizeof(s6e3fac_mdnie_natural_3);
		break;
	case S6E3FAC_COLOUR_STANDARD:
		s1 = s6e3fac_mdnie_standard_1;
		len1 = sizeof(s6e3fac_mdnie_standard_1);
		s2 = s6e3fac_mdnie_standard_2;
		len2 = sizeof(s6e3fac_mdnie_standard_2);
		s3 = s6e3fac_mdnie_standard_3;
		len3 = sizeof(s6e3fac_mdnie_standard_3);
		break;
	case S6E3FAC_COLOUR_AUTO:
		s1 = s6e3fac_mdnie_auto_gamut_1;
		len1 = sizeof(s6e3fac_mdnie_auto_gamut_1);
		s2 = s6e3fac_mdnie_auto_gamut_2;
		len2 = sizeof(s6e3fac_mdnie_auto_gamut_2);
		s3 = s6e3fac_mdnie_auto_gamut_3;
		len3 = sizeof(s6e3fac_mdnie_auto_gamut_3);
		apply_wb = true;
		break;
	case S6E3FAC_COLOUR_BYPASS:
		s1 = s6e3fac_mdnie_bypass_1;
		len1 = sizeof(s6e3fac_mdnie_bypass_1);
		s2 = s6e3fac_mdnie_bypass_2;
		len2 = sizeof(s6e3fac_mdnie_bypass_2);
		s3 = s6e3fac_mdnie_bypass_3;
		len3 = sizeof(s6e3fac_mdnie_bypass_3);
		break;
	case S6E3FAC_COLOUR_GRAYSCALE:
		s1 = s6e3fac_mdnie_grayscale_1;
		len1 = sizeof(s6e3fac_mdnie_grayscale_1);
		s2 = s6e3fac_mdnie_grayscale_2;
		len2 = sizeof(s6e3fac_mdnie_grayscale_2);
		s3 = s6e3fac_mdnie_grayscale_3;
		len3 = sizeof(s6e3fac_mdnie_grayscale_3);
		break;
	case S6E3FAC_COLOUR_NIGHT:
		s1 = s6e3fac_mdnie_night_1;
		len1 = sizeof(s6e3fac_mdnie_night_1);
		s2 = s6e3fac_mdnie_night_2;
		len2 = sizeof(s6e3fac_mdnie_night_2);
		s3 = s6e3fac_mdnie_night_3;
		len3 = sizeof(s6e3fac_mdnie_night_3);
		break;
	case S6E3FAC_COLOUR_OUTDOOR:
		s1 = s6e3fac_mdnie_outdoor_1;
		len1 = sizeof(s6e3fac_mdnie_outdoor_1);
		s2 = s6e3fac_mdnie_outdoor_2;
		len2 = sizeof(s6e3fac_mdnie_outdoor_2);
		s3 = s6e3fac_mdnie_outdoor_3;
		len3 = sizeof(s6e3fac_mdnie_outdoor_3);
		apply_wb = true;
		break;
	case S6E3FAC_COLOUR_VIVID:
	default:
		s1 = s6e3fac_mdnie_vivid_1;
		len1 = sizeof(s6e3fac_mdnie_vivid_1);
		s2 = s6e3fac_mdnie_vivid_2;
		len2 = sizeof(s6e3fac_mdnie_vivid_2);
		s3 = s6e3fac_mdnie_vivid_3;
		len3 = sizeof(s6e3fac_mdnie_vivid_3);
		apply_wb = true;
		break;
	}

	memcpy(s1_buf, s1, len1);

	if (mode == S6E3FAC_COLOUR_NIGHT) {
		memcpy(&s1_buf[S6E3FAC_MDNIE_SCR_OFFSET],
		       s6e3fac_find_night_scr(ctx->night_kelvin),
		       S6E3FAC_MDNIE_SCR_LEN);
	} else if (apply_wb && len1 > S6E3FAC_SCR_WHITE_B_IDX) {
		s1_buf[S6E3FAC_SCR_WHITE_R_IDX] =
			((u16)ctx->cal_r * (u16)ctx->wb_r) / 255;
		s1_buf[S6E3FAC_SCR_WHITE_G_IDX] =
			((u16)ctx->cal_g * (u16)ctx->wb_g) / 255;
		s1_buf[S6E3FAC_SCR_WHITE_B_IDX] =
			((u16)ctx->cal_b * (u16)ctx->wb_b) / 255;
	}

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_multi(&dsi_ctx, s1_buf, len1);
	mipi_dsi_generic_write_multi(&dsi_ctx, s2, len2);
	mipi_dsi_generic_write_multi(&dsi_ctx, s3, len3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static int s6e3fac_apply_brightness_locked(struct s6e3fac *ctx)
{
	struct backlight_device *bl = ctx->panel.backlight;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	const u8 *irc;
	u16 dbv;
	u8 wrdisbv[3];
	bool flat_irc;

	if (!ctx->prepared || ctx->in_aod || !bl)
		return 0;

	if (backlight_is_blank(bl)) {
		dbv = 0;
	} else if (ctx->hbm_mode == 2) {
		dbv = ctx->hbm_dbv;
	} else if (ctx->hbm_mode == 1) {
		dbv = S6E3FAC_MID_HBM_DBV;
	} else {
		u32 raw = backlight_get_brightness(bl);

		dbv = S6E3FAC_MIN_DBV +
		      raw * (S6E3FAC_MAX_NORMAL_DBV - S6E3FAC_MIN_DBV) /
		      bl->props.max_brightness;
		ctx->last_normal_dbv = dbv;
	}

	wrdisbv[0] = 0x51;
	wrdisbv[1] = dbv >> 8;
	wrdisbv[2] = dbv & 0xff;

	flat_irc = (ctx->colour_mode == S6E3FAC_COLOUR_NATURAL ||
		    ctx->colour_mode == S6E3FAC_COLOUR_STANDARD);
	irc = flat_irc ? s6e3fac_irc_flat : s6e3fac_irc_moderato;

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0d, 0x94);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x94, 0x20, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x01, 0xb1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x14);
	mipi_dsi_generic_write_multi(&dsi_ctx, wrdisbv, sizeof(wrdisbv));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x02, 0x79, 0x92);
	mipi_dsi_generic_write_multi(&dsi_ctx, irc, 36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static void s6e3fac_get_aod_aor(unsigned int nits, u8 *hi, u8 *lo)
{
	if (nits >= 60) {
		*hi = 0x00;
		*lo = 0x70;
	} else if (nits >= 30) {
		*hi = 0x04;
		*lo = 0xac;
	} else if (nits >= 10) {
		*hi = 0x07;
		*lo = 0xb8;
	} else {
		*hi = 0x08;
		*lo = 0xef;
	}
}

static int s6e3fac_enter_aod_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	u8 lpm_ctrl[7] = { 0x98, 0x01, 0x00, 0x00, 0x02, 0x00, 0x70 };
	u8 lpm_aor[3] = { 0x98, 0x00, 0x00 };
	u8 hi, lo;

	s6e3fac_get_aod_aor(ctx->aod_nits, &hi, &lo);
	lpm_ctrl[2] = hi;
	lpm_ctrl[3] = lo;
	lpm_aor[1] = hi;
	lpm_aor[2] = lo;

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	/* 1. samsung,lpm_on_aor_tx_cmds_revA */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x4e, 0x98);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x01, 0x09, 0x40, 0x02, 0x00, 0x70);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9,
					 0x61, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x44,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x10);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 17);

	/* 2. samsung,lpm_on_tx_cmds_revA (HLPM 2/10/30/60 nit) */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x38);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x4e, 0x98);
	mipi_dsi_generic_write_multi(&dsi_ctx, lpm_ctrl, sizeof(lpm_ctrl));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x01, 0x3b, 0x98);
	mipi_dsi_generic_write_multi(&dsi_ctx, lpm_aor, sizeof(lpm_aor));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	/* 3. samsung,dynamic_hlpm_enable_tx_cmds_revA (1-30 Hz self-refresh) */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85,
					 0x81, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x1f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 34);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	if (!dsi_ctx.accum_err)
		ctx->in_aod = true;

	return dsi_ctx.accum_err;
}

static int s6e3fac_exit_aod_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	/* 1. samsung,dynamic_hlpm_disable_tx_cmds_revA */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x85, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	/* 2. samsung,lpm_off_tx_cmds_revA */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0a, 0xb1);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x4e, 0x98);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x98, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 34);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	ctx->in_aod = false;

	return dsi_ctx.accum_err;
}

static int s6e3fac_on_locked(struct s6e3fac *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct drm_dsc_picture_parameter_set pps;
	bool is_120_base = (ctx->cur_vrefresh == 120 || ctx->cur_vrefresh == 96);

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
	s6e3fac_write_u8(&dsi_ctx, 0x60, is_120_base ? 0x00 : 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_msleep(&dsi_ctx, 110);
	/* TE on */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0e, 0x94);
	s6e3fac_write_u8(&dsi_ctx, 0x94, (ctx->cur_vrefresh == 120) ? 0x18 : 0x01);
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

	/* Stock sends compression mode, then PPS */
	mipi_dsi_compression_mode_multi(&dsi_ctx, true);
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x21, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x2f, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf1, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);

	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	/* Read factory OTP white-point once, then apply VRR, ACL, mDNIe, and brightness */
	s6e3fac_read_otp_locked(ctx);
	s6e3fac_apply_vrr_locked(ctx, false);

	return 0;
}

static int s6e3fac_prepare(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret = 0;

	mutex_lock(&ctx->lock);

	ctx->cur_vrefresh = s6e3fac_get_target_vrefresh(ctx);
	ctx->last_input_jiffies = jiffies;
	ctx->static_idle_ms = 0;
	ctx->last_vblank_count = s6e3fac_read_vblank_count(ctx);
	ctx->applied_rr = 0;

	if (ctx->seamless_transition) {
		ctx->seamless_transition = false;
		ctx->prepared = true;
		s6e3fac_apply_vrr_locked(ctx, true);
		s6e3fac_schedule_idle_check_locked(ctx);
		mutex_unlock(&ctx->lock);
		return 0;
	}

	if (ctx->in_aod) {
		ret = s6e3fac_exit_aod_locked(ctx);
		if (ret == 0) {
			ctx->prepared = true;
			s6e3fac_apply_vrr_locked(ctx, true);
			s6e3fac_apply_acl_locked(ctx);
			s6e3fac_apply_mdnie_locked(ctx);
			s6e3fac_apply_brightness_locked(ctx);
			s6e3fac_schedule_idle_check_locked(ctx);
			mutex_unlock(&ctx->lock);
			return 0;
		}
		dev_warn(dev, "AOD exit failed (%d), falling back to cold reset\n", ret);
		ctx->in_aod = false;
	} else {
		ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
		if (ret < 0) {
			dev_err(dev, "Failed to enable regulators: %d\n", ret);
			mutex_unlock(&ctx->lock);
			return ret;
		}
		usleep_range(12000, 13000);
	}

	s6e3fac_reset(ctx);

	ret = s6e3fac_on_locked(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
		mutex_unlock(&ctx->lock);
		return ret;
	}

	ctx->prepared = true;
	s6e3fac_apply_acl_locked(ctx);
	s6e3fac_apply_mdnie_locked(ctx);
	s6e3fac_apply_brightness_locked(ctx);
	s6e3fac_schedule_idle_check_locked(ctx);

	mutex_unlock(&ctx->lock);
	return 0;
}

static int s6e3fac_enable(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mutex_lock(&ctx->lock);

	if (!ctx->enabled) {
		ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
		mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
		mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);
		if (dsi_ctx.accum_err)
			dev_err(&ctx->dsi->dev, "display on failed: %d\n", dsi_ctx.accum_err);
		else
			ctx->enabled = true;
	}

	mutex_unlock(&ctx->lock);
	return dsi_ctx.accum_err;
}

static int s6e3fac_disable(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret = 0;

	mutex_lock(&ctx->lock);

	/* Seamless refresh-rate switch: keep display on and GRAM active */
	if (s6e3fac_crtc_stays_active(ctx)) {
		ctx->seamless_transition = true;
		mutex_unlock(&ctx->lock);
		return 0;
	}

	cancel_delayed_work(&ctx->vrr_idle_work);

	/* Hardware Always-On Display (HLPM): transition into self-refresh */
	if (ctx->aod_enabled) {
		ret = s6e3fac_enter_aod_locked(ctx);
		if (ret == 0) {
			ctx->enabled = false;
			mutex_unlock(&ctx->lock);
			return 0;
		}
		dev_warn(&ctx->dsi->dev, "AOD enter failed (%d), powering off\n", ret);
	}

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0xa5, 0xa5);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x9f, 0x5a, 0x5a);
	mipi_dsi_msleep(&dsi_ctx, 20);
	ctx->enabled = false;

	mutex_unlock(&ctx->lock);
	return dsi_ctx.accum_err;
}

static int s6e3fac_unprepare(struct drm_panel *panel)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mutex_lock(&ctx->lock);

	if (ctx->seamless_transition) {
		mutex_unlock(&ctx->lock);
		return 0;
	}

	/* In AOD HLPM, keep panel rails & GRAM powered while DPU/DSI sleep */
	if (ctx->in_aod) {
		ctx->prepared = false;
		mutex_unlock(&ctx->lock);
		return 0;
	}

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
	ctx->prepared = false;
	ctx->applied_rr = 0;

	mutex_unlock(&ctx->lock);
	return 0;
}

#define S6E3FAC_MODE(vr, vfp, vbp) {						\
	.clock = (1080 + 92 + 92 + 92) * (2340 + (vfp) + 84 + (vbp)) * (vr) / 1000, \
	.hdisplay = 1080,							\
	.hsync_start = 1080 + 92,						\
	.hsync_end = 1080 + 92 + 92,						\
	.htotal = 1080 + 92 + 92 + 92,						\
	.vdisplay = 2340,							\
	.vsync_start = 2340 + (vfp),						\
	.vsync_end = 2340 + (vfp) + 84,						\
	.vtotal = 2340 + (vfp) + 84 + (vbp),					\
	.width_mm = 64,								\
	.height_mm = 140,							\
	.type = DRM_MODE_TYPE_DRIVER,						\
}

/* Normal DRM modes: strictly 120 Hz (preferred) and 60 Hz; 96 Hz only in Game Mode */
static const struct drm_display_mode s6e3fac_normal_modes[] = {
	S6E3FAC_MODE(120,   467, 84),	/* fhd120hs */
	S6E3FAC_MODE(60,   3441, 85),	/* fhd60hs  */
};

static const struct drm_display_mode s6e3fac_game_mode =
	S6E3FAC_MODE(96,   1212, 84);	/* fhd96hs (Game Mode) */

static int s6e3fac_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct s6e3fac *ctx = to_s6e3fac(panel);
	struct drm_display_mode *mode;
	unsigned int pref_rr = ctx->game_mode ? 96 : ((refresh == 60) ? 60 : 120);
	unsigned int i, count = 0;

	ctx->connector = connector;

	if (ctx->game_mode) {
		mode = drm_mode_duplicate(connector->dev, &s6e3fac_game_mode);
		if (mode) {
			drm_mode_set_name(mode);
			mode->type |= DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, mode);
			count++;
		}
	}

	for (i = 0; i < ARRAY_SIZE(s6e3fac_normal_modes); i++) {
		mode = drm_mode_duplicate(connector->dev, &s6e3fac_normal_modes[i]);
		if (!mode)
			continue;

		drm_mode_set_name(mode);
		if (!ctx->game_mode && drm_mode_vrefresh(mode) == pref_rr)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
		count++;
	}

	connector->display_info.width_mm = 64;
	connector->display_info.height_mm = 140;
	connector->display_info.bpc = 8;

	return count;
}

static enum drm_panel_orientation s6e3fac_get_orientation(struct drm_panel *panel)
{
	return DRM_MODE_PANEL_ORIENTATION_NORMAL;
}

static const struct drm_panel_funcs s6e3fac_panel_funcs = {
	.prepare = s6e3fac_prepare,
	.enable = s6e3fac_enable,
	.disable = s6e3fac_disable,
	.unprepare = s6e3fac_unprepare,
	.get_modes = s6e3fac_get_modes,
	.get_orientation = s6e3fac_get_orientation,
};

static int s6e3fac_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct s6e3fac *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	mutex_lock(&ctx->lock);
	ret = s6e3fac_apply_brightness_locked(ctx);
	mutex_unlock(&ctx->lock);

	return ret;
}

static const struct backlight_ops s6e3fac_bl_ops = {
	.update_status = s6e3fac_bl_update_status,
};

static struct backlight_device *s6e3fac_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = S6E3FAC_MAX_NORMAL_DBV,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e3fac_bl_ops, &props);
}

/* ---------------- Sysfs attributes for Display Extras ---------------- */

static const struct {
	enum s6e3fac_vrr_policy policy;
	const char *name;
} s6e3fac_vrr_names[] = {
	{ S6E3FAC_VRR_DYNAMIC,   "dynamic" },
	{ S6E3FAC_VRR_FIXED,     "fixed" },
	{ S6E3FAC_VRR_GAME,      "game" },
	{ S6E3FAC_VRR_FORCE_120, "120" },
	{ S6E3FAC_VRR_FORCE_60,  "60" },
	{ S6E3FAC_VRR_FORCE_24,  "24" },
};

static ssize_t vrr_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;

	mutex_lock(&ctx->lock);
	for (i = 0; i < ARRAY_SIZE(s6e3fac_vrr_names); i++) {
		if (ctx->vrr_policy == s6e3fac_vrr_names[i].policy)
			len += sysfs_emit_at(buf, len, "[%s] ", s6e3fac_vrr_names[i].name);
		else
			len += sysfs_emit_at(buf, len, "%s ", s6e3fac_vrr_names[i].name);
	}
	len += sysfs_emit_at(buf, len, "(drm=%uHz hw=%uHz video=%u game=%u)\n",
			     ctx->cur_vrefresh, ctx->effective_rr,
			     ctx->video_mode ? 1 : 0, ctx->game_mode ? 1 : 0);
	mutex_unlock(&ctx->lock);

	return len;
}

static ssize_t vrr_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	enum s6e3fac_vrr_policy new_policy;
	struct drm_device *drm_dev = NULL;
	bool prev_game, matched = false;
	unsigned int i;

	if (sysfs_streq(buf, "adaptive")) {
		new_policy = S6E3FAC_VRR_DYNAMIC;
		matched = true;
	} else if (sysfs_streq(buf, "90") || sysfs_streq(buf, "96")) {
		new_policy = S6E3FAC_VRR_GAME;
		matched = true;
	} else {
		for (i = 0; i < ARRAY_SIZE(s6e3fac_vrr_names); i++) {
			if (sysfs_streq(buf, s6e3fac_vrr_names[i].name)) {
				new_policy = s6e3fac_vrr_names[i].policy;
				matched = true;
				break;
			}
		}
	}

	if (!matched)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	prev_game = ctx->game_mode;
	ctx->vrr_policy = new_policy;
	ctx->game_mode = (new_policy == S6E3FAC_VRR_GAME);
	ctx->last_input_jiffies = jiffies;
	ctx->static_idle_ms = 0;
	ctx->applied_rr = 0;
	if (ctx->prepared && !ctx->in_aod) {
		s6e3fac_apply_vrr_locked(ctx, true);
		s6e3fac_schedule_idle_check_locked(ctx);
	}
	if (prev_game != ctx->game_mode && ctx->connector)
		drm_dev = ctx->connector->dev;
	mutex_unlock(&ctx->lock);

	if (drm_dev)
		drm_kms_helper_hotplug_event(drm_dev);

	return count;
}
static DEVICE_ATTR_RW(vrr_mode);

static ssize_t video_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->video_mode ? 1 : 0);
}

static ssize_t video_mode_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;

	mutex_lock(&ctx->lock);
	if (ctx->video_mode != en) {
		ctx->video_mode = en;
		ctx->static_idle_ms = 0;
		if (!en)
			ctx->last_input_jiffies = jiffies;
		if (ctx->prepared && !ctx->in_aod) {
			s6e3fac_apply_vrr_locked(ctx, true);
			s6e3fac_schedule_idle_check_locked(ctx);
		}
	}
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(video_mode);

static ssize_t game_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->game_mode ? 1 : 0);
}

static ssize_t game_mode_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = NULL;
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;

	mutex_lock(&ctx->lock);
	if (ctx->game_mode != en) {
		ctx->game_mode = en;
		if (en)
			ctx->vrr_policy = S6E3FAC_VRR_GAME;
		else if (ctx->vrr_policy == S6E3FAC_VRR_GAME)
			ctx->vrr_policy = S6E3FAC_VRR_DYNAMIC;

		ctx->last_input_jiffies = jiffies;
		ctx->static_idle_ms = 0;
		ctx->applied_rr = 0;
		if (ctx->prepared && !ctx->in_aod) {
			s6e3fac_apply_vrr_locked(ctx, true);
			s6e3fac_schedule_idle_check_locked(ctx);
		}
		if (ctx->connector)
			drm_dev = ctx->connector->dev;
	}
	mutex_unlock(&ctx->lock);

	if (drm_dev)
		drm_kms_helper_hotplug_event(drm_dev);

	return count;
}
static DEVICE_ATTR_RW(game_mode);

static ssize_t hbm_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->hbm_mode);
}

static ssize_t hbm_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret || val > 2)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	if (ctx->hbm_mode != val) {
		ctx->hbm_mode = val;
		if (ctx->prepared && !ctx->in_aod) {
			s6e3fac_apply_mdnie_locked(ctx);
			s6e3fac_apply_brightness_locked(ctx);
		}
	}
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(hbm_mode);

static ssize_t hbm_dbv_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->hbm_dbv);
}

static ssize_t hbm_dbv_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	u16 val;
	int ret;

	ret = kstrtou16(buf, 10, &val);
	if (ret || val < S6E3FAC_MIN_HBM_DBV || val > S6E3FAC_MAX_HBM_DBV)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->hbm_dbv = val;
	if (ctx->hbm_mode == 2 && ctx->prepared && !ctx->in_aod)
		s6e3fac_apply_brightness_locked(ctx);
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(hbm_dbv);

static ssize_t colour_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;

	mutex_lock(&ctx->lock);
	for (i = 0; i < S6E3FAC_COLOUR_MAX; i++) {
		if (ctx->colour_mode == i)
			len += sysfs_emit_at(buf, len, "[%s]%s", s6e3fac_colour_names[i],
					     (i + 1 == S6E3FAC_COLOUR_MAX) ? "\n" : " ");
		else
			len += sysfs_emit_at(buf, len, "%s%s", s6e3fac_colour_names[i],
					     (i + 1 == S6E3FAC_COLOUR_MAX) ? "\n" : " ");
	}
	mutex_unlock(&ctx->lock);

	return len;
}

static ssize_t colour_mode_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < S6E3FAC_COLOUR_MAX; i++) {
		if (sysfs_streq(buf, s6e3fac_colour_names[i])) {
			mutex_lock(&ctx->lock);
			ctx->colour_mode = i;
			if (ctx->prepared && !ctx->in_aod) {
				s6e3fac_apply_mdnie_locked(ctx);
				s6e3fac_apply_brightness_locked(ctx);
			}
			mutex_unlock(&ctx->lock);
			return count;
		}
	}

	return -EINVAL;
}
static DEVICE_ATTR_RW(colour_mode);

static ssize_t white_balance_rgb_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u %u %u\n", ctx->wb_r, ctx->wb_g, ctx->wb_b);
}

static ssize_t white_balance_rgb_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	unsigned int r, g, b;

	if (sscanf(buf, "%u %u %u", &r, &g, &b) != 3)
		return -EINVAL;
	if (r < 128 || r > 255 || g < 128 || g > 255 || b < 128 || b > 255)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->wb_r = r;
	ctx->wb_g = g;
	ctx->wb_b = b;
	if (ctx->prepared && !ctx->in_aod)
		s6e3fac_apply_mdnie_locked(ctx);
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(white_balance_rgb);

static ssize_t night_kelvin_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->night_kelvin);
}

static ssize_t night_kelvin_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	u16 kelvin;
	int ret;

	ret = kstrtou16(buf, 10, &kelvin);
	if (ret || kelvin < 2000 || kelvin > 7000)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->night_kelvin = kelvin;
	if (ctx->colour_mode == S6E3FAC_COLOUR_NIGHT && ctx->prepared && !ctx->in_aod)
		s6e3fac_apply_mdnie_locked(ctx);
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(night_kelvin);

static ssize_t acl_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->acl_mode);
}

static ssize_t acl_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	u8 val;
	int ret;

	ret = kstrtou8(buf, 10, &val);
	if (ret || val > 3)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->acl_mode = val;
	if (ctx->prepared && !ctx->in_aod)
		s6e3fac_apply_acl_locked(ctx);
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(acl_mode);

static ssize_t factory_calibration_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "otp_read=%u cie_x=%u cie_y=%u cal_rgb=%u,%u,%u\n",
			  ctx->otp_read, ctx->mdnie_x, ctx->mdnie_y,
			  ctx->cal_r, ctx->cal_g, ctx->cal_b);
}
static DEVICE_ATTR_RO(factory_calibration);

static ssize_t aod_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u (active=%u)\n", ctx->aod_enabled ? 1 : 0,
			  ctx->in_aod ? 1 : 0);
}

static ssize_t aod_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;

	mutex_lock(&ctx->lock);
	ctx->aod_enabled = en;
	if (!en && ctx->in_aod) {
		ctx->in_aod = false;
		ctx->applied_rr = 0;
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
	}
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(aod_mode);

static ssize_t aod_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", ctx->aod_nits);
}

static ssize_t aod_brightness_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct s6e3fac *ctx = dev_get_drvdata(dev);
	unsigned int nits;
	int ret;

	ret = kstrtouint(buf, 10, &nits);
	if (ret)
		return ret;
	if (nits != 2 && nits != 10 && nits != 30 && nits != 60)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->aod_nits = nits;
	mutex_unlock(&ctx->lock);

	return count;
}
static DEVICE_ATTR_RW(aod_brightness);

static struct attribute *s6e3fac_attrs[] = {
	&dev_attr_vrr_mode.attr,
	&dev_attr_video_mode.attr,
	&dev_attr_game_mode.attr,
	&dev_attr_hbm_mode.attr,
	&dev_attr_hbm_dbv.attr,
	&dev_attr_colour_mode.attr,
	&dev_attr_white_balance_rgb.attr,
	&dev_attr_night_kelvin.attr,
	&dev_attr_acl_mode.attr,
	&dev_attr_factory_calibration.attr,
	&dev_attr_aod_mode.attr,
	&dev_attr_aod_brightness.attr,
	NULL,
};
ATTRIBUTE_GROUPS(s6e3fac);

static int s6e3fac_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fac *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3fac, panel,
				   &s6e3fac_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_init(&ctx->lock);
	INIT_WORK(&ctx->vrr_boost_work, s6e3fac_vrr_boost_work_fn);
	INIT_DELAYED_WORK(&ctx->vrr_idle_work, s6e3fac_vrr_idle_work_fn);

	ctx->cur_vrefresh = (refresh == 60) ? 60 : 120;
	ctx->effective_rr = ctx->cur_vrefresh;
	ctx->vrr_policy = S6E3FAC_VRR_DYNAMIC;
	ctx->last_input_jiffies = jiffies;
	ctx->hbm_mode = 0;
	ctx->hbm_dbv = S6E3FAC_MAX_HBM_DBV;
	ctx->last_normal_dbv = 1023;
	ctx->colour_mode = S6E3FAC_COLOUR_VIVID;
	ctx->night_kelvin = 4300;
	ctx->acl_mode = 1;
	ctx->cal_r = 0xff;
	ctx->cal_g = 0xff;
	ctx->cal_b = 0xff;
	ctx->wb_r = 0xff;
	ctx->wb_g = 0xff;
	ctx->wb_b = 0xff;
	ctx->aod_enabled = false;
	ctx->aod_nits = 10;

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

	/* Command mode panel, frames are paced by TE */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e3fac_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	ctx->input_handler.event = s6e3fac_input_event;
	ctx->input_handler.connect = s6e3fac_input_connect;
	ctx->input_handler.disconnect = s6e3fac_input_disconnect;
	ctx->input_handler.name = "s6e3fac-vrr";
	ctx->input_handler.id_table = s6e3fac_input_ids;
	ctx->input_handler.private = ctx;
	if (input_register_handler(&ctx->input_handler) == 0)
		ctx->input_registered = true;

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret) {
		if (ctx->input_registered)
			input_unregister_handler(&ctx->input_handler);
		return ret;
	}

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

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret && ctx->input_registered)
		input_unregister_handler(&ctx->input_handler);

	return ret;
}

static void s6e3fac_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fac *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->input_registered)
		input_unregister_handler(&ctx->input_handler);
	cancel_work_sync(&ctx->vrr_boost_work);
	cancel_delayed_work_sync(&ctx->vrr_idle_work);
}

static void s6e3fac_shutdown(struct mipi_dsi_device *dsi)
{
	struct s6e3fac *ctx = mipi_dsi_get_drvdata(dsi);

	cancel_work_sync(&ctx->vrr_boost_work);
	cancel_delayed_work_sync(&ctx->vrr_idle_work);

	mutex_lock(&ctx->lock);
	if (ctx->in_aod) {
		ctx->in_aod = false;
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fac_supplies), ctx->supplies);
	}
	mutex_unlock(&ctx->lock);
}

static const struct of_device_id s6e3fac_of_match[] = {
	{ .compatible = "samsung,s6e3fac-amb606aw01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fac_of_match);

static struct mipi_dsi_driver s6e3fac_driver = {
	.probe = s6e3fac_probe,
	.remove = s6e3fac_remove,
	.shutdown = s6e3fac_shutdown,
	.driver = {
		.name = "panel-samsung-s6e3fac",
		.of_match_table = s6e3fac_of_match,
		.dev_groups = s6e3fac_groups,
	},
};
module_mipi_dsi_driver(s6e3fac_driver);

MODULE_DESCRIPTION("DRM driver for Samsung S6E3FAC AMB606AW01 command mode DSI panel");
MODULE_LICENSE("GPL");
