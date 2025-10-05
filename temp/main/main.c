#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../components/dht/include/dht.h"
#include <driver/gpio.h>
#include <driver/adc.h>
#include <freertos/FreeRTOSConfig.h>
#include <esp_log.h>

#define SENSOR_TYPE DHT_TYPE_DHT11
adc1_channel_t adc;
gpio_num_t ADC_PORT = GPIO_NUM_34;
#define TAG "Sensor de humo" 

esp_err_t init_adc()
{
    
    adc1_pad_get_io_num(adc, &ADC_PORT);
    adc1_config_channel_atten(adc, ADC_ATTEN_DB_12);
    adc1_config_width(ADC_WIDTH_BIT_10);

    return ESP_OK;
}

void dht_test(void *pvParameters)
{
    float temperature, humidity;
    init_adc();

#ifdef CONFIG_EXAMPLE_INTERNAL_PULLUP
    gpio_set_pull_mode(dht_gpio, GPIO_PULLUP_ONLY);
#endif

    while (1)
    {
            int output = adc1_get_raw(adc);
            dht_read_float_data(SENSOR_TYPE, 32, &humidity, &temperature);
            printf("Humidity: %.1f%% Temp: %.1fC\n", humidity, temperature);
            ESP_LOGE(TAG, "Valor del sensor: %i", output);

        // If you read the sensor data too often, it will heat up
        // http://www.kandrsmith.org/RJS/Misc/Hygrometers/dht_sht_how_fast.html
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main()
{
    xTaskCreate(dht_test, "dht_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}

