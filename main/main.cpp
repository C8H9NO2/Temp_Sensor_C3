#include <sys/cdefs.h>
#include <nvs_flash.h>
#include <esp_adc_cal.h>
#include <esp32c3/pm.h>
#include <esp_pm.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "aht21.h"
#include "cJSON.h"

static const char *TAG = "TempSensor";


static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_mqtt_client_handle_t mqtt_client = {};
static char *Topic;


static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "try to connect to the AP");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, BIT1);   //BIT1 means WIFI_FAIL
        }
    } else if (event_base == IP_EVENT) {
        ESP_LOGI(TAG, "IP EVENT");
        if (event_id == IP_EVENT_STA_GOT_IP) {
            auto *event_got_ip = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event_got_ip->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, BIT0);    //BIT0 means WIFI_CONNECTED

        }
    }
}

void init_wifi() {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
//    esp_netif_dhcpc_stop(netif);
//    esp_netif_ip_info_t ip_info;
//    IP4_ADDR(&ip_info.ip, 192, 168, 10, 30);
//    IP4_ADDR(&ip_info.gw, 192, 168, 10, 20);
//    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
//    esp_netif_set_ip_info(netif, &ip_info);


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr,
                                                &instance_any_id));
    ESP_ERROR_CHECK(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr,
                                                &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    strcpy((char *) wifi_config.sta.ssid, CONFIG_WIFI_SSID);
    strcpy((char *) wifi_config.sta.password, CONFIG_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "ssid: %s", wifi_config.sta.ssid);
    ESP_LOGI(TAG, "password: %s", wifi_config.sta.password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           BIT0 | BIT1,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    if (bits & BIT0) {
        ESP_LOGI(TAG, "connect to AP");
    } else if (bits & BIT1) {
        ESP_LOGI(TAG, "Connect Fail");
    }

}

_Noreturn void read_temp(void *param) {
    test_init_aht21();
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    esp_adc_cal_characteristics_t *adc_chars;
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars);
    float temperature, humidity;
    uint32_t voltage;

    char topic_result[64] = {};
    strcat(topic_result, "homeassistant/sensor/");
    strcat(topic_result, CONFIG_AHT21_Device_Name);
    strcat(topic_result, "/state");

    while (true) {
        vTaskDelay(60000 / portTICK_RATE_MS);
        aht21_read_data(&temperature, &humidity);
        voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_4), adc_chars);
        cJSON *root;
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "temperature", temperature / 10.0);
        cJSON_AddNumberToObject(root, "humidity", humidity / 10.0);
        cJSON_AddNumberToObject(root, "voltage", voltage * 2);
        char *json_string = cJSON_Print(root);
        cJSON_Delete(root);
        printf(json_string);
        if (temperature < 600 && humidity < 1001 && voltage > 1750) {
            esp_mqtt_client_publish(mqtt_client, topic_result, json_string, 0, 1, 0);
        }
        cJSON_free(json_string);

    }
}


void mqtt_init() {
    const esp_mqtt_client_config_t mqtt_config = {.uri=CONFIG_MQTT_BROKER_URL,
            .username=CONFIG_MQTT_USERNAME,
            .password=CONFIG_MQTT_PASSWORD};
    printf("mqtt URI:%s", mqtt_config.uri);
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_register_device() {
    cJSON *init_temperature_root;
    init_temperature_root = cJSON_CreateObject();
    cJSON_AddStringToObject(init_temperature_root, "device_class", "temperature");

    char temperature_name[32] = {};
    strcat(temperature_name, "Temperature_");
    strcat(temperature_name, CONFIG_AHT21_Device_Name);

    char temperature_config_topic[64] = {};
    strcat(temperature_config_topic, "homeassistant/sensor/");
    strcat(temperature_config_topic, CONFIG_AHT21_Device_Name);
    strcat(temperature_config_topic, "T/config");

    cJSON_AddStringToObject(init_temperature_root, "name", temperature_name);
    cJSON_AddStringToObject(init_temperature_root, "state_topic", Topic);
    cJSON_AddStringToObject(init_temperature_root, "unit_of_measurement", "°C");
    cJSON_AddStringToObject(init_temperature_root, "value_template", "{{ value_json.temperature}}");
    char *json_string_register_T = cJSON_Print(init_temperature_root);
    cJSON_Delete(init_temperature_root);
    esp_mqtt_client_publish(mqtt_client, temperature_config_topic, json_string_register_T, 0, 1, 0);
    printf(json_string_register_T);
    cJSON_free(json_string_register_T);


    cJSON *init_humidity_root;
    init_humidity_root = cJSON_CreateObject();
    cJSON_AddStringToObject(init_humidity_root, "device_class", "humidity");

    char humidity_name[32] = {};
    strcat(humidity_name, "Humidity_");
    strcat(humidity_name, CONFIG_AHT21_Device_Name);

    char humidity_config_topic[64] = {};
    strcat(humidity_config_topic, "homeassistant/sensor/");
    strcat(humidity_config_topic, CONFIG_AHT21_Device_Name);
    strcat(humidity_config_topic, "H/config");

    cJSON_AddStringToObject(init_humidity_root, "name", humidity_name);
    cJSON_AddStringToObject(init_humidity_root, "state_topic", Topic);
    cJSON_AddStringToObject(init_humidity_root, "unit_of_measurement", "%");
    cJSON_AddStringToObject(init_humidity_root, "value_template", "{{ value_json.humidity}}");
    char *json_string_register_H = cJSON_Print(init_humidity_root);
    cJSON_Delete(init_humidity_root);
    esp_mqtt_client_publish(mqtt_client, humidity_config_topic, json_string_register_H, 0, 1, 0);
    printf(json_string_register_H);
    cJSON_free(json_string_register_H);


    cJSON *init_voltage_root;
    init_voltage_root = cJSON_CreateObject();
    cJSON_AddStringToObject(init_voltage_root, "device_class", "voltage");

    char voltage_name[32] = {};
    strcat(voltage_name, "Voltage_");
    strcat(voltage_name, CONFIG_AHT21_Device_Name);

    char voltage_config_topic[64] = {};
    strcat(voltage_config_topic, "homeassistant/sensor/");
    strcat(voltage_config_topic, CONFIG_AHT21_Device_Name);
    strcat(voltage_config_topic, "V/config");

    cJSON_AddStringToObject(init_voltage_root, "name", voltage_name);
    cJSON_AddStringToObject(init_voltage_root, "state_topic", Topic);
    cJSON_AddStringToObject(init_voltage_root, "unit_of_measurement", "mV");
    cJSON_AddStringToObject(init_voltage_root, "value_template", "{{ value_json.voltage}}");
    char *json_string_register_V = cJSON_Print(init_voltage_root);
    cJSON_Delete(init_voltage_root);
    esp_mqtt_client_publish(mqtt_client, voltage_config_topic, json_string_register_V, 0, 1, 0);
    printf(json_string_register_V);
    cJSON_free(json_string_register_V);
}


extern "C" void app_main() {
    char topic_result[64] = {};
    strcat(topic_result, "homeassistant/sensor/");
    strcat(topic_result, CONFIG_AHT21_Device_Name);
    strcat(topic_result, "/state");
    Topic = topic_result;

    vTaskDelay(5000 / portTICK_RATE_MS);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    init_wifi();
    vTaskDelay(5000);
    mqtt_init();
    mqtt_register_device();
    xTaskCreate(read_temp, TAG, 4096, nullptr, 6, nullptr);
    esp_pm_config_esp32c3_t pm_config = {
            .max_freq_mhz=CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ,
            .min_freq_mhz=20,
            .light_sleep_enable=true,
    };
    esp_pm_configure(&pm_config);
}
