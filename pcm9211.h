/*
 * https://patchwork.kernel.org/project/alsa-devel/patch/20170523090931.9697-1-julian@jusst.de/
 * PCM9211 codec driver header
 *
 * Copyright (C) 2017 jusst technologies GmbH / jusst.engineering
 *
 * Author: Julian Scheel <julian@jusst.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef __PCM9211_H_
#define __PCM9211_H_

extern const struct dev_pm_ops pcm9211_pm_ops;
extern const struct regmap_config pcm9211_regmap;

extern int pcm9211_probe(struct device *dev, struct regmap *regmap);
extern void pcm9211_remove(struct device *dev);

/* Register definitions */
#define PCM9211_ERR_OUT			0x20
#define PCM9211_DIR_INITIAL1		0x21
#define PCM9211_DIR_INITIAL2		0x22
#define PCM9211_DIR_INITIAL3		0x23
#define PCM9211_OSC_CTRL		0x24
#define PCM9211_ERR_CAUSE		0x25
#define PCM9211_AUTO_SEL_CAUSE		0x26
#define PCM9211_DIR_FS_RANGE		0x27
#define PCM9211_NON_PCM_DEF		0x28
#define PCM9211_DTS_CD_LD		0x29
#define PCM9211_INT0_CAUSE		0x2a
#define PCM9211_INT1_CAUSE		0x2b
#define PCM9211_INT0_OUT		0x2c
#define PCM9211_INT1_OUT		0x2d
#define PCM9211_INT_POLARITY		0x2e
#define PCM9211_ADC_DET_LEVEL		0x2e
#define PCM9211_DIR_OUT_FMT		0x2f
#define PCM9211_DIR_RSCLK_RATIO		0x30
#define PCM9211_XTI_SCLK_FREQ		0x31
#define PCM9211_DIR_SOURCE_BIT2		0x32
#define PCM9211_XTI_SOURCE_BIT2		0x33
#define PCM9211_DIR_INP_BIPHASE		0x34
#define PCM9211_RECOUT0_BIPHASE		0x35
#define PCM9211_RECOUT1_BIPHASE		0x36
#define PCM9211_FS_CALC_TARGET		0x37
#define PCM9211_FS_CALC_RESULT		0x38
#define PCM9211_BIPHASE_INFO		0x39
#define PCM9211_PC_BUF0			0x3a
#define PCM9211_PC_BUF1			0x3b
#define PCM9211_PD_BUF0			0x3c
#define PCM9211_PD_BUF1			0x3d

#define PCM9211_SYS_RESET		0x40

#define PCM9211_ADC_CTRL1		0x42

#define PCM9211_ADC_L_CH_ATT		0x46
#define PCM9211_ADC_R_CH_ATT		0x47
#define PCM9211_ADC_CTRL2		0x48
#define PCM9211_ADC_CTRL3		0x49

#define PCM9211_DIR_STATUS1		0x5a
#define PCM9211_DIR_STATUS2		0x5b
#define PCM9211_DIR_STATUS3		0x5c
#define PCM9211_DIR_STATUS4		0x5d
#define PCM9211_DIR_STATUS5		0x5e
#define PCM9211_DIR_STATUS6		0x5f
#define PCM9211_DIT_CTRL1		0x60
#define PCM9211_DIT_CTRL2		0x61
#define PCM9211_DIT_CTRL3		0x62
#define PCM9211_DIT_STATUS1		0x63
#define PCM9211_DIT_STATUS2		0x64
#define PCM9211_DIT_STATUS3		0x65
#define PCM9211_DIT_STATUS4		0x66
#define PCM9211_DIT_STATUS5		0x67
#define PCM9211_DIT_STATUS6		0x68

#define PCM9211_MAIN_AUX_MUTE		0x6a
#define PCM9211_MAIN_OUT_SOURCE		0x6b
#define PCM9211_AUX_OUT_SOURCE		0x6c
#define PCM9211_MPIO_B_MAIN_HIZ		0x6d
#define PCM9211_MPIO_C_MPIO_A_HIZ	0x6e
#define PCM9211_MPIO_GROUP		0x6f
#define PCM9211_MPIO_A_FLAGS		0x70
#define PCM9211_MPIO_B_MPIO_C_FLAGS	0x71
#define PCM9211_MPIO_A1_A0_OUT_FLAG	0x72
#define PCM9211_MPIO_A3_A2_OUT_FLAG	0x73
#define PCM9211_MPIO_B1_B0_OUT_FLAG	0x74
#define PCM9211_MPIO_B3_B2_OUT_FLAG	0x75
#define PCM9211_MPIO_C1_C0_OUT_FLAG	0x76
#define PCM9211_MPIO_C3_C2_OUT_FLAG	0x77
#define PCM9211_MPO_1_0_FUNC		0x78
#define PCM9211_MPIO_A_B_DIR		0x79
#define PCM9211_MPIO_C_DIR		0x7a
#define PCM9211_MPIO_A_B_DATA_OUT	0x7b
#define PCM9211_MPIO_C_DATA_OUT		0x7c
#define PCM9211_MPIO_A_B_DATA_IN	0x7d
#define PCM9211_MPIO_C_DATA_IN		0x7e


/* Register field values (only used ones) */

/* PCM9211_ERR_OUT */
#define PCM9211_ERROR_INT0_MASK		BIT(2)
#define PCM9211_NPCM_INT1_MASK		BIT(0)

/* PCM9211_NON_PCM_DEF */
#define PCM9211_NON_PCM_DTS_CD_DET_MASK	BIT(2)

/* PCM9211_INT0_CAUSE */
#define PCM9211_INT0_MNPCM0_MASK	BIT(6)
#define PCM9211_INT0_MDTSCD0_MASK	BIT(4)
#define PCM9211_INT0_MPCRNW0_MASK	BIT(2)
#define PCM9211_INT0_MFSCHG0_MASK	BIT(1)

/* PCM9211_INT_POLARITY	*/
#define PCM9211_INT0_POLARITY_POS_MASK	BIT(2)
#define PCM9211_INT1_POLARITY_POS_MASK	BIT(6)

/* PCM9211_ADC_DET_LEVEL */
#define PCM9211_ADC_DET_LEVEL_SHIFT	3
#define PCM9211_ADC_DET_LEVEL_MASK	(0x3 << PCM9211_ADC_DET_LEVEL_SHIFT)

/* PCM9211_DIR_OUT_FMT */
#define PCM9211_DIR_FMT_MASK		0x7
#define PCM9211_DIR_FMT_SHIFT		0

#define PCM9211_DIR_FMT_I2S		4
#define PCM9211_DIR_FMT_LEFT_J		5
#define PCM9211_DIR_FMT_RIGHT_J		0

/* PCM9211_XTI_SCLK_FREQ */
#define PCM9211_XTI_XSCK_SHIFT		4
#define PCM9211_XTI_XSCK_MASK		(0x3 << PCM9211_XTI_XSCK_SHIFT)
#define PCM9211_XTI_BCK_SHIFT		2
#define PCM9211_XTI_BCK_MASK		(0x3 << PCM9211_XTI_BCK_SHIFT)
#define PCM9211_XTI_LRCK_SHIFT		0
#define PCM9211_XTI_LRCK_MASK		(0x3 << PCM9211_XTI_LRCK_SHIFT)


/* PCM9211_BIPHASE_INFO */
#define PCM9211_BIPHASE_SFSST_SHIFT	7
#define PCM9211_BIPHASE_SFSST_MASK	BIT(PCM9211_BIPHASE_SFSST_SHIFT)
#define PCM9211_BIPHASE_SFSOUT_SHIFT	0
#define PCM9211_BIPHASE_SFSOUT_MASK	(0xf << PCM9211_BIPHASE_SFSOUT_SHIFT)


/* PCM9211_SYS_RESET */
#define PCM9211_SYS_RESET_MRST		BIT(7)
#define PCM9211_SYS_RESET_RXDIS_SHIFT	4
#define PCM9211_SYS_RESET_ADDIS_SHIFT	5


/* PCM9211_ADC_CTRL2 */
#define PCM9211_ADFMT_MASK		0x3
#define PCM9211_ADFMT_SHIFT		0

#define PCM9211_ADFMT_I2S		0
#define PCM9211_ADFMT_LEFT_J		1
#define PCM9211_ADFMT_RIGHT_J		2


/* PCM9211_MAIN_OUT_SOURCE */
#define PCM9211_MOSSRC_SHIFT		4
#define PCM9211_MOPSRC_SHIFT		0
#define PCM9211_MOSRC_MASK		0x7
#define PCM9211_MOSSRC_MASK		\
	(PCM9211_MOSRC_MASK << PCM9211_MOSSRC_SHIFT)
#define PCM9211_MOPSRC_MASK		\
	(PCM9211_MOSRC_MASK << PCM9211_MOPSRC_SHIFT)

#define PCM9211_MOSRC_AUTO		0
#define PCM9211_MOSRC_DIR		1
#define PCM9211_MOSRC_ADC		2
#define PCM9211_MOSRC_AUXIN0		3
#define PCM9211_MOSRC_AUXIN1		4
#define PCM9211_MOSRC_AUXIN2		5


/* PCM9211_MPIO_GROUP */
#define PCM9211_MPASEL_SHIFT		6
//#define PCM9211_MPASEL_MASK		(0x2 << PCM9211_MPASEL_SHIFT)
#define PCM9211_MPASEL_MASK		0x03
#define PCM9211_MPBSEL_SHIFT		3
//#define PCM9211_MPBSEL_MASK		(0x7 << PCM9211_MPBSEL_SHIFT)
#define PCM9211_MPBSEL_MASK		0x07
#define PCM9211_MPCSEL_SHIFT		0
//#define PCM9211_MPCSEL_MASK		(0x7 << PCM9211_MPCSEL_SHIFT)
#define PCM9211_MPCSEL_MASK		0x07


/* PCM9211_MPIO_A_FLAGS */
#define PCM9211_MPAxSEL_SHIFT(x)	(x)
#define PCM9211_MPAxSEL_MASK(x)		BIT(PCM9211_MPAxSEL_SHIFT(x))


/* PCM9211_MPIO_B_MPIO_C_FLAGS */
#define PCM9211_MPBxSEL_SHIFT(x)	((x) + 4)
#define PCM9211_MPBxSEL_MASK(x)		BIT(PCM9211_MPBxSEL_SHIFT(x))
#define PCM9211_MPCxSEL_SHIFT(x)	(x)
#define PCM9211_MPCxSEL_MASK(x)		BIT(PCM9211_MPCxSEL_SHIFT(x))


/* PCM9211_MPIO_A1_A0_OUT_FLAG..PCM9211_MPIO_C3_C2_OUT_FLAG */
#define PCM9211_MPIO_ABCx_FLAG_SHIFT(x)	(((x) % 2) * 4)
#define PCM9211_MPIO_ABCx_FLAG_MASK(x)	(0xf << PCM9211_MPIO_ABCx_FLAG_SHIFT(x))


/* PCM9211_MPO_1_0_FUNC */
#define PCM9211_MPOxOUT_SHIFT(x)	((x) * 4)
#define PCM9211_MPOxOUT_MASK(x)		(0xf << PCM9211_MPOxOUT_SHIFT(x))

#endif
