/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/*  WiFi softAP & station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "access_control.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"

#include "f900.h"
#include "r502.h"
#include "vl53l0x.h"
#include "webserver.h"
#include "settings.h"
#include "buzzer.h"
#include "tabledb.h"
#include "mqtt_helper.h"
#include "wifi.h"
#include "table_types.h"
#include "web_handlers.h"
#include "log_redirect.h"

static const char *TAG = "Main";

static tabledb_config_t table_fingerprint_config = {
    .namespace = "fingerprint",
    .version = TABLE_FINGERPRINT_STRUCT_VERSION,
    .size = sizeof(table_fingerprint_t),
    .update_cb = NULL
};

static tabledb_config_t table_face_config = {
    .namespace = "face",
    .version = TABLE_FACE_STRUCT_VERSION,
    .size = sizeof(table_face_t),
    .update_cb = NULL
};

void settings_change_callback(settings_t *new_srttings) {
    ESP_LOGI(TAG, "Configuration changed - applying new settings");

    // Update web server auth if changed
    webserver_set_auth(new_srttings->basic_auth_user, new_srttings->basic_auth_password);
    log_redirect_set_enabled(new_srttings->log_capture_enabled);
}

void start_and_configure_webserver() {
    httpd_handle_t server = webserver_start();
    settings_t *settings  = settings_get_settings();
    webserver_set_auth(settings->basic_auth_user, settings->basic_auth_password);

    register_settings_web_handlers(server);

    register_enrollment_web_handlers(server, &table_face_config, &table_fingerprint_config);

    register_photo_web_handlers(server);

    register_log_web_handlers(server);

    register_system_web_handlers(server);

    register_ota_web_handlers(server);

    register_static_web_handlers(server);
}

void fingerprint_success_callback(uint32_t user_id) {
    ESP_LOGI(TAG, "Fingerprint verified for user %" PRIu32, user_id);
    buzzer_success_chime();
}

void face_success_callback(uint32_t user_id) {
    ESP_LOGI(TAG, "Face verified for user %" PRIu32, user_id);
    buzzer_success_chime();
}

void start_and_configure_access_control() {
    access_control_start();
    access_control_set_fingerprint_success_callback(fingerprint_success_callback);
    access_control_set_face_success_callback(face_success_callback);
}

bool start_tof_sensor() {
    // Init VL53L0X sensor
    bool ret = vl53l0x_config(
        0,      // I2C port
        36,     // SCL pin
        37,     // SDA pin
        17,     // XSHUT pin
        33,     // IRQ pin
        0x29,   // Default I2C address
        true    // 2.8V I/O mode
    );
    if (!ret) {
        ESP_LOGE(TAG, "Failed to configure VL53L0X sensor");
        return false;
    }

    const char* err = vl53l0x_init();
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize VL53L0X sensor: %s", err);
        return false;
    }

    return true;
}

void app_main(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Init config */
    settings_init();
    settings_set_change_callback(settings_change_callback);

    settings_t *settings = settings_get_settings();

    size_t log_buffer_size = (size_t)((settings->log_size_limit > 0) ? settings->log_size_limit : 256);
    esp_err_t log_init_ret = log_redirect_init(log_buffer_size, settings->log_capture_enabled);
    if (log_init_ret != ESP_OK) {
        ESP_LOGW(TAG, "Log redirect initialization failed: %s", esp_err_to_name(log_init_ret));
    }

    /* Init WiFi */
    wifi_init();

    if (settings->ap_mode_enabled) {
        wifi_init_softap(settings->ap_ssid, settings->ap_password);
    }
    wifi_init_sta(settings->wifi_sta_ssid, settings->wifi_sta_password);

    wifi_start();

    /* Init tabledb */
    ESP_ERROR_CHECK(tabledb_init(&table_fingerprint_config));
    ESP_ERROR_CHECK(tabledb_init(&table_face_config));

    /* Init F900 */
    f900_init((f900_config_t){.rx_pin = 34, .tx_pin = 35, .en_pin = 21, .uart_num = UART_NUM_2});

    /* Init R502 */
    r502_init((r502_config_t){.rx_pin = 10, .tx_pin = 11, .en_pin = 9, .irq_pin = 8, .uart_num = UART_NUM_1, .address = 0xFFFFFFFF});
    r502_set_enable(true);

    // Buzzer initialization
    if (settings->buzzer_enabled) {
        buzzer_init(38); // GPIO 38 for buzzer
        buzzer_short_beep();
    } else {
        ESP_LOGI(TAG, "Buzzer is disabled in settings");
    }

    // Start ToF sensor
    if (!start_tof_sensor()) {
        ESP_LOGE(TAG, "ToF sensor initialization failed");
    }

    // Verify password
    r502_generic_reply sensor_reply;
    ret = r502_vfypwd(0x00000000, &sensor_reply);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fingerprint sensor password verification failed");
        // TODO: Fatal error
    }

    // Configure mqtt if enabled
    if (settings->mqtt_enabled) {
        ESP_LOGI(TAG, "MQTT enabled, URI: %s", settings->mqtt_uri);
        mqtt_client_init(&(mqtt_client_config_t){
            .uri = settings->mqtt_uri,
            .username = settings->mqtt_username,
            .password = settings->mqtt_password,
            .client_id = settings->mqtt_client_id,
            .keepalive_sec = settings->mqtt_keepalive,
            .timeout_ms = 5000, // 5 seconds
            .buffer_size = 1024, // 1KB buffer
            .message_retry_count = 3 // Retry up to 3 times for QoS>0 messages
        });
    } else {
        ESP_LOGI(TAG, "MQTT is disabled");
    }

    /* Start access control tasks */
    start_and_configure_access_control();

    // Start web server
    start_and_configure_webserver();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
