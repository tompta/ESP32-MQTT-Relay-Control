#include "driver/gpio.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
//#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;
//*******************set in menuconfig******************************
#define RELAY_PIN (gpio_num_t) CONFIG_RELAY_GPIO_NUMBER_SELECTION
char ESP32_MESSAGE_MQTT = CONFIG_ESP32_NUMBER_SELECTION; //not being used
const char *SUBSCRIBE_MQTT1 = CONFIG_SUBSCRIBE_TO_MQTT_1;
const char *SUBSCRIBE_MQTT2 = CONFIG_SUBSCRIBE_TO_MQTT_2;
const char *PUBLISH_MQTT = CONFIG_PUBLISH_TO_MQTT;
//***********************************************************************
int RELAY_CONNECTED = 0;
bool RELAY_CHANGED = 0;		//flag set in MQTT_EVENT_DATA

//*******************************************************************************************
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT, "ESP32 has connected", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, SUBSCRIBE_MQTT1, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
			if (CONFIG_SUBSCRIBE_TO_ONE_OR_TWO == 2){
				msg_id = esp_mqtt_client_subscribe(client, SUBSCRIBE_MQTT2, 1);
				ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
			}
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT, "ESP32 has subscribed", 0, 0, 0);
            ESP_LOGI(TAG, "ESP32 has subscribed, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            
			int EVENT_DATA = atoi(event->data);
			if (EVENT_DATA == 1 || EVENT_DATA == 0){//for turning on and off light
				RELAY_CONNECTED = EVENT_DATA ? 1 : 0;
				RELAY_CHANGED = true;
				msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT,"ESP32 light toggled" , 0, 0, 0);
			}
			if (EVENT_DATA == 3){ //for checking if device is online
				msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT,"ESP32 is alive" , 0, 0, 0);
			}
			if (EVENT_DATA == 4){// for checking the devices info
				//msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT,HEAP_SIZE , 0, 0, 0);
				//msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT,IDF_VERSION , 0, 0, 0);
				msg_id = esp_mqtt_client_publish(client, PUBLISH_MQTT, "future stats" , 0, 0, 0);
			}
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}
//*******************************************************************************************
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}
//*******************************************************************************************
static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}
//*******************************************************************************************
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .event_handle = mqtt_event_handler,
        // .user_context = (void *)your_context,
		.lwt_topic = PUBLISH_MQTT,
		.lwt_msg = "ESP32 last will message",

    };

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}
//*******************************************************************************************
void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);
    
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0);
    nvs_flash_erase();
    nvs_flash_init();
    wifi_init();
    mqtt_app_start();

    while(true){
		vTaskDelay(50 / portTICK_PERIOD_MS);
        if (RELAY_CHANGED) {
            RELAY_CHANGED = false;
            gpio_set_level(RELAY_PIN, RELAY_CONNECTED);
        }
    }
}
