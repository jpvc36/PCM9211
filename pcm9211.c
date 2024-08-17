/*
 * PCM9211 codec driver
 *
 * Copyright (C) 2017 jusst technologies GmbH / jusst.engineering
 *
 * Author; Julian Scheel <julian@jusst.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <sound/control.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm9211.h"

#define PCM9211_MAX_SYSCLK 24576000

#define PCM9211_SUPPLIES 4
static const char *const pcm9211_supply_names[PCM9211_SUPPLIES] = {
	"VCCAD",
	"VCC",
	"VDDRX",
	"DVDD",
};

struct pcm9211_priv {
	struct regulator_bulk_data supplies[PCM9211_SUPPLIES];
	struct snd_pcm_hw_constraint_list rate_constraints;
	struct delayed_work npcm_clear_work;
	struct snd_kcontrol *preamble_ctl;
	struct snd_kcontrol *npcm_ctl;
	struct snd_kcontrol *rate_ctl;
	struct snd_kcontrol *dts_ctl;
//	struct snd_soc_codec *codec;
	struct snd_soc_component *codec;
	struct gpio_desc *reset;
	struct gpio_desc *int0;
	struct regmap *regmap;
	struct device *dev;
	struct clk *xti;

	unsigned int dir_rate;
	unsigned long sysclk;
	u8 burst_preamble[4];
	u8 npcm_state;
};

static const struct regmap_range pcm9211_reg_rd_range[] = {
	regmap_reg_range(PCM9211_ERR_OUT, PCM9211_PD_BUF1),
	regmap_reg_range(PCM9211_SYS_RESET, PCM9211_SYS_RESET),
	regmap_reg_range(PCM9211_ADC_CTRL1, PCM9211_ADC_CTRL1),
	regmap_reg_range(PCM9211_ADC_L_CH_ATT, PCM9211_ADC_CTRL3),
	regmap_reg_range(PCM9211_DIR_STATUS1, PCM9211_DIT_STATUS6),
	regmap_reg_range(PCM9211_MAIN_AUX_MUTE, PCM9211_MPIO_C_DATA_IN),
};

static const struct regmap_access_table pcm9211_reg_rd_table = {
	.yes_ranges = pcm9211_reg_rd_range,
	.n_yes_ranges = ARRAY_SIZE(pcm9211_reg_rd_range),
};

static const struct regmap_range pcm9211_reg_wr_range[] = {
	regmap_reg_range(PCM9211_ERR_OUT, PCM9211_INT1_CAUSE),
	regmap_reg_range(PCM9211_INT_POLARITY, PCM9211_FS_CALC_TARGET),
	regmap_reg_range(PCM9211_SYS_RESET, PCM9211_SYS_RESET),
	regmap_reg_range(PCM9211_ADC_CTRL1, PCM9211_ADC_CTRL1),
	regmap_reg_range(PCM9211_ADC_L_CH_ATT, PCM9211_ADC_CTRL3),
	regmap_reg_range(PCM9211_DIT_CTRL1, PCM9211_DIT_STATUS6),
	regmap_reg_range(PCM9211_MAIN_AUX_MUTE, PCM9211_MPIO_C_DATA_OUT),
};

static const struct regmap_access_table pcm9211_reg_wr_table = {
	.yes_ranges = pcm9211_reg_wr_range,
	.n_yes_ranges = ARRAY_SIZE(pcm9211_reg_wr_range),
};

static const struct regmap_range pcm9211_reg_volatile_range[] = {
	regmap_reg_range(PCM9211_INT0_OUT, PCM9211_INT1_OUT),
	regmap_reg_range(PCM9211_BIPHASE_INFO, PCM9211_PD_BUF1),
	regmap_reg_range(PCM9211_DIR_STATUS1, PCM9211_DIR_STATUS6),
};

static const struct regmap_access_table pcm9211_reg_volatile_table = {
	.yes_ranges = pcm9211_reg_volatile_range,
	.n_yes_ranges = ARRAY_SIZE(pcm9211_reg_volatile_range),
};

static const struct reg_default pcm9211_reg_defaults[] = {
	{ PCM9211_ERR_OUT, 0x00 },
	{ PCM9211_DIR_INITIAL1, 0x00 },
	{ PCM9211_DIR_INITIAL2, 0x01 },
	{ PCM9211_DIR_INITIAL3, 0x04 },
	{ PCM9211_OSC_CTRL, 0x00 },
	{ PCM9211_ERR_CAUSE, 0x01 },
	{ PCM9211_AUTO_SEL_CAUSE, 0x01 },
	{ PCM9211_DIR_FS_RANGE, 0x00 },
	{ PCM9211_NON_PCM_DEF, 0x03 },
	{ PCM9211_DTS_CD_LD, 0x0c },
	{ PCM9211_INT0_CAUSE, 0xff },
	{ PCM9211_INT1_CAUSE, 0xff },
	{ PCM9211_INT0_OUT, 0x00 },
	{ PCM9211_INT1_OUT, 0x00 },
	{ PCM9211_INT_POLARITY, 0x00 },
	{ PCM9211_DIR_OUT_FMT, 0x04 },
	{ PCM9211_DIR_RSCLK_RATIO, 0x02 },
	{ PCM9211_XTI_SCLK_FREQ, 0x1a },
	{ PCM9211_DIR_SOURCE_BIT2, 0x22 },
	{ PCM9211_XTI_SOURCE_BIT2, 0x22 },
	{ PCM9211_DIR_INP_BIPHASE, 0xc2 },
	{ PCM9211_RECOUT0_BIPHASE, 0x02 },
	{ PCM9211_RECOUT1_BIPHASE, 0x02 },
	{ PCM9211_FS_CALC_TARGET, 0x00 },
	{ PCM9211_FS_CALC_RESULT, 0x08 },
	{ PCM9211_BIPHASE_INFO, 0x08 },
	{ PCM9211_PC_BUF0, 0x01 },
	{ PCM9211_PC_BUF1, 0x00 },
	{ PCM9211_PD_BUF0, 0x20 },
	{ PCM9211_PD_BUF1, 0x57 },
	{ PCM9211_SYS_RESET, 0x40 },
	{ PCM9211_ADC_CTRL1, 0x02 },
	{ PCM9211_ADC_L_CH_ATT, 0xd7 },
	{ PCM9211_ADC_R_CH_ATT, 0xd7 },
	{ PCM9211_ADC_CTRL2, 0x00 },
	{ PCM9211_ADC_CTRL3, 0x00 },
	{ PCM9211_DIR_STATUS1, 0x04 },
	{ PCM9211_DIR_STATUS2, 0x00 },
	{ PCM9211_DIR_STATUS3, 0x00 },
	{ PCM9211_DIR_STATUS4, 0x00 },
	{ PCM9211_DIR_STATUS5, 0x00 },
	{ PCM9211_DIR_STATUS6, 0x00 },
	{ PCM9211_DIT_CTRL1, 0x44 },
	{ PCM9211_DIT_CTRL2, 0x10 },
	{ PCM9211_DIT_CTRL3, 0x00 },
	{ PCM9211_DIT_STATUS1, 0x00 },
	{ PCM9211_DIT_STATUS2, 0x00 },
	{ PCM9211_DIT_STATUS3, 0x00 },
	{ PCM9211_DIT_STATUS4, 0x00 },
	{ PCM9211_DIT_STATUS5, 0x00 },
	{ PCM9211_DIT_STATUS6, 0x00 },
	{ PCM9211_MAIN_AUX_MUTE, 0x00 },
	{ PCM9211_MAIN_OUT_SOURCE, 0x00 },
	{ PCM9211_AUX_OUT_SOURCE, 0x00 },
	{ PCM9211_MPIO_B_MAIN_HIZ, 0x00 },
	{ PCM9211_MPIO_C_MPIO_A_HIZ, 0x0f },
	{ PCM9211_MPIO_GROUP, 0x40 },
	{ PCM9211_MPIO_A_FLAGS, 0x00 },
	{ PCM9211_MPIO_B_MPIO_C_FLAGS, 0x00 },
	{ PCM9211_MPIO_A1_A0_OUT_FLAG, 0x00 },
	{ PCM9211_MPIO_A3_A2_OUT_FLAG, 0x00 },
	{ PCM9211_MPIO_B1_B0_OUT_FLAG, 0x00 },
	{ PCM9211_MPIO_B3_B2_OUT_FLAG, 0x00 },
	{ PCM9211_MPIO_C1_C0_OUT_FLAG, 0x00 },
	{ PCM9211_MPIO_C3_C2_OUT_FLAG, 0x00 },
	{ PCM9211_MPO_1_0_FUNC, 0x3d },
	{ PCM9211_MPIO_A_B_DIR, 0x00 },
	{ PCM9211_MPIO_C_DIR, 0x00 },
	{ PCM9211_MPIO_A_B_DATA_OUT, 0x00 },
	{ PCM9211_MPIO_C_DATA_OUT, 0x00 },
	{ PCM9211_MPIO_A_B_DATA_IN, 0x00 },
	{ PCM9211_MPIO_C_DATA_IN, 0x02 },
};

const struct regmap_config pcm9211_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PCM9211_MPIO_C_DATA_IN,
	.wr_table = &pcm9211_reg_wr_table,
	.rd_table = &pcm9211_reg_rd_table,
	.volatile_table = &pcm9211_reg_volatile_table,
	.reg_defaults = pcm9211_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(pcm9211_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(pcm9211_regmap);

static const u32 adc_rates[] = { 48000, 92000 };
static const struct snd_pcm_hw_constraint_list adc_rate_constraints = {
	.count = ARRAY_SIZE(adc_rates),
	.list = adc_rates,
};

static const int biphase_rates[] = {
	0, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
	48000, 64000, 88200, 96000, 128000, 176400, 192000
};

static const unsigned int pcm9211_sck_ratios[] = { 1, 2, 4, 8 };
static const unsigned int pcm9211_bck_ratios[] = { 2, 4, 8, 16 };
static const unsigned int pcm9211_lrck_ratios[] = { 128, 256, 512, 1024 };
#define PCM9211_NUM_SCK_RATIOS ARRAY_SIZE(pcm9211_sck_ratios)
#define PCM9211_NUM_BCK_RATIOS ARRAY_SIZE(pcm9211_bck_ratios)
#define PCM9211_NUM_LRCK_RATIOS ARRAY_SIZE(pcm9211_lrck_ratios)

static int pcm9211_main_output_port(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	unsigned int port;
	int ret;

	ret = regmap_read(priv->regmap, PCM9211_MAIN_OUT_SOURCE, &val);
	if (ret) {
		dev_err(dev, "Failed to read selected source: %d\n", ret);
		return ret;
	}

	port = (val & PCM9211_MOPSRC_MASK) >> PCM9211_MOPSRC_SHIFT;
	if (port == PCM9211_MOSRC_AUTO) {
		ret = regmap_read(priv->regmap, PCM9211_BIPHASE_INFO, &val);
		if (ret) {
			dev_err(dev, "Failed to read biphase information: %d\n",
					ret);
			return ret;
		}

		/* Assumes that Sampling Frequency Status calculation
		 * corresponds with DIR Lock, which seems to to be exposed to
		 * any register directly
		 */
		if ((val & PCM9211_BIPHASE_SFSST_MASK) == 0)
			port = PCM9211_MOSRC_DIR;
		else
			port = PCM9211_MOSRC_ADC;
	}

	return port;
}

static int pcm9211_dir_rate(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, PCM9211_BIPHASE_INFO, &val);
	if (ret) {
		dev_err(dev, "Failed to read biphase information: %d\n", ret);
		return ret;
	}

	if ((val & PCM9211_BIPHASE_SFSST_MASK)) {
		dev_dbg(dev, "Biphase Fs calculation not locked\n");
		return 0;
	}

	return biphase_rates[(val & PCM9211_BIPHASE_SFSOUT_MASK)
		>> PCM9211_BIPHASE_SFSOUT_SHIFT];
}

static int pcm9211_read_burst_preamble(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = regmap_raw_read(priv->regmap, PCM9211_PC_BUF0,
			priv->burst_preamble, 4);
	if (ret) {
		dev_err(dev, "Failed to read burst preamble: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "Burst preamble: 0x%02x 0x%02x 0x%02x 0x%02x\n",
			priv->burst_preamble[0], priv->burst_preamble[1],
			priv->burst_preamble[2], priv->burst_preamble[3]);

	return 0;
}

static int pcm9211_dir_rate_kctl_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 8000;
	uinfo->value.integer.min = 96000;

	return 0;
}

static int pcm9211_dir_rate_kctl(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = priv->dev;

	/* If we have an interrupt connected dir_rate is up-to-date */
	if (!priv->int0)
		priv->dir_rate = pcm9211_dir_rate(dev);

	ucontrol->value.integer.value[0] = priv->dir_rate;

	return 0;
}

#define pcm9211_dir_npcm_kctl_info snd_ctl_boolean_mono_info
#define pcm9211_dir_npcm_kctl pcm9211_int0_kctl

#define pcm9211_dir_dtscd_kctl_info snd_ctl_boolean_mono_info
#define pcm9211_dir_dtscd_kctl pcm9211_int0_kctl

static int pcm9211_int0_kctl(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(component);
	u8 mask = kcontrol->private_value & 0xff;
	struct device *dev = priv->dev;
	unsigned int cause;
	int ret;

	/* If interrupt line is not connected read the last interrupt state */
	if (!priv->int0) {
		ret = regmap_read(priv->regmap, PCM9211_INT0_OUT, &cause);
		if (ret) {
			dev_err(dev, "Failed to read int0 cause: %d\n", ret);
			return IRQ_HANDLED;
		}
		priv->npcm_state = cause & (PCM9211_INT0_MNPCM0_MASK |
				PCM9211_INT0_MDTSCD0_MASK);
	}

	ucontrol->value.integer.value[0] = (priv->npcm_state & mask) == mask;

	return 0;
}

static int pcm9211_dir_preamble_kctl_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 4;

	return 0;
}

static int pcm9211_dir_preamble_kctl(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = priv->dev;

	/* If we have an interrupt connected preamble is up-to-date */
	if (!priv->int0)
		priv->dir_rate = pcm9211_read_burst_preamble(dev);

	memcpy(ucontrol->value.bytes.data, priv->burst_preamble, 4);

	return 0;
}

static struct snd_kcontrol* pcm9211_get_ctl(struct device *dev, const char *name)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	struct snd_ctl_elem_id elem_id;

	memset(&elem_id, 0, sizeof(elem_id));
	elem_id.iface = SNDRV_CTL_ELEM_IFACE_PCM;
	strcpy(elem_id.name, name);
//	return snd_ctl_find_id(priv->codec->component.card->snd_card, &elem_id);
	return snd_ctl_find_id(priv->codec->card->snd_card, &elem_id);
}

static struct snd_kcontrol* pcm9211_get_rate_ctl(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

	if (!priv->rate_ctl)
		priv->rate_ctl = pcm9211_get_ctl(dev, "DIR Sample Rate");

	return priv->rate_ctl;
}

static struct snd_kcontrol* pcm9211_get_npcm_ctl(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

	if (!priv->npcm_ctl)
		priv->npcm_ctl = pcm9211_get_ctl(dev, "DIR Non-PCM Bitstream");

	return priv->npcm_ctl;
}

static struct snd_kcontrol* pcm9211_get_dtscd_ctl(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

	if (!priv->dts_ctl)
		priv->dts_ctl = pcm9211_get_ctl(dev, "DIR DTS Bitstream");

	return priv->dts_ctl;
}

static struct snd_kcontrol* pcm9211_get_burst_preamble_ctl(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

	if (!priv->preamble_ctl)
		priv->preamble_ctl = pcm9211_get_ctl(dev, "DIR Burst Preamble");

	return priv->preamble_ctl;
}

static irqreturn_t pcm9211_interrupt(int irq, void *data)
{
	struct pcm9211_priv *priv = data;
	struct device *dev = priv->dev;

	unsigned int cause;
	int rate;
	int ret;

	ret = regmap_read(priv->regmap, PCM9211_INT0_OUT, &cause);
	if (ret) {
		dev_err(dev, "Failed to read int0 cause: %d\n", ret);
		return IRQ_HANDLED;
	}

	if (cause & PCM9211_INT0_MFSCHG0_MASK) {
		/* Interrupt is generated before the Fs calculation has
		 * finished. Give it time to settle.
		 */
		usleep_range(15000, 16000);
		rate = pcm9211_dir_rate(dev);

		if (rate < 0) {
			dev_err(dev, "Failed to retrieve DIR rate: %d\n", rate);
			goto preamble;
		}

		if (rate == priv->dir_rate)
			goto preamble;

		priv->dir_rate = rate;
		dev_dbg(dev, "DIR sampling rate changed to: %d\n", rate);

		if (priv->codec == NULL || pcm9211_get_rate_ctl(dev) == NULL)
			goto preamble;

		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_rate_ctl(dev)->id);
	}

preamble:
	if ((cause & PCM9211_INT0_MPCRNW0_MASK)) {
		if (pcm9211_read_burst_preamble(dev) < 0)
			goto npcm;

		if (priv->codec == NULL ||
				pcm9211_get_burst_preamble_ctl(dev) == NULL)
			goto dts;

		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_burst_preamble_ctl(dev)->id);
	}

npcm:
	if ((cause & PCM9211_INT0_MNPCM0_MASK)) {
		/* PCM9211 does not generate an interrupt for NPCM0 1->0
		 * transition, but continously generates interrupts as long as
		 * NPCM0 is high, so use a timeout to clear */
		cancel_delayed_work_sync(&priv->npcm_clear_work);
		queue_delayed_work(system_wq, &priv->npcm_clear_work,
				msecs_to_jiffies(100));


		if ((cause & PCM9211_INT0_MNPCM0_MASK) !=
				(priv->npcm_state & PCM9211_INT0_MNPCM0_MASK))
			dev_dbg(dev, "NPCM status on interrupt: %d\n",
					(cause & PCM9211_INT0_MNPCM0_MASK) ==
					PCM9211_INT0_MNPCM0_MASK);

		priv->npcm_state = (priv->npcm_state &
				~PCM9211_INT0_MNPCM0_MASK) |
			(cause & PCM9211_INT0_MNPCM0_MASK);

		if (priv->codec == NULL || pcm9211_get_npcm_ctl(dev) == NULL)
			goto dts;

		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_npcm_ctl(dev)->id);
	}

dts:
	if (cause & PCM9211_INT0_MDTSCD0_MASK) {
		dev_dbg(dev, "DTSCD status on interrupt: %d\n",
				(cause & PCM9211_INT0_MDTSCD0_MASK) ==
				PCM9211_INT0_MDTSCD0_MASK);
		priv->npcm_state |= PCM9211_INT0_MDTSCD0_MASK;

		if (priv->codec == NULL || pcm9211_get_dtscd_ctl(dev) == NULL)
			return IRQ_HANDLED;

		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_dtscd_ctl(dev)->id);
	}

	return IRQ_HANDLED;
}

static int pcm9211_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
//	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_component *codec = dai->component;
//	struct pcm9211_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	int port;

	port = pcm9211_main_output_port(dev);
	if (port < 0) {
		dev_err(dev, "Failed to read selected port: %d\n", port);
		return port;
	}

	if (port == PCM9211_MOSRC_ADC)
		return snd_pcm_hw_constraint_list(substream->runtime,
				0, SNDRV_PCM_HW_PARAM_RATE,
				&adc_rate_constraints);

	priv->dir_rate = pcm9211_dir_rate(dev);
	priv->rate_constraints.count = 1;
	priv->rate_constraints.list = &priv->dir_rate;
	priv->rate_constraints.mask = 0;

	dev_dbg(dev, "Detected biphase rate is %d Hz\n", priv->dir_rate);

	return snd_pcm_hw_constraint_list(substream->runtime,
			0, SNDRV_PCM_HW_PARAM_RATE, &priv->rate_constraints);
}

static int pcm9211_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
//	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_component *codec = dai->component;
//	struct pcm9211_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;

	int ret;

	if (freq > PCM9211_MAX_SYSCLK) {
		dev_err(dev, "System clock greater %d is not supported\n",
				PCM9211_MAX_SYSCLK);
		return -EINVAL;
	}

	ret = clk_set_rate(priv->xti, freq);
	if (ret)
		return ret;

	priv->sysclk = freq;

	return 0;
}

static int pcm9211_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int format)
{
//	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_component *codec = dai->component;
//	struct pcm9211_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	u32 adfmt, dirfmt;
	int ret;

	/* Configure format for ADC and DIR block, if main output source is
	 * set to AUTO the output port may switch between them at any time
	 */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adfmt = PCM9211_ADFMT_I2S;
		dirfmt = PCM9211_DIR_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adfmt = PCM9211_ADFMT_RIGHT_J;
		dirfmt = PCM9211_DIR_FMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adfmt = PCM9211_ADFMT_LEFT_J;
		dirfmt = PCM9211_DIR_FMT_LEFT_J;
		break;
	default:
		dev_err(dev, "Unsupported DAI format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, PCM9211_ADC_CTRL2,
			PCM9211_ADFMT_MASK, adfmt << PCM9211_ADFMT_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to update ADC format: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, PCM9211_DIR_OUT_FMT,
			PCM9211_DIR_FMT_MASK, dirfmt << PCM9211_DIR_FMT_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to update ADC format: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pcm9211_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
//	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_component *codec = dai->component;
//	struct pcm9211_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(codec);
	struct device *dev = codec->dev;
	unsigned int sclk = 1;
	unsigned int rate;
	unsigned int bck;
	unsigned int ratio;
	unsigned int port;
	int ret;
	int i;

	rate = params_rate(params);
	bck = rate * 64;

	port = pcm9211_main_output_port(dev);
	if (port == PCM9211_MOSRC_ADC) {
		switch (rate) {
		case 48000:
			sclk = 12288000;
			break;
		case 96000:
			sclk = 24576000;
			break;
		default:
			dev_err(dev, "Rate %d unsupported.\n", rate);
			return -EINVAL;
		}

		/* Systemclock setup */
		ratio = priv->sysclk / sclk;
		for (i = 0; i < PCM9211_NUM_SCK_RATIOS; i++) {
			if (pcm9211_sck_ratios[i] == ratio)
				break;
		}
		if (i == PCM9211_NUM_SCK_RATIOS) {
			dev_err(dev, "SCK divider %d is not supported\n",
					ratio);
			return -EINVAL;
		}
		ret = regmap_update_bits(priv->regmap, PCM9211_XTI_SCLK_FREQ,
				PCM9211_XTI_XSCK_MASK,
				i << PCM9211_XTI_XSCK_SHIFT);

		if (ret) {
			dev_err(dev, "Failed to configure SCK divider: %d\n",
					ret);
			return ret;
		}

		/* Bitclock setup */
		ratio = priv->sysclk / bck;
		for (i = 0; i < PCM9211_NUM_BCK_RATIOS; i++) {
			if (pcm9211_bck_ratios[i] == ratio)
				break;
		}
		if (i == PCM9211_NUM_BCK_RATIOS) {
			dev_err(dev, "BCK divider %d is not supported\n",
					ratio);
			return -EINVAL;
		}
		ret = regmap_update_bits(priv->regmap, PCM9211_XTI_SCLK_FREQ,
				PCM9211_XTI_BCK_MASK,
				i << PCM9211_XTI_BCK_SHIFT);
		if (ret) {
			dev_err(dev, "Failed to configure BCK divider: %d\n",
					ret);
			return ret;
		}

		/* Frameclock setup */
		ratio = priv->sysclk / rate;
		for (i = 0; i < PCM9211_NUM_LRCK_RATIOS; i++) {
			if (pcm9211_lrck_ratios[i] == ratio)
				break;
		}
		if (i == PCM9211_NUM_LRCK_RATIOS) {
			dev_err(dev, "LRCK divider %d is not supported\n",
					ratio);
			return -EINVAL;
		}
		ret = regmap_update_bits(priv->regmap, PCM9211_XTI_SCLK_FREQ,
				PCM9211_XTI_LRCK_MASK,
				i << PCM9211_XTI_LRCK_SHIFT);
		if (ret) {
			dev_err(dev, "Failed to configure LRCK divider: %d\n",
					ret);
			return ret;
		}
	}

	return 0;
}

static int pcm9211_reset(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	int ret;

	/* Use reset gpio if available, otherwise soft-reset */
	if (priv->reset) {
		gpiod_set_value_cansleep(priv->reset, 0);
		usleep_range(500, 1000);
		gpiod_set_value_cansleep(priv->reset, 1);
	} else {
		ret = regmap_update_bits(priv->regmap, PCM9211_SYS_RESET,
				PCM9211_SYS_RESET_MRST, 0);
		if (ret) {
			dev_err(dev, "Could not reset device: %d\n", ret);
			return ret;
		}
		usleep_range(10000, 15000);
	}

	regcache_mark_dirty(priv->regmap);

	return 0;
}

static int pcm9211_write_pinconfig(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	u8 values[4];
	int val;
	int ret;
	int i;

	ret = of_property_read_u8_array(dev->of_node, "ti,group-function",
			values, 3);
	if (!ret) {
		val = (values[0] & PCM9211_MPASEL_MASK) <<					// SHOULD BE 0x03
			PCM9211_MPASEL_SHIFT |							// 6
			(values[1] & PCM9211_MPBSEL_MASK) <<					// SHOULD BE 0x07
			PCM9211_MPBSEL_SHIFT |							// 3
			(values[2] & PCM9211_MPCSEL_MASK) <<					// SHOULD BE 0x07
			PCM9211_MPCSEL_SHIFT;							// 0
		ret = regmap_write(priv->regmap, PCM9211_MPIO_GROUP, val);			// 0x6f
//		dev_dbg(dev, "MPIO_GROUP_FLAGS ret=%d val=0x%x",ret,val);
		if (ret) {
			dev_err(dev, "Failed to write mpio group functions: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-a-flags-gpio",
			values, 4);
	if (!ret) {
		/* Write MPIO A flags/gpio selection */
		for (i = 0, val = 0; i < 4; i++)
			val |= (values[i] << PCM9211_MPAxSEL_SHIFT(i)) &
				PCM9211_MPAxSEL_MASK(i);

		ret = regmap_update_bits(priv->regmap, PCM9211_MPIO_A_FLAGS,
				PCM9211_MPAxSEL_MASK(0) | PCM9211_MPAxSEL_MASK(1) |
				PCM9211_MPAxSEL_MASK(2), val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_a flags: %d\n", ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-b-flags-gpio",
			values, 4);
	if (!ret) {
		/* Write MPIO B flags/gpio selection */
		for (i = 0, val = 0; i < 4; i++)
			val |= (values[i] << PCM9211_MPBxSEL_SHIFT(i)) &			// i + 4
				PCM9211_MPBxSEL_MASK(i);					// BIT(i + 4)

		ret = regmap_update_bits(priv->regmap,
				PCM9211_MPIO_B_MPIO_C_FLAGS,					// 0x71
				PCM9211_MPBxSEL_MASK(0) |
				PCM9211_MPBxSEL_MASK(1) |
				PCM9211_MPBxSEL_MASK(2), val);
//		dev_dbg(dev, "MPIO_B_FLAGS ret=%d val=0x%x",ret,val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_b flags: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-c-flags-gpio",
			values, 4);
	if (!ret) {
		/* Write MPIO B flags/gpio selection */
		for (i = 0, val = 0; i < 4; i++)
			val |= (values[i] << PCM9211_MPCxSEL_SHIFT(i)) &			// i
				PCM9211_MPCxSEL_MASK(i);					// BIT(i)

		ret = regmap_update_bits(priv->regmap,
				PCM9211_MPIO_B_MPIO_C_FLAGS,					// 0x71
				PCM9211_MPCxSEL_MASK(0) |
				PCM9211_MPCxSEL_MASK(1) |
				PCM9211_MPCxSEL_MASK(2), val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_c flags: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-a-flag",
			values, 4);
	if (!ret) {
		/* Write MPIO A flag selection */
		for (i = 0, val = 0; i < 2; i++)
			val |= (values[i] <<
					PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_A1_A0_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_a1/0 flags: %d\n",
					ret);
			return ret;
		}

		for (i = 2, val = 0; i < 4; i++)
			val |= (values[i] <<
					PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_A3_A2_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_a3/2 flags: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-b-flag",
			values, 4);
	if (!ret) {
		/* Write MPIO B flag selection */
		for (i = 0, val = 0; i < 2; i++)
			val |= (values[i] << PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_B1_B0_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_b1/0 flags: %d\n",
					ret);
			return ret;
		}

		for (i = 2, val = 0; i < 4; i++)
			val |= (values[i] << PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_B3_B2_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_b3/2 flags: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpio-c-flag",
				  values, 4);
	if (!ret) {
		/* Write MPIO C flag selection */
		for (i = 0, val = 0; i < 2; i++)
			val |= (values[i] << PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_C1_C0_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_c1/0 flags: %d\n",
					ret);
			return ret;
		}

		for (i = 2, val = 0; i < 4; i++)
			val |= (values[i] << PCM9211_MPIO_ABCx_FLAG_SHIFT(i)) &
				PCM9211_MPIO_ABCx_FLAG_MASK(i);
		ret = regmap_write(priv->regmap, PCM9211_MPIO_C3_C2_OUT_FLAG,
				val);
		if (ret) {
			dev_err(dev, "Failed to update mpio_c3/2 flags: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,mpo-function",
			values, 2);
	if (!ret) {
		/* Write MPO function selection */
		for (i = 0, val = 0; i < 2; i++)
			val |= (values[i] << PCM9211_MPOxOUT_SHIFT(i)) &			// i * 4
				PCM9211_MPOxOUT_MASK(i);					// 0xf << (i * 4) 
		ret = regmap_write(priv->regmap, PCM9211_MPO_1_0_FUNC, val);			// 0x78
//		dev_dbg(dev, "MPO_FUNC_FLAGS ret=%d val=0x%x",ret,val);
		if (ret) {
			dev_err(dev, "Failed to update mpo function selection: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,int0-function", values, 2);
	if (!ret) {
		val = values[0] ? PCM9211_ERROR_INT0_MASK : 0;					// BIT(2)
		ret = regmap_update_bits(priv->regmap, PCM9211_ERR_OUT,				// 0x20
				PCM9211_ERROR_INT0_MASK, val);					// BIT(2)
		val = values[1];
		ret |= regmap_write(priv->regmap, PCM9211_INT0_CAUSE, val);			// 0x2a
		if (ret) {
			dev_err(dev, "Failed to update int0 function selection: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8_array(dev->of_node, "ti,int1-function", values, 2);
	if (!ret) {
		val = values[0] ? PCM9211_NPCM_INT1_MASK : 0;					// BIT(0)
		ret = regmap_update_bits(priv->regmap, PCM9211_ERR_OUT,				// 0x20
				PCM9211_NPCM_INT1_MASK, val);					// BIT(0)
		val = values[1];
		ret |= regmap_write(priv->regmap, PCM9211_INT1_CAUSE, val);			// 0x2b
		if (ret) {
			dev_err(dev, "Failed to update int1 function selection: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8(dev->of_node, "ti,adc-function", values);
	if (!ret) {
		/* Write ADC function selection */
		val = (values[0] << PCM9211_ADC_DET_LEVEL_SHIFT);				// 3
		ret = regmap_update_bits(priv->regmap, PCM9211_ADC_DET_LEVEL,			// 0x2e
				PCM9211_ADC_DET_LEVEL_MASK, val);				// 0x18
		if (ret) {
			dev_err(dev, "Failed to update adc function selection: %d\n",
					ret);
			return ret;
		}
	}

	ret = of_property_read_u8(dev->of_node, "ti,dit-output", values);
	if (!ret) {
		val = values[0];
		ret = regmap_write(priv->regmap, PCM9211_DIT_CTRL1, val);			// 0x60
//		dev_dbg(dev, "DIT_OUTPUT ret=%d val=0x%x",ret,val);
		if (ret) {
			dev_err(dev, "Failed to update DIT output selection: %d\n",
					ret);
			return ret;
		}
	}

	return 0;
}

static void pcm9211_npcm_clear_work(struct work_struct *work)
{
	struct pcm9211_priv *priv = container_of(work, struct pcm9211_priv,
			npcm_clear_work.work);
	u8 old_state = priv->npcm_state;
	struct device *dev = priv->dev;

	/* Clear NPCM & DTSCD, as DTSCD is only valid as long as NPCM is */
	priv->npcm_state &= ~(PCM9211_INT0_MNPCM0_MASK |
			PCM9211_INT0_MDTSCD0_MASK);

	dev_dbg(dev, "Clear NPCM flag after timeout\n");

	if (priv->codec == NULL || pcm9211_get_dtscd_ctl(dev) == NULL ||
			pcm9211_get_npcm_ctl(dev) == NULL)
		return;

	if (old_state & PCM9211_INT0_MNPCM0_MASK)
		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_npcm_ctl(dev)->id);

	if (old_state & PCM9211_INT0_MDTSCD0_MASK)
		snd_ctl_notify(priv->codec->card->snd_card,
				SNDRV_CTL_EVENT_MASK_VALUE,
				&pcm9211_get_dtscd_ctl(dev)->id);
}

//static int pcm9211_soc_probe(struct snd_soc_codec *codec)
static int pcm9211_soc_probe(struct snd_soc_component *codec)
{
//	struct pcm9211_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct pcm9211_priv *priv = snd_soc_component_get_drvdata(codec);

	priv->codec = codec;

	return 0;
}

/* Simple Controls */
static const DECLARE_TLV_DB_SCALE(pcm9211_adc_tlv, -10050, 50, 1);
static const char *const pcm9211_main_outputs[] = { "AUTO", "DIR", "ADC",
	"AUXIN0", "AUXIN1", "AUXIN2" };
static const struct soc_enum pcm9211_main_sclk_enum =
	SOC_ENUM_SINGLE(PCM9211_MAIN_OUT_SOURCE, 4, 4, pcm9211_main_outputs);

static const struct snd_kcontrol_new pcm9211_snd_controls[] = {
//	SOC_DOUBLE_R_RANGE_TLV("ADC Attenuation",
	SOC_DOUBLE_R_TLV("ADC Attenuation",
			PCM9211_ADC_L_CH_ATT,
			PCM9211_ADC_R_CH_ATT,
//			0, 14, 255, 0, pcm9211_adc_tlv),
			0, 255, 0, pcm9211_adc_tlv),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "DIR Sample Rate",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = pcm9211_dir_rate_kctl_info,
		.get = pcm9211_dir_rate_kctl,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "DIR Non-PCM Bitstream",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = pcm9211_dir_npcm_kctl_info,
		.get = pcm9211_dir_npcm_kctl,
		.private_value = PCM9211_INT0_MNPCM0_MASK,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "DIR DTS Bitstream",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = pcm9211_dir_dtscd_kctl_info,
		.get = pcm9211_dir_dtscd_kctl,
		.private_value = PCM9211_INT0_MDTSCD0_MASK,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "DIR Burst Preamble",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = pcm9211_dir_preamble_kctl_info,
		.get = pcm9211_dir_preamble_kctl,
	},
	SOC_ENUM("MAIN SCLK Output Select", pcm9211_main_sclk_enum),
};

/* DAPM Controls */
static const char *const pcm9211_dir_inputs[] = { "RXIN0", "RXIN1", "RXIN2",
	"RXIN3", "RXIN4", "RXIN5", "RXIN6", "RXIN7" };
static const int pcm9211_dir_values[] = { 0x40, 0x81, 2, 3 ,4 ,5 ,6 ,7 };
static const struct soc_enum pcm9211_dir_mux_enum =
//	SOC_ENUM_SINGLE(PCM9211_DIR_INP_BIPHASE, 0, 8, pcm9211_dir_inputs);					// (xreg, xshift, xitems, xtexts)
	SOC_VALUE_ENUM_SINGLE(PCM9211_DIR_INP_BIPHASE, 0, 0xff, 8, pcm9211_dir_inputs, pcm9211_dir_values);	// (xreg, xshift, xmask, xitems, xtexts, xvalues)
static const struct snd_kcontrol_new pcm9211_dir_mux_control =
	SOC_DAPM_ENUM("DIR Input Select", pcm9211_dir_mux_enum);

static const struct soc_enum pcm9211_main_out_enum =
	SOC_ENUM_SINGLE(PCM9211_MAIN_OUT_SOURCE, 0, 4, pcm9211_main_outputs);
static const struct snd_kcontrol_new pcm9211_main_out_control =
	SOC_DAPM_ENUM("MAIN Output Select", pcm9211_main_out_enum);

/* DAPM widgets */
static const struct snd_soc_dapm_widget pcm9211_dapm_widgets[] = {
	/* Inputs */
	SND_SOC_DAPM_INPUT("RXIN0"),
	SND_SOC_DAPM_INPUT("RXIN1"),
	SND_SOC_DAPM_INPUT("RXIN2"),
	SND_SOC_DAPM_INPUT("RXIN3"),
	SND_SOC_DAPM_INPUT("RXIN4"),
	SND_SOC_DAPM_INPUT("RXIN5"),
	SND_SOC_DAPM_INPUT("RXIN6"),
	SND_SOC_DAPM_INPUT("RXIN7"),
	SND_SOC_DAPM_INPUT("VINL"),
	SND_SOC_DAPM_INPUT("VINR"),

	SND_SOC_DAPM_ADC("ADC", NULL, PCM9211_SYS_RESET,
			PCM9211_SYS_RESET_ADDIS_SHIFT, 1),

	/* Processing */
	SND_SOC_DAPM_AIF_IN("DIR", NULL, 0, PCM9211_SYS_RESET,
			PCM9211_SYS_RESET_RXDIS_SHIFT, 1),
	SND_SOC_DAPM_MIXER("AUTO", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Internal routing */
	SND_SOC_DAPM_MUX("DIR Input Mux", SND_SOC_NOPM, 0, 0,
			&pcm9211_dir_mux_control),
	SND_SOC_DAPM_MUX("MAIN Output Mux", SND_SOC_NOPM, 0, 0,
			&pcm9211_main_out_control),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("MAIN"),
};

/* DAPM Routing */
static const struct snd_soc_dapm_route pcm9211_dapm_routes[] = {
	{ "DIR Input Mux", "RXIN0", "RXIN0" },
	{ "DIR Input Mux", "RXIN1", "RXIN1" },
	{ "DIR Input Mux", "RXIN2", "RXIN2" },
	{ "DIR Input Mux", "RXIN3", "RXIN3" },
	{ "DIR Input Mux", "RXIN4", "RXIN4" },
	{ "DIR Input Mux", "RXIN5", "RXIN5" },
	{ "DIR Input Mux", "RXIN6", "RXIN6" },
	{ "DIR Input Mux", "RXIN7", "RXIN7" },

	{ "ADC", NULL, "VINL" },
	{ "ADC", NULL, "VINR" },

	{ "DIR", NULL, "DIR Input Mux" },
	{ "AUTO", NULL, "DIR" },
	{ "AUTO", NULL, "ADC" },

	{ "MAIN Output Mux", "DIR", "DIR" },
	{ "MAIN Output Mux", "ADC", "ADC" },
	{ "MAIN Output Mux", "AUTO", "AUTO" },

	{ "MAIN", NULL, "MAIN Output Mux" },
};

static struct snd_soc_dai_ops pcm9211_dai_ops = {
	.startup = pcm9211_startup,
	.hw_params = pcm9211_hw_params,
	.set_sysclk = pcm9211_set_dai_sysclk,
	.set_fmt = pcm9211_set_dai_fmt,
};

/* BCLK is always 64 * FS == 32 bit/channel */
#define PCM9211_FORMATS SNDRV_PCM_FMTBIT_S32_LE
struct snd_soc_dai_driver pcm9211_dai = {
	.name = "pcm9211-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = PCM9211_FORMATS,
	},
	.ops = &pcm9211_dai_ops,
};

//static const struct snd_soc_codec_driver pcm9211_driver = {
static const struct snd_soc_component_driver pcm9211_driver = {
	.probe = pcm9211_soc_probe,
//	.component_driver = {
		.controls = pcm9211_snd_controls,
		.num_controls = ARRAY_SIZE(pcm9211_snd_controls),
		.dapm_widgets = pcm9211_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(pcm9211_dapm_widgets),
		.dapm_routes = pcm9211_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(pcm9211_dapm_routes),
//	},
};

int pcm9211_probe(struct device *dev, struct regmap *regmap)
{
	struct pcm9211_priv *priv;
	unsigned int cause;
	int ret;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->dev = dev;
	priv->regmap = regmap;
/*
	priv->xti = devm_clk_get(dev, "xti");
	if (IS_ERR(priv->xti)) {
		ret = PTR_ERR(priv->xti);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get clock 'xti': %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->xti);
	if (ret) {
		dev_err(dev, "Failed to enable xti clock: %d\n", ret);
		return ret;
	}
	priv->sysclk = clk_get_rate(priv->xti);
*/
	priv->sysclk = 24576000;
	if (priv->sysclk > PCM9211_MAX_SYSCLK) {
		dev_err(dev, "xti clock rate (%lu) exceeds supported max %u\n",
				priv->sysclk, PCM9211_MAX_SYSCLK);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->supplies); i++)
		priv->supplies[i].supply = pcm9211_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies),
			priv->supplies);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies),
			priv->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		dev_err(dev, "Failed to get reset gpio: %d\n", ret);
		return ret;
	}

	pcm9211_reset(dev);

	priv->int0 = devm_gpiod_get_optional(dev, "int0", GPIOD_IN);
	if (IS_ERR(priv->int0)) {
		ret = PTR_ERR(priv->int0);
		dev_err(dev, "Failed to get int0 gpio: %d\n", ret);
		return ret;
	}

	if (priv->int0) {
		int irq = gpiod_to_irq(priv->int0);
//		dev_dbg(dev, "IRQ configured\n");

		if (irq < 0) {
			dev_err(dev, "Configured 'int0' gpio cannot be used as IRQ: %d\n",
					irq);
			return irq;
		}

		INIT_DELAYED_WORK(&priv->npcm_clear_work,
				pcm9211_npcm_clear_work);
		ret = devm_request_threaded_irq(dev, irq, NULL,
				pcm9211_interrupt,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,					// 0x00000000 | 0x00002000
				"pcm9211", priv);
		if (ret) {
			dev_err(dev, "Failed to request irq: %d\n", ret);
			return ret;
		}

		/* Set interrupt to use positive polarity */
		ret = regmap_update_bits(priv->regmap, PCM9211_INT_POLARITY,				// 0x2e
				PCM9211_INT0_POLARITY_POS_MASK,						// BIT(2)
				PCM9211_INT0_POLARITY_POS_MASK);					// BIT(2)
		if (ret) {
			dev_err(dev, "Failed to configure int0 polarity: %d\n",
					ret);
			return ret;
		}
	}

	ret = pcm9211_write_pinconfig(dev);
	if (ret)
		return ret;

	/* Unmap NPCM, DTS, Burst Preamble and Fs change interrupt */
/*	ret = regmap_update_bits(priv->regmap, PCM9211_INT0_CAUSE,					// 0x2a
			PCM9211_INT0_MNPCM0_MASK | PCM9211_INT0_MDTSCD0_MASK |				// BIT(6) | BIT(4)
			PCM9211_INT0_MPCRNW0_MASK | PCM9211_INT0_MFSCHG0_MASK,				// BIT(2) | BIT(1)
			0);
	if (ret) {
		dev_err(dev, "Failed to unmask interrupt causes: %d\n", ret);
		return ret;
	}
*/
	/* Enable DTSCD detection */
	ret = regmap_update_bits(priv->regmap, PCM9211_NON_PCM_DEF,					// 0x28
			PCM9211_NON_PCM_DTS_CD_DET_MASK,						// BIT(2)
			PCM9211_NON_PCM_DTS_CD_DET_MASK);						// BIT(2)
	if (ret) {
		dev_err(dev, "Failed to enable DTSCD detection: %d\n", ret);
		return ret;
	}

	/* Read initial sampling rate and npcm state */
	priv->dir_rate = pcm9211_dir_rate(dev);
	ret = regmap_read(priv->regmap, PCM9211_INT0_OUT, &cause);					// 0x2c
	if (ret) {
		dev_err(dev, "Failed to read int0 cause: %d\n", ret);
		return IRQ_HANDLED;									// (1 << 0)
	}
	priv->npcm_state = cause;

//	ret = snd_soc_register_codec(dev, &pcm9211_driver, &pcm9211_dai, 1);
	ret = snd_soc_register_component(dev, &pcm9211_driver, &pcm9211_dai, 1);
	if (ret) {
		dev_err(dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(pcm9211_probe);

void pcm9211_remove(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

//	snd_soc_unregister_codec(dev);
	snd_soc_unregister_component(dev);
	pm_runtime_disable(dev);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
	clk_disable_unprepare(priv->xti);
}
EXPORT_SYMBOL_GPL(pcm9211_remove);

#ifdef CONFIG_PM
static int pcm9211_runtime_resume(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->xti);
	if (ret) {
		dev_err(dev, "Failed to enable xti clock: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		goto err_reg;
	}

	ret = pcm9211_reset(dev);
	if (ret) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	regcache_cache_only(priv->regmap, false);
	regcache_mark_dirty(priv->regmap);

	ret = regcache_sync(priv->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap: %d\n", ret);
		goto err;
	}

	return 0;

err:
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
err_reg:
	clk_disable_unprepare(priv->xti);

	return ret;
}

static int pcm9211_runtime_suspend(struct device *dev)
{
	struct pcm9211_priv *priv = dev_get_drvdata(dev);

	regcache_cache_only(priv->regmap, true);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
	clk_disable_unprepare(priv->xti);

	return 0;
}
#endif

const struct dev_pm_ops pcm9211_pm_ops = {
	SET_RUNTIME_PM_OPS(pcm9211_runtime_suspend, pcm9211_runtime_resume,
			NULL)
};
EXPORT_SYMBOL_GPL(pcm9211_pm_ops);

MODULE_DESCRIPTION("PCM9211 codec driver");
MODULE_AUTHOR("Julian Scheel <julian@jusst.de>");
MODULE_LICENSE("GPL v2");

