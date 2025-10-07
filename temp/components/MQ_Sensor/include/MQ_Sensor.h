#include <stdio.h>
#include <driver/gpio.h>
#include <driver/adc.h>

static const gpio_num_t ADC_SMK_PORT = GPIO_NUM_32;

esp_err_t init_smk();

int detect_smk();