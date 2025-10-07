#include <stdio.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "mqtt_client.h"
#include "dht.h"
#include "MQ_Sensor.h"
#include "cJSON.h"

#define TAG "EMERGENCY_FIRE"

#define WIFI_SSID      "MOVISTAR-WIFI6-F000"
#define WIFI_PASS      "nKJETHW3CJ9T9EUU9FWC"
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com"

#define STREET_NAME "Calle Gutiérrez Sañudo"
#define FLOOR 3

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Reintentando conexión...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! Dirección IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    // Inicializa la NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registra eventos
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta terminado.");
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al broker Node-RED!");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Mensaje recibido en %.*s: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
            break;

        default:
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(event_data);
}

esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = MQTT_BROKER_URI,
    .broker.address.port = 1883,    
};

esp_mqtt_client_handle_t client;

static void mqtt_app_start() {
    client = esp_mqtt_client_init(&mqtt_cfg);
    
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

static void mqtt_publish_tmp(float temp)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "payload",temp);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "esp32/edf1/tmp1", post_data, 0, 1, 1);
}

static void mqtt_publish_smk(int valor)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "payload",valor);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "esp32/edf1/smk1", post_data, 0, 1, 2);
}

float temperature = 24.0;
float humidity = 0.0;
int output_smk;

void smk_task(void *pvParameters)
{
    init_smk();
    while(1)
    {
        output_smk = detect_smk();
        mqtt_publish_smk(output_smk);
        vTaskDelay(200/portTICK_PERIOD_MS);
    }
}

void tmp_task(void *pvParameters)
{
    temperature = 102.0;
    humidity = 0.0;
    while(1)
    {
        /* Cuando se tenga el sensor de temperatura se descomenta
        *  dht_read_float_data(SENSOR_TYPE, 33, &humidity, &temperatura);
        */
       mqtt_publish_tmp(temperature);
       vTaskDelay(pdMS_TO_TICKS(400));
    }
}

void fire_detected(void *pvParameters)
{
    while(1){
        if(output_smk == 1 && (temperature > 100 || temperature < 200))
        {
            cJSON *root = cJSON_CreateObject();

            cJSON_AddStringToObject(root, "name", STREET_NAME);
            cJSON_AddNumberToObject(root, "floor", FLOOR);
            cJSON_AddNumberToObject(root, "smk", output_smk);
            cJSON_AddNumberToObject(root, "tmp", temperature);
            char *post_data = cJSON_PrintUnformatted(root);
            esp_mqtt_client_publish(client, "esp32/edf1/building", post_data, 0, 1, 2);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    wifi_init_sta();
    vTaskDelay(2000/portTICK_PERIOD_MS);
    mqtt_app_start();
    vTaskDelay(2000/portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "EMPIEZA MQTT");

    xTaskCreate(smk_task, "smk_task", configMINIMAL_STACK_SIZE * 3, NULL, 2, NULL);
    xTaskCreate(tmp_task, "tmp_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
    xTaskCreate(fire_detected, "fire_task", configMINIMAL_STACK_SIZE * 3, NULL, 7, NULL);
}