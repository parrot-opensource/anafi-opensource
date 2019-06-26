/*
 * ak4954_amb.c  --  audio driver for AK4954
 *
 * Copyright 2014 Ambarella Ltd.
 *
 * Author: Diao Chengdong <cddiao@ambarella.com>
 *
 * History:
 *	2014/03/27 - created
 *	2015/12/23 - modified by XianqingZheng<xqzheng@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "ak4954_amb.h"

//#define PLL_32BICK_MODE
//#define PLL_64BICK_MODE

//#define AK4954_DEBUG			//used at debug mode
//#define AK4954_CONTIF_DEBUG		//used at debug mode

#ifdef AK4954_DEBUG
#define akdbgprt printk
#else
#define akdbgprt(format, arg...) do {} while (0)
#endif

#define LINEIN1_MIC_BIAS_CONNECT
#define LINEIN2_MIC_BIAS_CONNECT

/* AK4954 Codec Private Data */
struct ak4954_priv {
	unsigned int rst_pin;
	unsigned int rst_active;
	unsigned int sysclk;
	unsigned int clkid;
	u8 reg_cache[AK4954_MAX_REGISTERS];
	int onDrc;
	int onStereo;
	int mic;
};

/*
 * ak4951 register
 */
static struct reg_default  ak4954_reg_defaults[] = {
	{ 0,  0x00 }, { 1,  0x30 }, { 2,  0x0A }, { 3,  0x00 },
	{ 4,  0x34 }, { 5,  0x02 }, { 6,  0x09 }, { 7,  0x14 },
	{ 8,  0x00 }, { 9,  0x3A }, { 10, 0x48 }, { 11, 0x01 },
	{ 12, 0xE1 }, { 13, 0x91 }, { 14, 0x91 }, { 15, 0x00 },
	{ 16, 0x00 }, { 17, 0x00 }, { 18, 0x00 }, { 19, 0x0C },
	{ 20, 0x0C }, { 21, 0x00 }, { 22, 0x00 }, { 23, 0x00 },
	{ 24, 0x00 }, { 25, 0x00 }, { 26, 0x00 }, { 27, 0x01 },
	{ 28, 0x00 }, { 29, 0x03 }, { 30, 0xA9 }, { 31, 0x1F },
	{ 32, 0xAD }, { 33, 0x20 }, { 34, 0x7F }, { 35, 0x0C },
	{ 36, 0xFF }, { 37, 0x38 }, { 38, 0xA2 }, { 39, 0x83 },
	{ 40, 0x80 }, { 41, 0x2E }, { 42, 0x5B }, { 43, 0x23 },
	{ 44, 0x07 }, { 45, 0x28 }, { 46, 0xAA }, { 47, 0xEC },
	{ 48, 0x00 }, { 49, 0x00 }, { 50, 0x9F }, { 51, 0x00 },
	{ 52, 0x2B }, { 53, 0x3F }, { 54, 0xD4 }, { 55, 0xE0 },
	{ 56, 0x6A }, { 57, 0x00 }, { 58, 0x1B }, { 59, 0x3F },
	{ 60, 0xD4 }, { 61, 0xE0 }, { 62, 0x6A }, { 63, 0x00 },
	{ 64, 0xA2 }, { 65, 0x3E }, { 66, 0xD4 }, { 67, 0xE0 },
	{ 68, 0x6A }, { 69, 0x00 }, { 70, 0xA8 }, { 71, 0x38 },
	{ 72, 0xD4 }, { 73, 0xE0 }, { 74, 0x6A }, { 75, 0x00 },
	{ 76, 0x96 }, { 77, 0x1F }, { 78, 0xD4 }, { 79, 0xE0 },
	{ 80, 0x00 }, { 81, 0x00 }, { 82, 0x11 }, { 83, 0x90 },
	{ 84, 0x8A }, { 85, 0x07 }, { 86, 0x40 }, { 87, 0x07 },
	{ 88, 0x80 }, { 89, 0x2E }, { 90, 0xA9 }, { 91, 0x1F },
	{ 92, 0xAD }, { 93, 0x20 }, { 94, 0x00 }, { 95, 0x00 },
	{ 96, 0x00 }, { 97, 0x6F }, { 98, 0x18 }, { 99, 0x0C },
	{ 100, 0x10 },{ 101, 0x09 },{ 102, 0x08 },{ 103, 0x08 },
	{ 104, 0x7F },{ 105, 0x1D },{ 106, 0x03 },{ 107, 0x00 },
	{ 108, 0x18 },{ 109, 0x0C },{ 110, 0x10 },{ 111, 0x06 },
	{ 112, 0x08 },{ 113, 0x04 },{ 114, 0x7F },{ 115, 0x4E },
	{ 116, 0x0C },{ 117, 0x00 },{ 118, 0x1C },{ 119, 0x10 },
	{ 120, 0x10 },{ 121, 0x0C },{ 122, 0x08 },{ 123, 0x09 },
	{ 124, 0x7F },{ 125, 0x12 },{ 126, 0x07 },{ 127, 0x01 },
	{ 128, 0x50 },{ 129, 0x01 },{ 130, 0xA0 },{ 131, 0x22 },
	{ 132, 0xA9 },{ 133, 0x1F },{ 134, 0xAD },{ 135, 0x20 },
	{ 136, 0x04 },{ 137, 0x0A },{ 138, 0x07 },{ 139, 0x34 },
	{ 140, 0xE6 },{ 141, 0x1C },{ 142, 0x33 },{ 143, 0x26 },
};

static const struct {
	int readable;   /* Mask of readable bits */
	int writable;   /* Mask of writable bits */
} ak4954_access_masks[] = {
    { 0xEF, 0xEF },	//0x00
    { 0x3F, 0x3F },	//0x01
    { 0xBF, 0xBF },	//0x02
    { 0xCF, 0xCF },	//0x03
    { 0x3F, 0x3F },	//0x04
    { 0x7F, 0x7F },	//0x05
    { 0xCF, 0xCF },	//0x06
    { 0xF7, 0xB7 },	//0x07
    { 0x7B, 0x7B },	//0x08
    { 0xFF, 0xFF },	//0x09
    { 0xCF, 0xCF },	//0x0A
    { 0xBF, 0xBF },	//0x0B
    { 0xFF, 0xFF },	//0x0C
    { 0xFF, 0xFF },	//0x0D
    { 0xFF, 0xFF },	//0x0E
    { 0x00, 0x00 },	//0x0F
    { 0x00, 0x00 },	//0x10
    { 0x00, 0x00 },	//0x11
    { 0x80, 0x80 },	//0x12
    { 0xFF, 0xFF },	//0x13
    { 0xFF, 0xFF },	//0x14
    { 0x83, 0x83 },	//0x15
    { 0xFF, 0xFF },	//0x16
    { 0xFF, 0xFF },	//0x17
    { 0x7F, 0x7F },	//0x18
    { 0x9F, 0x9F },	//0x19
    { 0x00, 0x00 },	//0x1A
    { 0x0F, 0x0F },	//0x1B
    { 0xF3, 0xF3 },	//0x1C
    { 0x87, 0x87 },	//0x1D
    { 0xFF, 0xFF },	//0x1E
    { 0xFF, 0xFF },	//0x1F
    { 0xFF, 0xFF },	//0x20
    { 0xFF, 0xFF },	//0x21
    { 0xFF, 0xFF },	//0x22
    { 0xFF, 0xFF },	//0x23
    { 0xFF, 0xFF },	//0x24
    { 0xFF, 0xFF },	//0x25
    { 0xFF, 0xFF },	//0x26
    { 0xFF, 0xFF },	//0x27
    { 0xFF, 0xFF },	//0x28
    { 0xFF, 0xFF },	//0x29
    { 0xFF, 0xFF },	//0x2A
    { 0xFF, 0xFF },	//0x2B
    { 0xFF, 0xFF },	//0x2C
    { 0xFF, 0xFF },	//0x2D
    { 0xFF, 0xFF },	//0x2E
    { 0xFF, 0xFF },	//0x2F
    { 0x1F, 0x1F },	//0x30
    { 0x00, 0x00 },	//0x31
    { 0xFF, 0xFF },	//0x32
    { 0xFF, 0xFF },	//0x33
    { 0xFF, 0xFF },	//0x34
    { 0xFF, 0xFF },	//0x35
    { 0xFF, 0xFF },	//0x36
    { 0xFF, 0xFF },	//0x37
    { 0xFF, 0xFF },	//0x38
    { 0xFF, 0xFF },	//0x39
    { 0xFF, 0xFF },	//0x3A
    { 0xFF, 0xFF },	//0x3B
    { 0xFF, 0xFF },	//0x3C
    { 0xFF, 0xFF },	//0x3D
    { 0xFF, 0xFF },	//0x3E
    { 0xFF, 0xFF },	//0x3F
    { 0xFF, 0xFF },	//0x40
    { 0xFF, 0xFF },	//0x41
    { 0xFF, 0xFF },	//0x42
    { 0xFF, 0xFF },	//0x43
    { 0xFF, 0xFF },	//0x44
    { 0xFF, 0xFF },	//0x45
    { 0xFF, 0xFF },	//0x46
    { 0xFF, 0xFF },	//0x47
    { 0xFF, 0xFF },	//0x48
    { 0xFF, 0xFF },	//0x49
    { 0xFF, 0xFF },	//0x4A
    { 0xFF, 0xFF },	//0x4B
    { 0xFF, 0xFF },	//0x4C
    { 0xFF, 0xFF },	//0x4D
    { 0xFF, 0xFF },	//0x4E
    { 0xFF, 0xFF },	//0x4F
    { 0x7F, 0x7F },	//0x50
    { 0x37, 0x37 },	//0x51
    { 0x77, 0x77 },	//0x52
    { 0xDF, 0xDF },	//0x53
    { 0xDF, 0xDF },	//0x54
    { 0x0F, 0x0F },	//0x55
    { 0xFF, 0xFF },	//0x56
    { 0xFF, 0xFF },	//0x57
    { 0xFF, 0xFF },	//0x58
    { 0xFF, 0xFF },	//0x59
    { 0xFF, 0xFF },	//0x5A
    { 0xFF, 0xFF },	//0x5B
    { 0xFF, 0xFF },	//0x5C
    { 0xFF, 0xFF },	//0x5D
    { 0x00, 0x00 },	//0x5E
    { 0x00, 0x00 },	//0x5F
    { 0xFF, 0xFF },	//0x60
    { 0xFF, 0xFF },	//0x61
    { 0x7F, 0x7F },	//0x62
    { 0x7F, 0x7F },	//0x63
    { 0x7F, 0x7F },	//0x64
    { 0x7F, 0x7F },	//0x65
    { 0x7F, 0x7F },	//0x66
    { 0x7F, 0x7F },	//0x67
    { 0x7F, 0x7F },	//0x68
    { 0x7F, 0x7F },	//0x69
    { 0x7F, 0x7F },	//0x6A
    { 0x7F, 0x7F },	//0x6B
    { 0x7F, 0x7F },	//0x6C
    { 0x7F, 0x7F },	//0x6D
    { 0x7F, 0x7F },	//0x6E
    { 0x7F, 0x7F },	//0x6F
    { 0x7F, 0x7F },	//0x70
    { 0x7F, 0x7F },	//0x71
    { 0x7F, 0x7F },	//0x72
    { 0x7F, 0x7F },	//0x73
    { 0x7F, 0x7F },	//0x74
    { 0x7F, 0x7F },	//0x75
    { 0x7F, 0x7F },	//0x76
    { 0x7F, 0x7F },	//0x77
    { 0x7F, 0x7F },	//0x78
    { 0x7F, 0x7F },	//0x79
    { 0x7F, 0x7F },	//0x7A
    { 0x7F, 0x7F },	//0x7B
    { 0x7F, 0x7F },	//0x7C
    { 0x7F, 0x7F },	//0x7D
    { 0x7F, 0x7F },	//0x7E
    { 0x7F, 0x7F },	//0x7F
    { 0xFF, 0xFF },	//0x80
    { 0xFF, 0xFF },	//0x81
    { 0xFF, 0xFF },	//0x82
    { 0xFF, 0xFF },	//0x83
    { 0xFF, 0xFF },	//0x84
    { 0xFF, 0xFF },	//0x85
    { 0xFF, 0xFF },	//0x86
    { 0xFF, 0xFF },	//0x87
    { 0xFF, 0xFF },	//0x88
    { 0xFF, 0xFF },	//0x89
    { 0xFF, 0xFF },	//0x8A
    { 0xFF, 0xFF },	//0x8B
    { 0xFF, 0xFF },	//0x8C
    { 0xFF, 0xFF },	//0x8D
    { 0xFF, 0xFF },	//0x8E
    { 0xFF, 0xFF }	//0x8F

};
/*
 *  MIC Gain control:
 * from 6 to 26 dB in 6.5 dB steps
 */
static DECLARE_TLV_DB_SCALE(mgain_tlv, 600, 650, 0);

/*
 * Input Digital volume control:
 * from -54.375 to 36 dB in 0.375 dB steps mute instead of -54.375 dB)
 */
static DECLARE_TLV_DB_SCALE(ivol_tlv, -5437, 37, 1);

/*
 * Speaker output volume control:
 * from -33 to 12 dB in 3 dB steps (mute instead of -33 dB)
 */
static DECLARE_TLV_DB_SCALE(spkout_tlv, 426, 200, 0);

/*
 * Output Digital volume control: (DATT-A)
 * (This can be used as Bluetooth I/F output volume)
 * from -57.5 to 6 dB in 0.5 dB steps (mute instead of -57.5 dB)
 */
static DECLARE_TLV_DB_SCALE(dvol_tlv, -6600, 50, 1);


static const char *drc_on_select[]  =
{
	"DRC OFF",
	"DRC ON",
};

static const struct soc_enum ak4954_drc_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(drc_on_select), drc_on_select),
};
/*For our Board -- cddiao*/
static const char *mic_and_lin_select[]  =
{
	"LIN",
	"MIC",
};


static const struct soc_enum ak4954_micswitch_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mic_and_lin_select), mic_and_lin_select),
};


static int get_micstatus(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4954->mic;

    return 0;

}

static int set_micstatus(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

	ak4954->mic = ucontrol->value.enumerated.item[0];

	if ( ak4954->mic ) {
		snd_soc_update_bits(codec,AK4954_03_SIGNAL_SELECT2,0x0f,0x05);// LIN2 RIN2
	}else {
		snd_soc_update_bits(codec,AK4954_03_SIGNAL_SELECT2,0x0f,0x0a);// LIN3 RIN3
	}
    return 0;
}



static const char *stereo_on_select[]  =
{
	"Stereo Enphasis Filter OFF",
	"Stereo Enphasis Filter ON",
};

static const struct soc_enum ak4954_stereo_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(stereo_on_select), stereo_on_select),
};

static int ak4954_writeMask(struct snd_soc_codec *, u16, u16, u16);
static int get_ondrc(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4954->onDrc;

    return 0;

}

static int set_ondrc(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

	ak4954->onDrc = ucontrol->value.enumerated.item[0];

	if ( ak4954->onDrc ) {
		ak4954_writeMask(codec, AK4954_50_DRC_MODE_CONTROL, 0x3, 0x3);
		ak4954_writeMask(codec, AK4954_60_DVLC_FILTER_SELECT, 0, 0x55);
	}
	else {
		ak4954_writeMask(codec, AK4954_50_DRC_MODE_CONTROL, 0x3, 0x0);
		ak4954_writeMask(codec, AK4954_60_DVLC_FILTER_SELECT, 0, 0x0);
	}

    return 0;
}

static int get_onstereo(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4954->onStereo;

    return 0;

}

static int set_onstereo(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);

	ak4954->onStereo = ucontrol->value.enumerated.item[0];

	if ( ak4954->onStereo ) {
		ak4954_writeMask(codec, AK4954_1C_DIGITAL_FILTER_SELECT2, 0x30, 0x30);
	}
	else {
		ak4954_writeMask(codec, AK4954_1C_DIGITAL_FILTER_SELECT2, 0x30, 0x00);
	}

    return 0;
}

#ifdef AK4954_DEBUG

static const char *test_reg_select[]   =
{
    "read AK4954 Reg 00:2F",
    "read AK4954 Reg 30:5F",
    "read AK4954 Reg 60:8F",
};

static const struct soc_enum ak4954_enum[] =
{
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(test_reg_select), test_reg_select),
};

static int nTestRegNo = 0;

static int get_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    /* Get the current output routing */
    ucontrol->value.enumerated.item[0] = nTestRegNo;

    return 0;

}

static int set_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
    u32    currMode = ucontrol->value.enumerated.item[0];
	int    i, value;
	int	   regs, rege;

	nTestRegNo = currMode;

	switch(nTestRegNo) {
		case 1:
			regs = 0x30;
			rege = 0x5F;
			break;
		case 2:
			regs = 0x60;
			rege = 0x8F;
			break;
		default:
			regs = 0x00;
			rege = 0x2F;
			break;
	}

	for ( i = regs ; i <= rege ; i++ ){
		value = snd_soc_read(codec, i);
		printk("***AK4954 Addr,Reg=(%x, %x)\n", i, value);
	}

	return(0);

}

#endif

static const struct snd_kcontrol_new ak4954_snd_controls[] = {
	SOC_SINGLE_TLV("Mic Gain Control",
			AK4954_02_SIGNAL_SELECT1, 0, 0x07, 0, mgain_tlv),
	SOC_SINGLE_TLV("Input Digital Volume",
			AK4954_0D_LCH_INPUT_VOLUME_CONTROL, 0, 0xF1, 0, ivol_tlv),
	SOC_SINGLE_TLV("Speaker Output Volume",
			AK4954_03_SIGNAL_SELECT2, 6, 0x03, 0, spkout_tlv),
	SOC_SINGLE_TLV("Digital Output Volume",
			AK4954_13_LCH_DIGITAL_VOLUME_CONTROL, 0, 0x90, 1, dvol_tlv),

	SOC_SINGLE("High Path Filter 1", AK4954_1B_DIGITAL_FILTER_SELECT1, 1, 3, 0),
	SOC_SINGLE("High Path Filter 2", AK4954_1C_DIGITAL_FILTER_SELECT2, 0, 1, 0),
	SOC_SINGLE("Low Path Filter", 	 AK4954_1C_DIGITAL_FILTER_SELECT2, 1, 1, 0),
	SOC_SINGLE("5 Band Equalizer 1", AK4954_30_DIGITAL_FILTER_SELECT3, 0, 1, 0),
	SOC_SINGLE("5 Band Equalizer 2", AK4954_30_DIGITAL_FILTER_SELECT3, 1, 1, 0),
	SOC_SINGLE("5 Band Equalizer 3", AK4954_30_DIGITAL_FILTER_SELECT3, 2, 1, 0),
	SOC_SINGLE("5 Band Equalizer 4", AK4954_30_DIGITAL_FILTER_SELECT3, 3, 1, 0),
	SOC_SINGLE("5 Band Equalizer 5", AK4954_30_DIGITAL_FILTER_SELECT3, 4, 1, 0),
	SOC_SINGLE("Auto Level Control 1", AK4954_0B_ALC_MODE_CONTROL1, 5, 1, 0),
	SOC_SINGLE("Soft Mute Control", AK4954_07_MODE_CONTROL3, 5, 1, 0),
	SOC_ENUM_EXT("DRC Control", ak4954_drc_enum[0], get_ondrc, set_ondrc),
	SOC_ENUM_EXT("Stereo Enphasis Filter Control", ak4954_stereo_enum[0], get_onstereo, set_onstereo),
	SOC_ENUM_EXT("Mic and Lin Switch",ak4954_micswitch_enum[0],get_micstatus,set_micstatus),
#ifdef AK4954_DEBUG
	SOC_ENUM_EXT("Reg Read", ak4954_enum[0], get_test_reg, set_test_reg),
#endif


};

static const char *ak4954_lin_select_texts[] =
		{"LIN1", "LIN2", "LIN3"};

static const struct soc_enum ak4954_lin_mux_enum =
	SOC_ENUM_SINGLE(AK4954_03_SIGNAL_SELECT2, 2,
			ARRAY_SIZE(ak4954_lin_select_texts), ak4954_lin_select_texts);

static const struct snd_kcontrol_new ak4954_lin_mux_control =
	SOC_DAPM_ENUM("LIN Select", ak4954_lin_mux_enum);

static const char *ak4954_rin_select_texts[] =
		{"RIN1", "RIN2", "RIN3"};

static const struct soc_enum ak4954_rin_mux_enum =
	SOC_ENUM_SINGLE(AK4954_03_SIGNAL_SELECT2, 0,
			ARRAY_SIZE(ak4954_rin_select_texts), ak4954_rin_select_texts);

static const struct snd_kcontrol_new ak4954_rin_mux_control =
	SOC_DAPM_ENUM("RIN Select", ak4954_rin_mux_enum);

static const char *ak4954_lin1_select_texts[] =
		{"LIN1", "Mic Bias"};

static const struct soc_enum ak4954_lin1_mux_enum =
	SOC_ENUM_SINGLE(0, 0,
			ARRAY_SIZE(ak4954_lin1_select_texts), ak4954_lin1_select_texts);

static const struct snd_kcontrol_new ak4954_lin1_mux_control =
	SOC_DAPM_ENUM("LIN1 Switch", ak4954_lin1_mux_enum);


static const char *ak4954_micbias_select_texts[] =
		{"LIN1", "LIN2"};

static const struct soc_enum ak4954_micbias_mux_enum =
	SOC_ENUM_SINGLE(AK4954_02_SIGNAL_SELECT1, 4,
			ARRAY_SIZE(ak4954_micbias_select_texts), ak4954_micbias_select_texts);

static const struct snd_kcontrol_new ak4954_micbias_mux_control =
	SOC_DAPM_ENUM("MIC bias Select", ak4954_micbias_mux_enum);

static const char *ak4954_spklo_select_texts[] =
		{"Speaker", "Line"};

static const struct soc_enum ak4954_spklo_mux_enum =
	SOC_ENUM_SINGLE(AK4954_01_POWER_MANAGEMENT2, 0,
			ARRAY_SIZE(ak4954_spklo_select_texts), ak4954_spklo_select_texts);

static const struct snd_kcontrol_new ak4954_spklo_mux_control =
	SOC_DAPM_ENUM("SPKLO Select", ak4954_spklo_mux_enum);

static const char *ak4954_adcpf_select_texts[] =
		{"SDTI", "ADC"};

static const struct soc_enum ak4954_adcpf_mux_enum =
	SOC_ENUM_SINGLE(AK4954_1D_DIGITAL_FILTER_MODE, 1,
			ARRAY_SIZE(ak4954_adcpf_select_texts), ak4954_adcpf_select_texts);

static const struct snd_kcontrol_new ak4954_adcpf_mux_control =
	SOC_DAPM_ENUM("ADCPF Select", ak4954_adcpf_mux_enum);


static const char *ak4954_pfsdo_select_texts[] =
		{"ADC", "PFIL"};

static const struct soc_enum ak4954_pfsdo_mux_enum =
	SOC_ENUM_SINGLE(AK4954_1D_DIGITAL_FILTER_MODE, 0,
			ARRAY_SIZE(ak4954_pfsdo_select_texts), ak4954_pfsdo_select_texts);

static const struct snd_kcontrol_new ak4954_pfsdo_mux_control =
	SOC_DAPM_ENUM("PFSDO Select", ak4954_pfsdo_mux_enum);

static const char *ak4954_pfdac_select_texts[] =
		{"SDTI", "PFIL"};

static const struct soc_enum ak4954_pfdac_mux_enum =
	SOC_ENUM_SINGLE(AK4954_1D_DIGITAL_FILTER_MODE, 2,
			ARRAY_SIZE(ak4954_pfdac_select_texts), ak4954_pfdac_select_texts);

static const struct snd_kcontrol_new ak4954_pfdac_mux_control =
	SOC_DAPM_ENUM("PFDAC Select", ak4954_pfdac_mux_enum);

static const char *ak4954_dac_select_texts[] =
		{"PFDAC", "DRC"};

static const struct soc_enum ak4954_dac_mux_enum =
	SOC_ENUM_SINGLE(AK4954_1D_DIGITAL_FILTER_MODE, 7,
			ARRAY_SIZE(ak4954_dac_select_texts), ak4954_dac_select_texts);

static const struct snd_kcontrol_new ak4954_dac_mux_control =
	SOC_DAPM_ENUM("DAC Select", ak4954_dac_mux_enum);

static const char *ak4954_mic_select_texts[] =
		{"AMIC", "DMIC"};

static const struct soc_enum ak4954_mic_mux_enum =
	SOC_ENUM_SINGLE(AK4954_08_DIGITL_MIC, 0,
			ARRAY_SIZE(ak4954_mic_select_texts), ak4954_mic_select_texts);

static const struct snd_kcontrol_new ak4954_mic_mux_control =
	SOC_DAPM_ENUM("MIC Select", ak4954_mic_mux_enum);


static const struct snd_kcontrol_new ak4954_dacsl_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACSL", AK4954_02_SIGNAL_SELECT1, 5, 1, 0),
};

static int ak4954_spklo_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	u32 reg, nLOSEL;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	reg = snd_soc_read(codec, AK4954_01_POWER_MANAGEMENT2);
	nLOSEL = (0x1 & reg);

	switch (event) {
		case SND_SOC_DAPM_PRE_PMU:	/* before widget power up */
			break;
		case SND_SOC_DAPM_POST_PMU:	/* after widget power up */
			if ( nLOSEL ) {
				akdbgprt("\t[AK4954] %s wait=300msec\n",__FUNCTION__);
				mdelay(300);
			}
			else {
				akdbgprt("\t[AK4954] %s wait=1msec\n",__FUNCTION__);
				mdelay(1);
			}
			snd_soc_update_bits(codec, AK4954_02_SIGNAL_SELECT1, 0x80,0x80);
			break;
		case SND_SOC_DAPM_PRE_PMD:	/* before widget power down */
			snd_soc_update_bits(codec, AK4954_02_SIGNAL_SELECT1, 0x80,0x00);
			mdelay(1);
			break;
		case SND_SOC_DAPM_POST_PMD:	/* after widget power down */
			if ( nLOSEL ) {
				akdbgprt("\t[AK4954] %s wait=300msec\n",__FUNCTION__);
				mdelay(300);
			}
			break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget ak4954_dapm_widgets[] = {

// ADC, DAC
	SND_SOC_DAPM_ADC("ADC Left", "NULL", AK4954_00_POWER_MANAGEMENT1, 0, 0),
	SND_SOC_DAPM_ADC("ADC Right", "NULL", AK4954_00_POWER_MANAGEMENT1, 1, 0),
	SND_SOC_DAPM_DAC("DAC", "NULL", AK4954_00_POWER_MANAGEMENT1, 2, 0),

#ifdef PLL_32BICK_MODE
	SND_SOC_DAPM_SUPPLY("PMPLL", AK4954_01_POWER_MANAGEMENT2, 2, 0, NULL, 0),
#else
#ifdef PLL_64BICK_MODE
	SND_SOC_DAPM_SUPPLY("PMPLL", AK4954_01_POWER_MANAGEMENT2, 2, 0, NULL, 0),
#endif
#endif

	SND_SOC_DAPM_ADC("PFIL", "NULL", AK4954_00_POWER_MANAGEMENT1, 7, 0),
	SND_SOC_DAPM_DAC("DRC", "NULL", AK4954_1D_DIGITAL_FILTER_MODE, 7, 0),

	SND_SOC_DAPM_ADC("DMICL", "NULL", AK4954_08_DIGITL_MIC, 4, 0),
	SND_SOC_DAPM_ADC("DMICR", "NULL", AK4954_08_DIGITL_MIC, 5, 0),

	SND_SOC_DAPM_AIF_OUT("SDTO", "Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),

// Analog Output
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("SPKLO"),

	SND_SOC_DAPM_PGA("HPL Amp", AK4954_01_POWER_MANAGEMENT2, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR Amp", AK4954_01_POWER_MANAGEMENT2, 5, 0, NULL, 0),

	SND_SOC_DAPM_PGA("SPK Amp", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Line Amp", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER_E("SPKLO Mixer", AK4954_01_POWER_MANAGEMENT2, 1, 0,
			&ak4954_dacsl_mixer_controls[0], ARRAY_SIZE(ak4954_dacsl_mixer_controls),
			ak4954_spklo_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

// Analog Input
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("RIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),
	SND_SOC_DAPM_INPUT("RIN2"),
	SND_SOC_DAPM_INPUT("LIN3"),
	SND_SOC_DAPM_INPUT("RIN3"),

	SND_SOC_DAPM_MUX("LIN MUX", SND_SOC_NOPM, 0, 0,	&ak4954_lin_mux_control),
	SND_SOC_DAPM_MUX("RIN MUX", SND_SOC_NOPM, 0, 0,	&ak4954_rin_mux_control),

// MIC Bias
	SND_SOC_DAPM_MICBIAS("Mic Bias", AK4954_02_SIGNAL_SELECT1, 3, 0),
	SND_SOC_DAPM_MUX("LIN1 MUX", SND_SOC_NOPM, 0, 0, &ak4954_lin1_mux_control),
	SND_SOC_DAPM_MUX("Mic Bias MUX", SND_SOC_NOPM, 0, 0, &ak4954_micbias_mux_control),
	SND_SOC_DAPM_MUX("SPKLO MUX", SND_SOC_NOPM, 0, 0, &ak4954_spklo_mux_control),


// PFIL
	SND_SOC_DAPM_MUX("PFIL MUX", SND_SOC_NOPM, 0, 0, &ak4954_adcpf_mux_control),
	SND_SOC_DAPM_MUX("PFSDO MUX", SND_SOC_NOPM, 0, 0, &ak4954_pfsdo_mux_control),
	SND_SOC_DAPM_MUX("PFDAC MUX", SND_SOC_NOPM, 0, 0, &ak4954_pfdac_mux_control),
	SND_SOC_DAPM_MUX("DAC MUX", SND_SOC_NOPM, 0, 0, &ak4954_dac_mux_control),

// Digital Mic
	SND_SOC_DAPM_INPUT("DMICLIN"),
	SND_SOC_DAPM_INPUT("DMICRIN"),

	SND_SOC_DAPM_MUX("MIC MUX", SND_SOC_NOPM, 0, 0, &ak4954_mic_mux_control),

};


static const struct snd_soc_dapm_route ak4954_intercon[] = {

#ifdef PLL_32BICK_MODE
	{"ADC Left", NULL, "PMPLL"},
	{"ADC Right", NULL, "PMPLL"},
	{"DAC", NULL, "PMPLL"},
#else
#ifdef PLL_64BICK_MODE
	{"ADC Left", NULL, "PMPLL"},
	{"ADC Right", NULL, "PMPLL"},
	{"DAC", NULL, "PMPLL"},
#endif
#endif

	{"Mic Bias MUX", "LIN1", "LIN1"},
	{"Mic Bias MUX", "LIN2", "LIN2"},

	{"Mic Bias", NULL, "Mic Bias MUX"},

	{"LIN1 MUX", "LIN1", "LIN1"},
	{"LIN1 MUX", "Mic Bias", "Mic Bias"},

	{"LIN MUX", "LIN1", "LIN1 MUX"},
	{"LIN MUX", "LIN2", "Mic Bias"},
	{"LIN MUX", "LIN3", "LIN3"},
	{"RIN MUX", "RIN1", "RIN1"},
	{"RIN MUX", "RIN2", "RIN2"},
	{"RIN MUX", "RIN3", "RIN3"},
	{"ADC Left", NULL, "LIN MUX"},
	{"ADC Right", NULL, "RIN MUX"},

	{"DMICL", NULL, "DMICLIN"},
	{"DMICR", NULL, "DMICRIN"},

	{"MIC MUX", "AMIC", "ADC Left"},
	{"MIC MUX", "AMIC", "ADC Right"},
	{"MIC MUX", "DMIC", "DMICL"},
	{"MIC MUX", "DMIC", "DMICR"},

	{"PFIL MUX", "SDTI", "SDTI"},
	{"PFIL MUX", "ADC", "MIC MUX"},
	{"PFIL", NULL, "PFIL MUX"},

	{"PFSDO MUX", "ADC", "MIC MUX"},
	{"PFSDO MUX", "PFIL", "PFIL"},

	{"SDTO", NULL, "PFSDO MUX"},

	{"PFDAC MUX", "SDTI", "SDTI"},
	{"PFDAC MUX", "PFIL", "PFIL"},

	{"DAC MUX", "PFDAC", "PFDAC MUX"},
	{"DRC", NULL, "PFDAC MUX"},
	{"DAC MUX", "DRC", "DRC"},

	{"DAC", NULL, "DAC MUX"},

	{"HPL Amp", NULL, "DAC"},
	{"HPR Amp", NULL, "DAC"},
	{"HPL", NULL, "HPL Amp"},
	{"HPR", NULL, "HPR Amp"},

	{"SPKLO Mixer", "DACSL", "DAC"},
	{"SPK AmNULLp", NULL, "SPKLO Mixer"},
	{"Line Amp", NULL, "SPKLO Mixer"},
	{"SPKLO MUX", "Speaker", "SPK Amp"},
	{"SPKLO MUX", "Line", "Line Amp"},
	{"SPKLO", NULL, "SPKLO MUX"},

};

static int ak4954_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int rate = params_rate(params);
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);
	int oversample = 0;
	u8 	fs = 0;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	oversample = ak4954->sysclk / rate;
	switch (oversample) {
	case 256:
		fs &= (~AK4954_FS_CM1);
		fs &= (~AK4954_FS_CM0);
		break;
	case 384:
		fs &= (~AK4954_FS_CM1);
		fs |= AK4954_FS_CM0;
		break;
	case 512:
		fs |= AK4954_FS_CM1;
		fs &= (~AK4954_FS_CM0);
		break;
	case 1024:
		fs |= AK4954_FS_CM1;
		fs |= AK4954_FS_CM0;
		break;
	default:
		break;
	}
	switch (rate) {
	case 8000:
		fs |= AK4954_FS_8KHZ;
		break;
	case 11025:
		fs |= AK4954_FS_11_025KHZ;
		break;
	case 12000:
		fs |= AK4954_FS_12KHZ;
		break;
	case 16000:
		fs |= AK4954_FS_16KHZ;
		break;
	case 22050:
		fs |= AK4954_FS_22_05KHZ;
		break;
	case 24000:
		fs |= AK4954_FS_24KHZ;
		break;
	case 32000:
		fs |= AK4954_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4954_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4954_FS_48KHZ;
		break;
	case 64000:
		fs |= AK4954_FS_64KHZ;
		break;
	case 88000:
		fs |= AK4954_FS_88_2KHZ;
		break;
	case 96000:
		fs |= AK4954_FS_96KHZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, AK4954_06_MODE_CONTROL2, fs);

	return 0;
}

static int ak4954_set_pll(u8 *pll, int clk_id,int freq)
{
	if (clk_id == AK4954_MCLK_IN_BCLK_OUT){
		switch (freq) {
		case 11289600:
			*pll |= (2 << 4);
			break;
		case 12288000:
			*pll |= (3 << 4);
			break;
		case 12000000:
			*pll |= (4 << 4);
			break;
		case 24000000:
			*pll |= (5 << 4);
			break;
		case 13500000:
			*pll |= (6 << 4);
			break;
		case 27000000:
			*pll |= (7 << 4);
			break;
		default:
			break;
		}
	}else if  (clk_id == AK4954_BCLK_IN) {
		*pll |= (0 << 4);
	}
	return 0;
}

static int ak4954_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);
	u8 pllpwr = 0, pll = 0;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	pll= snd_soc_read(codec, AK4954_05_MODE_CONTROL1);
	pll &=(~0x70);
	pllpwr = snd_soc_read(codec,AK4954_01_POWER_MANAGEMENT2);
	pllpwr &=(~0x0c);

	if (clk_id == AK4954_MCLK_IN) {
		pllpwr &= (~AK4954_PMPLL);
		pllpwr &= (~AK4954_M_S);
	}else if (clk_id == AK4954_BCLK_IN) {
		pllpwr |= AK4954_PMPLL;
		pllpwr &= (~AK4954_M_S);
		ak4954_set_pll(&pll, clk_id, freq);
	}else if (clk_id == AK4954_MCLK_IN_BCLK_OUT) {
		pllpwr |= AK4954_PMPLL;
		pllpwr |= AK4954_M_S;
		ak4954_set_pll(&pll, clk_id, freq);
	}
	snd_soc_write(codec, AK4954_05_MODE_CONTROL1, pll);
	snd_soc_write(codec, AK4954_01_POWER_MANAGEMENT2, pllpwr);

	ak4954->sysclk = freq;
	ak4954->clkid = clk_id;
	return 0;
}

static int ak4954_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{

	struct snd_soc_codec *codec = dai->codec;
	u8 mode;
	u8 format;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	/* set master/slave audio interface */
	mode = snd_soc_read(codec, AK4954_01_POWER_MANAGEMENT2);
	format = snd_soc_read(codec, AK4954_05_MODE_CONTROL1);
	format &= ~AK4954_DIF;

    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
        case SND_SOC_DAIFMT_CBS_CFS:
			akdbgprt("\t[AK4954] %s(Slave)\n",__FUNCTION__);
            mode &= ~(AK4954_M_S);
            //format &= ~(AK4954_BCKO);
            break;
        case SND_SOC_DAIFMT_CBM_CFM:
			akdbgprt("\t[AK4954] %s(Master)\n",__FUNCTION__);
            mode |= (AK4954_M_S);
            //format |= (AK4954_BCKO);
            break;
        case SND_SOC_DAIFMT_CBS_CFM:
        case SND_SOC_DAIFMT_CBM_CFS:
        default:
            dev_err(codec->dev, "Clock mode unsupported");
           return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			format |= AK4954_DIF_24_16_I2S_MODE;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			format |= AK4954_DIF_24MSB_MODE;
			break;
		default:
			return -EINVAL;
	}

	/* set mode and format */

	snd_soc_write(codec, AK4954_01_POWER_MANAGEMENT2, mode);
	snd_soc_write(codec, AK4954_05_MODE_CONTROL1, format);

	return 0;
}

/*
 * Write with Mask to  AK4954 register space
 */
static int ak4954_writeMask(
struct snd_soc_codec *codec,
u16 reg,
u16 mask,
u16 value)
{
    u16 olddata;
    u16 newdata;

	if ( (mask == 0) || (mask == 0xFF) ) {
		newdata = value;
	}
	else {
		olddata = snd_soc_read(codec, reg);
	    newdata = (olddata & ~(mask)) | value;
	}

	snd_soc_write(codec, (unsigned int)reg, (unsigned int)newdata);

	return(0);
}

// * for AK4954
static int ak4954_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *codec_dai)
{
	int 	ret = 0;
 //   struct snd_soc_codec *codec = codec_dai->codec;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	return ret;
}


static int ak4954_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	u8 reg;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		reg = snd_soc_read(codec, AK4954_00_POWER_MANAGEMENT1);	// * for AK4954
		snd_soc_write(codec, AK4954_00_POWER_MANAGEMENT1,			// * for AK4954
				reg | AK4954_PMVCM);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, AK4954_00_POWER_MANAGEMENT1, 0x00);	// * for AK4954
		break;
	}

	return 0;
}

#define AK4954_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			    SNDRV_PCM_RATE_96000)

#define AK4954_FORMATS		SNDRV_PCM_FMTBIT_S16_LE


static struct snd_soc_dai_ops ak4954_dai_ops = {
	.hw_params	= ak4954_hw_params,
	.set_sysclk	= ak4954_set_dai_sysclk,
	.set_fmt	= ak4954_set_dai_fmt,
	.trigger = ak4954_trigger,
};

struct snd_soc_dai_driver ak4954_dai[] = {
	{
		.name = "ak4954-hifi",
		.playback = {
		       .stream_name = "Playback",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4954_RATES,
		       .formats = AK4954_FORMATS,
		},
		.capture = {
		       .stream_name = "Capture",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4954_RATES,
		       .formats = AK4954_FORMATS,
		},
		.ops = &ak4954_dai_ops,
	},
};

static int ak4954_probe(struct snd_soc_codec *codec)
{
	struct ak4954_priv *ak4954 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	snd_soc_write(codec, AK4954_00_POWER_MANAGEMENT1, 0x00);
	snd_soc_write(codec, AK4954_00_POWER_MANAGEMENT1, 0x00);

	ak4954_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
//	ak4954_set_reg_digital_effect(codec);

	ak4954->onDrc = 0;
	ak4954->onStereo = 0;
	ak4954->mic = 1;
	/*Enable Line out */
	snd_soc_update_bits(codec,AK4954_01_POWER_MANAGEMENT2,0x02,0x02);
	snd_soc_update_bits(codec,AK4954_02_SIGNAL_SELECT1, 0xa0,0xa0);

	/*Enable LIN2*/
	snd_soc_update_bits(codec,AK4954_02_SIGNAL_SELECT1,0x18,0x08);//MPWR1
	snd_soc_update_bits(codec,AK4954_03_SIGNAL_SELECT2,0x0f,0x05);// LIN2 RIN2
	snd_soc_update_bits(codec,AK4954_08_DIGITL_MIC,0x01,0x00);//AMIC
	snd_soc_update_bits(codec,AK4954_1D_DIGITAL_FILTER_MODE,0x02,0x02);//ADC output
	snd_soc_update_bits(codec,AK4954_1D_DIGITAL_FILTER_MODE,0x01,0x01);//ALC output
	snd_soc_update_bits(codec,AK4954_02_SIGNAL_SELECT1,0x07,0x3);//Mic Gain
	snd_soc_update_bits(codec,AK4954_0D_LCH_INPUT_VOLUME_CONTROL,0xff,0xb0);//Lch gain
	snd_soc_update_bits(codec,AK4954_0E_RCH_INPUT_VOLUME_CONTROL,0xff,0xb0);//Lch gain

	/*Enable LIN3*/
	//snd_soc_update_bits(codec,AK4954_03_SIGNAL_SELECT2,0x0f,0x0a);// LIN3 RIN3

    return ret;

}

static int ak4954_remove(struct snd_soc_codec *codec)
{

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4954_set_bias_level(codec, SND_SOC_BIAS_OFF);


	return 0;
}

static int ak4954_suspend(struct snd_soc_codec *codec)
{
	ak4954_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int ak4954_resume(struct snd_soc_codec *codec)
{

	ak4954_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}


struct snd_soc_codec_driver soc_codec_dev_ak4954 = {
	.probe			= ak4954_probe,
	.remove			= ak4954_remove,
	.suspend		= ak4954_suspend,
	.resume			= ak4954_resume,
	.set_bias_level		= ak4954_set_bias_level,
	.component_driver = {
		.controls		= ak4954_snd_controls,
		.num_controls		= ARRAY_SIZE(ak4954_snd_controls),
		.dapm_widgets		= ak4954_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(ak4954_dapm_widgets),
		.dapm_routes		= ak4954_intercon,
		.num_dapm_routes	= ARRAY_SIZE(ak4954_intercon),
	},
};

static struct regmap_config ak4954_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= AK4954_MAX_REGISTERS,
	.reg_defaults		= ak4954_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ak4954_reg_defaults),
	.cache_type		= REGCACHE_RBTREE,
};

EXPORT_SYMBOL_GPL(soc_codec_dev_ak4954);

static int ak4954_i2c_probe(struct i2c_client *i2c,
                            const struct i2c_device_id *id)
{
	struct device_node *np = i2c->dev.of_node;
	struct ak4954_priv *ak4954;
	struct regmap *regmap;
	enum of_gpio_flags flags;
	int rst_pin, ret=0;

	akdbgprt("\t[AK4954] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4954 = devm_kzalloc(&i2c->dev, sizeof(struct ak4954_priv), GFP_KERNEL);
	if (ak4954 == NULL)
		return -ENOMEM;

	rst_pin = of_get_gpio_flags(np, 0, &flags);
	if (rst_pin < 0 || !gpio_is_valid(rst_pin))
		return -ENXIO;

	ak4954->rst_pin = rst_pin;
	ak4954->rst_active = !!(flags & OF_GPIO_ACTIVE_LOW);

	i2c_set_clientdata(i2c, ak4954);
	regmap = devm_regmap_init_i2c(i2c, &ak4954_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap_init() for ak1954 failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ak4954, &ak4954_dai[0], ARRAY_SIZE(ak4954_dai));
	if (ret < 0){
		kfree(ak4954);
		akdbgprt("\t[AK4954 Error!] %s(%d)\n",__FUNCTION__,__LINE__);
	}
	return ret;
}

static int ak4954_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static struct of_device_id ak4954_of_match[] = {
	{ .compatible = "ambarella,ak4954",},
	{},
};
MODULE_DEVICE_TABLE(of, ak4954_of_match);

static const struct i2c_device_id ak4954_i2c_id[] = {
	{ "ak4954", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4954_i2c_id);

static struct i2c_driver ak4954_i2c_driver = {
	.driver = {
		.name = "ak4954-codec",
		.owner = THIS_MODULE,
		.of_match_table = ak4954_of_match,
	},
	.probe		=	ak4954_i2c_probe,
	.remove		=	ak4954_i2c_remove,
	.id_table	=	ak4954_i2c_id,
};

static int __init ak4954_modinit(void)
{
	int ret;
	akdbgprt("\t[AK4954] %s(%d)\n", __FUNCTION__,__LINE__);

	ret = i2c_add_driver(&ak4954_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register ak4954 I2C driver: %d\n",ret);
	return ret;
}

module_init(ak4954_modinit);

static void __exit ak4954_exit(void)
{
	i2c_del_driver(&ak4954_i2c_driver);
}
module_exit(ak4954_exit);

MODULE_DESCRIPTION("Soc AK4954 driver");
MODULE_AUTHOR("Diao Chengdong<cddiao@ambarella.com>");
MODULE_LICENSE("GPL");

