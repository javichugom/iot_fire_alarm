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

#define WIFI_SSID      "*"
#define WIFI_PASS      "*"
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com"

#define STREET_NAME "*"
#define LATITUDE 0
#define LONGITUDE 0
#define FLOOR 2
#define SENSOR_TYPE DHT_TYPE_DHT11

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
            
        }
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
             esp_mqtt_client_subscribe(client, "edf1/+/reset", 1);
            break;

        case MQTT_EVENT_DATA:
            char topic[50];
            snprintf(topic, event->topic_len + 1, "%.*s", event->topic_len, event->topic);
            ESP_LOGI (TAG, "MENSAJE RECIBIDO %s", topic);
            if(strcmp(topic, "edf1/fl1/reset") == 0 || strcmp(topic, "edf1/fl2/reset"))
            {
                ESP_LOGI (TAG, "RESETEANDO");
                esp_restart();
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Suscripción confirmada, msg_id=%d", event->msg_id);
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
    .session.last_will.topic = "edf1/fl1/error",
    .session.last_will.msg = "Dispositivo del Piso 1 se ha desconectado",
    .session.last_will.qos = 1,
    .session.last_will.retain = true, 
    .session.keepalive = 30,
};

esp_mqtt_client_handle_t client;

static void mqtt_app_start() {
    client = esp_mqtt_client_init(&mqtt_cfg);
    
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    while(esp_mqtt_client_start(client) != ESP_FAIL)
    {
        ESP_LOGE(TAG, "ESPERANDO CONEXIÓN...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void mqtt_publish_tmp(float temp)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "payload",temp);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "edf1/fl1/tmp1", post_data, 0, 1, 1);
    free(post_data);
    cJSON_Delete(root);
}

static void mqtt_publish_smk(int valor)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "payload",valor);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "edf1/fl1/smk1", post_data, 0, 1, 1);
    free(post_data);
    cJSON_Delete(root);
}

static void mqtt_publish_tmp_register(float temp, int smk)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "tmp",temp);
    cJSON_AddNumberToObject(root, "smk", smk);
    cJSON_AddNumberToObject(root, "flr", 2);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "edf1/fl1/reg1", post_data, 0, 1, 1);
    free(post_data);
    cJSON_Delete(root);
}

float temperature = 200.0;
float humidity = 0.0;
int output_smk;

void smk_task(void *pvParameters)
{
    init_smk();
    while(1)
    {
        output_smk = detect_smk();
        mqtt_publish_smk(output_smk);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void tmp_task(void *pvParameters)
{
    temperature = 102.0;
    humidity = 0.0;
    while(1)
    {
       dht_read_float_data(SENSOR_TYPE, 33, &humidity, &temperature);
       mqtt_publish_tmp(temperature);
       vTaskDelay(pdMS_TO_TICKS(400));
    }
}

SemaphoreHandle_t sem;

void fire_task(void *pvParameters)
{
    if (sem == NULL) {
        ESP_LOGE(TAG, "Semáforo no inicializado");
        vTaskDelete(NULL);
        return;
    }

    if (client == NULL) {
        ESP_LOGE(TAG, "Cliente MQTT no inicializado");
        vTaskDelete(NULL);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Error al crear objeto JSON (sin memoria)");
        vTaskDelete(NULL);
        return;
    }

    cJSON_AddStringToObject(root, "name", STREET_NAME);
    cJSON_AddNumberToObject(root, "lat", LATITUDE);
    cJSON_AddNumberToObject(root, "lon", LONGITUDE);

    if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "ENVIANDO ALERTA");

        char *post_data = cJSON_PrintUnformatted(root);
        if (post_data != NULL) {
            esp_err_t err = esp_mqtt_client_publish(client, "edf1/address", post_data, 0, 1, 2);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error al publicar mensaje MQTT: %s", esp_err_to_name(err));
            }
            free(post_data);
        } else {
            ESP_LOGE(TAG, "Error al convertir JSON a texto");
        }

        xSemaphoreGive(sem);
    } else {
        ESP_LOGE(TAG, "No se pudo tomar el semáforo");
    }
    cJSON_Delete(root);
    vTaskDelete(NULL);
}

void fire_detected(void *pvParameters)
{
    int flag = 0;
    while(flag == 0){
        if(output_smk == 1 && (temperature > 100 || temperature < 200))
        {
            xSemaphoreGive(sem);
            static TaskHandle_t fireTaskHandle = NULL;
            if (fireTaskHandle == NULL) {
                xTaskCreate(fire_task, "fire_task", 8192, NULL, 5, &fireTaskHandle);
            }
            flag = 1;
        }
        //ESP_LOGI("HEAP", "Free heap: %d", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{

    sem = xSemaphoreCreateBinary();
    assert(sem != NULL);
    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000));
    mqtt_app_start();
    vTaskDelay(pdMS_TO_TICKS(2000));

    BaseType_t res;

    res = xTaskCreate(smk_task, "smk_task", 8192, NULL, 2, NULL);
    if (res != pdPASS) {
        ESP_LOGE("APP", "No se pudo crear la tarea smk_task");
    }

    res = xTaskCreate(tmp_task, "tmp_task", 8192, NULL, 3, NULL);
    if (res != pdPASS) {
        ESP_LOGE("APP", "No se pudo crear la tarea tmp_task");
    }

    // Crear tarea de detección de fuego (no la de envío directamente)
    res = xTaskCreate(fire_detected, "fire_detect", 8192, NULL, 5, NULL);
    if (res != pdPASS) {
        ESP_LOGE("APP", "No se pudo crear la tarea fire_detected");
    }
}