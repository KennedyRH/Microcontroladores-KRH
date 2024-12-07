#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <esp_netif.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <mqtt_client.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#include <esp_task_wdt.h>

#include "driver/gpio.h"
#include "portmacro.h"
#include "esp_mac.h"

#define LED_PIN GPIO_NUM_21
#define GPIO_OUTPUT_PIN_SEL (1ULL << LED_PIN)

#define OFF 0      // ESTADO APAGADO
#define MEDIUM 1   // ESTADO PARPADEO 0.5s
#define FAST 2     // ESTADO PARPADEO 0.1s
#define SLOW 3     // ESTADO PARPADEO 1s
#define VARIABLE 4 // ESTADO PARPADEO VARIABLE


#define SSID "CLARO6FD0"
#define PASS "3B4FHJH3L3"
#define MAX_RETRY 50 // REINTENTOS MAX.
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int retry_num = 0;    // REINTENTOS WIFI
bool wifi_connected = false; // ESTADO WIFI

static const char *TAG = "Proyecto Final";
static const char *topic = "micro/esp32s3/pf";

bool mqtt_connected = false;            // ESTADO MQTT
esp_mqtt_client_handle_t client = NULL; // CLIENTE MQTT

TimerHandle_t tim_100ms;   // TIMER HANDLE
TimerHandle_t tim_250ms;   // VAR TIMER HANDLE
uint16_t var_time_set = 0; // TIMER DEFAULT SET

/*-------- FSM DATA --------*/
struct IO_DATA
{
    bool pb;
    bool change;
    uint8_t prev_state;
    uint8_t curr_state;
    uint8_t next_state;
    uint8_t cnt_mid;
    uint8_t cnt_fast;
    uint8_t cnt_slow;
    uint8_t cnt_var;
} io_data;

/*-------- TIMER CALLBACK FUNCTION --------*/
void TimerCallBack(TimerHandle_t xTimer)
{
    static int pin_state = 0;
    if (xTimer == tim_100ms)
    {
        switch (io_data.curr_state)
        {
        case OFF:
            gpio_set_level(LED_PIN, 1);
            break;
        case MEDIUM:
            io_data.cnt_mid++;
            if (io_data.cnt_mid == 5)
            {
                io_data.cnt_mid = 0;
                pin_state = pin_state == 0 ? 1 : 0;
                gpio_set_level(LED_PIN, pin_state);
            }
            break;
        case FAST:
            io_data.cnt_fast++;
            if (io_data.cnt_fast == 1)
            {
                io_data.cnt_fast = 0;
                pin_state = pin_state == 0 ? 1 : 0;
                gpio_set_level(LED_PIN, pin_state);
            }
            break;
        case SLOW:
            io_data.cnt_slow++;
            if (io_data.cnt_slow == 10)
            {
                io_data.cnt_slow = 0;
                pin_state = pin_state == 0 ? 1 : 0;
                gpio_set_level(LED_PIN, pin_state);
            }
            break;
        case VARIABLE:
            io_data.cnt_var++;
            if (io_data.cnt_var >= var_time_set)
            {
                io_data.cnt_var = 0;
                pin_state = pin_state == 0 ? 1 : 0;
                gpio_set_level(LED_PIN, pin_state);
            }
            break;
        }
    }

    if (xTimer == tim_250ms && io_data.curr_state == VARIABLE)
    {
        if (var_time_set <= 10)
        {
            var_time_set += 1;
        }
        else
        {
            var_time_set = 0;
        }
    }
}

/*-------- FSM FUNCTIONS --------*/
int offFunc(void)
{
    io_data.prev_state = io_data.curr_state;
    io_data.curr_state = OFF;

    while (1)
    {
        if (io_data.pb == true)
        {
            io_data.pb = false;
            io_data.change = true;
            return MEDIUM;
        }
    }
}

int mediumFunc(void)
{
    io_data.prev_state = io_data.curr_state;
    io_data.curr_state = MEDIUM;

    io_data.cnt_mid = 0;

    while (1)
    {
        if (io_data.pb == true)
        {
            io_data.pb = false;
            io_data.change = true;
            return FAST;
        }
    }
}

int fastFunc(void)
{
    io_data.prev_state = io_data.curr_state;
    io_data.curr_state = FAST;

    io_data.cnt_fast = 0;

    while (1)
    {
        if (io_data.pb == true)
        {
            io_data.pb = false;
            io_data.change = true;
            return SLOW;
        }
    }
}

int slowFunc(void)
{
    io_data.prev_state = io_data.curr_state;
    io_data.curr_state = SLOW;

    io_data.cnt_slow = 0;

    while (1)
    {
        if (io_data.pb == true)
        {
            io_data.pb = false;
            io_data.change = true;
            return VARIABLE;
        }
    }
}

int varFunc(void)
{
    io_data.prev_state = io_data.curr_state;
    io_data.curr_state = VARIABLE;

    io_data.cnt_var = 0;

    while (1)
    {
        if (io_data.pb == true)
        {
            io_data.pb = false;
            io_data.change = true;
            return OFF;
        }
    }
}

/*-------- MQTT EVENT HANDLER --------*/
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id = 0;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        // Subscripcion
        esp_mqtt_client_subscribe(client, "micro/esp32s3/connection", 2);
        esp_mqtt_client_subscribe(client, "micro/esp32s3/estado", 2);
        esp_mqtt_client_subscribe(client, "micro/esp32s3/pf", 2);
        esp_mqtt_client_subscribe(client, "micro/esp32s3/led", 2);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        esp_mqtt_client_publish(client, "micro/esp32s3/connection", "Connected", 0, 0, 0);

        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);

        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");

        char temp[10];
        strncpy(temp, event->data, event->data_len);

        if (strcmp(event->topic, topic) == 0)
        {
            ESP_LOGW(TAG, "TOPIC CORRECTO");
            if (strcmp(temp, "1") == 0)
            {
                io_data.pb = true;
            }
        }

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        ESP_LOGI(TAG, "MQTT5 return code is %d", event->error_handle->connect_return_code);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            // log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            // log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            // log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/*-------- WIFI EVENT HANDLER --------*/
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retry_num < MAX_RETRY)
        {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/*-------- WIFI INIT CONFIG --------*/
void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate(); // Evento FreeRTOS para WiFi

    // ESP_ERROR_CHECK(esp_netif_init());                          //Inicializacion Network Interface
    // ESP_ERROR_CHECK(esp_event_loop_create_default());           //No se
    esp_netif_create_default_wifi_sta(); // Config Network Interface para STATION

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT(); // Config INIT por defecto
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));                // WiFi INIT

    esp_event_handler_instance_t instance_any_id; // Handler de Evento Cualquiera
    esp_event_handler_instance_t instance_got_ip; // Handler Evento de IP

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id)); // Registro Evento wifi

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip)); // Registro Evento IP

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = ""}};
    strcpy((char *)wifi_config.sta.ssid, SSID);
    strcpy((char *)wifi_config.sta.password, PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                   // Seleccion STA
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config)); // Config STA
    ESP_ERROR_CHECK(esp_wifi_start());                                   // Iniciar WIFI

    ESP_LOGI(TAG, "WIFI INICIADO");

    /* Esto lo saqué del ejemplo. Solo entendí que es para verificar si se logró la conexión */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s", SSID);
        wifi_connected = true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", SSID);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/*-------- MQTT CLIENT CONFIG --------*/
static void mqtt5_app_start(void)
{
    ESP_LOGI(TAG, "STARTING MQTT");
    esp_mqtt_client_config_t mqttConfig = {
        .broker.address.uri = "mqtt://broker.hivemq.com:1883",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,
        .credentials.username = "",
        .credentials.authentication.password = ""};

    client = esp_mqtt_client_init(&mqttConfig);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, client);
    esp_mqtt_client_start(client);
}

/*-------- TASKS FUNCTIONS --------*/
void input(void)
{
    while (1)
    {
        if (io_data.next_state == OFF)
        {
            // ESP_LOGW(TAG, "OFF");
            io_data.next_state = offFunc();
        }
        if (io_data.next_state == MEDIUM)
        {
            // ESP_LOGW(TAG, "MED");
            io_data.next_state = mediumFunc();
        }
        if (io_data.next_state == FAST)
        {
            // ESP_LOGW(TAG, "FAST");
            io_data.next_state = fastFunc();
        }
        if (io_data.next_state == SLOW)
        {
            // ESP_LOGW(TAG, "SLOW");
            io_data.next_state = slowFunc();
        }
        if (io_data.next_state == VARIABLE)
        {
            // ESP_LOGW(TAG, "VAR");
            io_data.next_state = varFunc();
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void output(void)
{
    static char buffer[50];
    static char mqtt_buff[50];

    while (1)
    {
        if (io_data.change == true)
        {
            vTaskDelay(15/portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "STATE CHANGE");
            io_data.change = false;
            if (io_data.curr_state == OFF)
            {
                sprintf(buffer, "Cambiando al estado: %s\n", "OFF");
            }
            else if (io_data.curr_state == SLOW)
            {
                sprintf(buffer, "Cambiando al estado: %s\n", "SLOW");
            }
            else if (io_data.curr_state == MEDIUM)
            {
                sprintf(buffer, "Cambiando al estado: %s\n", "MEDIUM");
            }
            else if (io_data.curr_state == FAST)
            {
                sprintf(buffer, "Cambiando al estado: %s\n", "FAST");
            }
            else if (io_data.curr_state == VARIABLE)
            {
                sprintf(buffer, "Cambiando al estado: %s\n", "VARIABLE");
            }
            printf(buffer);
            esp_mqtt_client_publish(client, "micro/esp32s3/estado", itoa(io_data.curr_state, mqtt_buff, 10), 0, 0, 0);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_task_wdt_config_t config = {
        .timeout_ms = 60000,
        .trigger_panic = true,
        .idle_core_mask = 0, // i.e. do not watch any idle task
    };
    ret = esp_task_wdt_reconfigure(&config);
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    gpio_config_t pin_conf = {};
    pin_conf.intr_type = GPIO_INTR_DISABLE;      // disable interrupt
    pin_conf.mode = GPIO_MODE_OUTPUT;            // set as output mode
    pin_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL; //  Bit Mask
    pin_conf.pull_down_en = 0;                   // disable pull-down mode
    pin_conf.pull_up_en = 0;                     // disable pull-up mode
    gpio_config(&pin_conf);                      // configure GPIO with the given settings

    gpio_set_level(LED_PIN, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(LED_PIN, !gpio_get_level(LED_PIN));

    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    // chequear esto
    mqtt5_app_start();
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    while (mqtt_connected == false)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    xTaskCreate((void *)&input, "Entrada", 10240, NULL, 1, NULL);
    xTaskCreate((void *)&output, "Salida", 10240, NULL, 1, NULL);

    tim_100ms = xTimerCreate("Timer100", 100 / portTICK_PERIOD_MS, pdTRUE, (void *)0, &TimerCallBack);
    tim_250ms = xTimerCreate("Timer250", 250 / portTICK_PERIOD_MS, pdTRUE, (void *)0, &TimerCallBack);

    if (tim_100ms == NULL || tim_250ms == NULL)
    {
        ESP_LOGE(TAG, "ERROR TIMER");
    }
    else
    {
        xTimerStart(tim_100ms, 0);
        xTimerStart(tim_250ms, 0);
    }
}
