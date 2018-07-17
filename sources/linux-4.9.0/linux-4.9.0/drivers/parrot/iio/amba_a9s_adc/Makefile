obj-$(CONFIG_IIO_AMBA_A9S_ADC) += amba_a9s_adc_mod.o
amba_a9s_adc_mod-y := amba_a9s_adc.o
# only one of these two should be enabled at the same time
amba_a9s_adc_mod-$(CONFIG_IIO_AMBA_A9S_ADC_RPMSG) += amba_a9s_adc_rpmsg.o
amba_a9s_adc_mod-$(CONFIG_IIO_AMBA_A9S_ADC_LOCAL) += amba_a9s_adc_local.o
amba_a9s_adc_mod-y += amba_a9s_adc_buffer.o


