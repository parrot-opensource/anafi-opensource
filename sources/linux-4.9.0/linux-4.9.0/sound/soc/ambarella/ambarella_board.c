/*
 * sound/soc/ambarella_board.c
 *
 * Author: XianqingZheng <xqzheng@ambarella.com>
 *
 * History:
 *	2015/04/28 - [XianqingZheng] Created file
 *	2016/07/12 - [XianqingZheng] rewrite the file
 *
 * Copyright (C) 2014-2018, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <plat/audio.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>

/* clk_fmt :
* 0 : mclk, bclk provided by cpu
* 1 : bclk provide by cpu, mclk is not used
* 2 : mclk provided by cpu, bclk is provided by codec
* There are four type of connection:
* clk_fmt=0:
* cpu :                   codec:
*     MCLK    ------>  MCKI
*     BCLK    ------>  BICK
*     LRCK    ------>  LRCK
*                   ...
* clk_fmt=1:
* cpu :                   codec:
*     MCLK   is not used
*     BCLK    ------> BICK
*     LRCK    ------> LRCK
*                    ...
* clk_fmt=2:
* cpu :                   codec:
*     MCLK   ------> MCKI
*     BCLK   <------ BICK
*     LRCK   <------ LRCK
* There are one connection we are not used, it is like clk_fmt=0,but power on the codec PLL.
* It is a waster of power, so we do not use it.
*/
struct amb_snd_fmt {
	u32	dai_fmt;
	u32	clk_fmt;
	bool	dmic;
};

struct amb_snd_fmt *amb_fmt = NULL;

struct amb_clk {
	int mclk;
	int i2s_div;
	int bclk;
};

static int amba_clk_config(struct snd_pcm_hw_params *params, struct amb_clk *clk, bool dmic)
{
	u32 channels, sample_bits, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		sample_bits = 32;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		sample_bits = 16;
		break;
	}

	channels = params_channels(params);
	rate = params_rate(params);

	if(dmic)
		clk->mclk = 1024 * rate;
	else
		clk->mclk = 12288000;

	clk->bclk = channels * rate * sample_bits;

	clk->i2s_div = (clk->mclk / ( 2 * clk->bclk)) - 1;

	return 0;
}

static int amba_general_board_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct amb_clk clk={0};
	int i2s_mode, i = dai_link->id, rval = 0;
	bool dmic = amb_fmt[i].dmic;

	rval = amba_clk_config(params, &clk, dmic);
	if (rval < 0) {
		pr_err("amba can not support the sample rate\n");
		goto hw_params_exit;
	}

	if (amb_fmt[i].dai_fmt == 0)
		i2s_mode = SND_SOC_DAIFMT_I2S;
	else
		i2s_mode = SND_SOC_DAIFMT_DSP_A;

	/* set the I2S system data format*/
	if (amb_fmt[i].clk_fmt == 2) {
		rval = snd_soc_dai_set_fmt(codec_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		if (rval < 0) {
			pr_err("can't set codec DAI configuration\n");
			goto hw_params_exit;
		}

		rval = snd_soc_dai_set_fmt(cpu_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		if (rval < 0) {
			pr_err("can't set cpu DAI configuration\n");
			goto hw_params_exit;
		}
	} else {

		rval = snd_soc_dai_set_fmt(codec_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (rval < 0) {
			pr_err("can't set codec DAI configuration\n");
			goto hw_params_exit;
		}

		rval = snd_soc_dai_set_fmt(cpu_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (rval < 0) {
			pr_err("can't set cpu DAI configuration\n");
			goto hw_params_exit;
		}
	}

	/* set the I2S system clock*/
	switch(amb_fmt[i].clk_fmt) {
	case 0:
		rval = snd_soc_dai_set_sysclk(codec_dai, amb_fmt[i].clk_fmt, clk.mclk, 0);
		break;
	case 1:
		rval = snd_soc_dai_set_sysclk(codec_dai, amb_fmt[i].clk_fmt, clk.bclk, 0);
		break;
	case 2:
		rval = snd_soc_dai_set_sysclk(codec_dai, amb_fmt[i].clk_fmt, clk.mclk, 0);
		break;
	default:
		pr_err("clk_fmt is wrong, just 0, 1, 2 is available!\n");
		goto hw_params_exit;
	}

	if (rval < 0) {
		pr_err("can't set codec MCLK configuration\n");
		goto hw_params_exit;
	}


	rval = snd_soc_dai_set_sysclk(cpu_dai, AMBARELLA_CLKSRC_ONCHIP, clk.mclk, 0);
	if (rval < 0) {
		pr_err("can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	rval = snd_soc_dai_set_clkdiv(cpu_dai, 0, clk.i2s_div);
	if (rval < 0) {
		pr_err("can't set cpu MCLK/SF ratio\n");
		goto hw_params_exit;
	}

hw_params_exit:
	return rval;
}

static struct snd_soc_ops amba_general_board_ops = {
	.hw_params = amba_general_board_hw_params,
};

/* ambevk machine dapm widgets */
static const struct snd_soc_dapm_widget amba_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic internal", NULL),
	SND_SOC_DAPM_MIC("Mic external", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_HP("HP Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static int amba_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

/* ambevk audio machine driver */
static struct snd_soc_card snd_soc_card_amba = {
	.owner = THIS_MODULE,
	.dapm_widgets = amba_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(amba_dapm_widgets),
};

static int amba_soc_snd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpup_np, *codec_np;
	const __be32 *prop;
	struct snd_soc_dai_link *amba_dai_link;
	struct snd_soc_card *card = &snd_soc_card_amba;
	int rval = 0, i, psize;

	card->dev = &pdev->dev;

	if (snd_soc_of_parse_card_name(card, "amb,model")) {
		dev_err(&pdev->dev, "Card name is not provided\n");
		return -ENODEV;
	}

	cpup_np = of_parse_phandle(np, "amb,i2s-controllers", 0);
	if (!cpup_np) {
		dev_err(&pdev->dev, "phandle missing or invalid for amb,i2s-controllers\n");
		return -EINVAL;
	}

	prop = of_get_property(np, "amb,audio-codec", &psize);
	if (!prop) {
		dev_err(&pdev->dev, "phandle missing or invalid for amb,audio-codec\n");
		return -EINVAL;
	}

	psize /= sizeof(u32);
	amba_dai_link = devm_kzalloc(&pdev->dev, psize * sizeof(*amba_dai_link), GFP_KERNEL);
	if (amba_dai_link == NULL) {
		dev_err(&pdev->dev, "alloc memory for amba_dai_link fail!\n");
		return -ENOMEM;
	}

	amb_fmt =  devm_kzalloc(&pdev->dev, psize * sizeof(struct amb_snd_fmt), GFP_KERNEL);
	if (amb_fmt == NULL) {
		dev_err(&pdev->dev, "alloc memory for amb_fmt fail!\n");
		return -ENOMEM;
	}

	for(i = 0; i < psize; i++) {
		codec_np = of_parse_phandle(np, "amb,audio-codec", i);
		amba_dai_link[i].codec_of_node = codec_np;
		amba_dai_link[i].cpu_of_node = cpup_np;
		amba_dai_link[i].platform_of_node = cpup_np;
		amba_dai_link[i].init = amba_codec_init;
		amba_dai_link[i].ops = &amba_general_board_ops;
		amba_dai_link[i].id = i;
		card->dai_link = amba_dai_link;
		card->num_links = psize;

		/*Get fmt information from dts*/
		rval = of_property_read_u32_index(np, "amb,dai_fmt", i,
				&amb_fmt[i].dai_fmt);
		if(rval < 0) {
			dev_err(&pdev->dev, "get dai_fmt from dts for codec[%d] fail\n", i);
			return -EINVAL;
		}

		rval = of_property_read_u32_index(np, "amb,clk_fmt", i,
				&amb_fmt[i].clk_fmt);
		if(rval < 0) {
			dev_err(&pdev->dev, "get clk_fmt from dts for codec[%d] fail\n", i);
			return -EINVAL;
		}

		/*Get codec information from dts*/
		rval = of_property_read_string_index(np, "amb,codec-name", i,
				&card->dai_link[i].name);
		if(rval < 0) {
			dev_err(&pdev->dev, "can not get codec-name from dts\n");
			return -EINVAL;
		}

		rval = of_property_read_string_index(np, "amb,stream-name", i,
				&card->dai_link[i].stream_name);
		if(rval < 0) {
			dev_err(&pdev->dev, "can not get stream-name from dts\n");
			return -EINVAL;
		}

		rval = of_property_read_string_index(np, "amb,codec-dai-name", i,
				&card->dai_link[i].codec_dai_name);
		if(rval < 0) {
			dev_err(&pdev->dev, "can not get codec-dai-name from dts\n");
			return -EINVAL;
		}

		if(strcmp(card->dai_link[i].codec_dai_name, "dmic-hifi") == 0) {
			amb_fmt[i].dmic = true;
		} else {
			amb_fmt[i].dmic = false;
		}

		of_node_put(codec_np);
		codec_np = NULL;
	}

	of_node_put(cpup_np);
	if(psize >= 2) {
		rval = snd_soc_of_parse_audio_routing(card,
		"amb,audio-routing");
		if(rval) {
			dev_err(&pdev->dev, "amb,audio-routing is invalid\n");
			return -EINVAL;
		}
	}

	rval = snd_soc_register_card(card);
	if (rval)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", rval);


	return rval;
}

static int amba_soc_snd_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&snd_soc_card_amba);
	if(snd_soc_card_amba.dapm_routes != NULL)
		kfree(snd_soc_card_amba.dapm_routes);

	return 0;
}

static const struct of_device_id amba_dt_ids[] = {
	{ .compatible = "ambarella,audio-board", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, amba_dt_ids);

static struct platform_driver amba_soc_snd_driver = {
	.driver = {
		.name = "snd_soc_card_amba",
		.pm = &snd_soc_pm_ops,
		.of_match_table = amba_dt_ids,
	},
	.probe = amba_soc_snd_probe,
	.remove = amba_soc_snd_remove,
};

module_platform_driver(amba_soc_snd_driver);

MODULE_AUTHOR("XianqingZheng<xqzheng@ambarella.com>");
MODULE_DESCRIPTION("Amabrella Board for ALSA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("snd-soc-amba");

