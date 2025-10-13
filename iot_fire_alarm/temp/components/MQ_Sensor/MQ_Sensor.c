#include <stdio.h>
#include "MQ_Sensor.h"

adc1_channel_t adc_smk;

esp_err_t init_smk()
{
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_width(ADC_WIDTH_BIT_10);

    return ESP_OK;
}

int detect_smk()
{
    int output = adc1_get_raw(adc_smk);
    if(output > 500)
        return 1;
    else
        return 0;
}
