#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "settings.h"

// Update static array: use settings_field_t and updated type enums
static const settings_field_t settings_fields[] = {
    // Wi-Fi
    {"wifi_sta_ssid", SETTINGS_TYPE_STRING, offsetof(settings_t, wifi_sta_ssid), WIFI_SSID_MAX_LEN},
    {"wifi_sta_password", SETTINGS_TYPE_STRING, offsetof(settings_t, wifi_sta_password), WIFI_PASS_MAX_LEN},
    {"ap_mode_enabled", SETTINGS_TYPE_BOOL, offsetof(settings_t, ap_mode_enabled), 0},
    {"ap_ssid", SETTINGS_TYPE_STRING, offsetof(settings_t, ap_ssid), WIFI_SSID_MAX_LEN},
    {"ap_password", SETTINGS_TYPE_STRING, offsetof(settings_t, ap_password), WIFI_PASS_MAX_LEN},

    // Basic Auth
    {"basic_auth_user", SETTINGS_TYPE_STRING, offsetof(settings_t, basic_auth_user), BASIC_AUTH_USER_MAX_LEN},
    {"basic_auth_password", SETTINGS_TYPE_STRING, offsetof(settings_t, basic_auth_password), BASIC_AUTH_PASS_MAX_LEN},

    // MQTT
    {"mqtt_enabled", SETTINGS_TYPE_BOOL, offsetof(settings_t, mqtt_enabled), 0},
    {"mqtt_uri", SETTINGS_TYPE_STRING, offsetof(settings_t, mqtt_uri), MQTT_URI_MAX_LEN},
    {"mqtt_client_id", SETTINGS_TYPE_STRING, offsetof(settings_t, mqtt_client_id), MQTT_CLIENT_ID_MAX_LEN},
    {"mqtt_username", SETTINGS_TYPE_STRING, offsetof(settings_t, mqtt_username), MQTT_USERNAME_MAX_LEN},
    {"mqtt_password", SETTINGS_TYPE_STRING, offsetof(settings_t, mqtt_password), MQTT_PASSWORD_MAX_LEN},
    {"mqtt_keepalive", SETTINGS_TYPE_INT, offsetof(settings_t, mqtt_keepalive), 0},

    // ToF Sensor
    {"distance_threshold", SETTINGS_TYPE_INT, offsetof(settings_t, distance_threshold), 0},
    {"distance_trigger_time", SETTINGS_TYPE_INT, offsetof(settings_t, distance_trigger_time), 0},

    // Buzzer
    {"buzzer_enabled", SETTINGS_TYPE_BOOL, offsetof(settings_t, buzzer_enabled), 0},

    // LED
    {"led_enabled", SETTINGS_TYPE_BOOL, offsetof(settings_t, led_enabled), 0},

    // Log
    {"log_capture_enabled", SETTINGS_TYPE_BOOL, offsetof(settings_t, log_capture_enabled), 0},
    {"log_size_limit", SETTINGS_TYPE_INT, offsetof(settings_t, log_size_limit), 0},

    {NULL, 0, 0, 0} // Sentinel
};

#define DEFAULT_WIFI_SSID "MyWiFiNetwork"
#define DEFAULT_WIFI_PASS "MyWiFiPassword"
#define DEFAULT_AP_SSID "AccessControlAP"
#define DEFAULT_AP_PASS "AccessControlPass"
#define DEFAULT_BASIC_AUTH_USER "admin"
#define DEFAULT_BASIC_AUTH_PASS "admin"
#define DEFAULT_MQTT_URI "mqtt://mqtt.example.com:8883"
#define DEFAULT_MQTT_CLIENT_ID "access_control"
#define DEFAULT_MQTT_USERNAME ""
#define DEFAULT_MQTT_PASSWORD ""
#define DEFAULT_MQTT_KEEPALIVE 60
#define DEFAULT_DISTANCE_THRESHOLD 50
#define DEFAULT_DISTANCE_TRIGGER_TIME 2
#define DEFAULT_LOG_CAPTURE_ENABLED true
#define DEFAULT_LOG_SIZE_LIMIT 1000

static settings_t g_settings;
static nvs_handle_t nvs_settings_handle;
static settings_change_callback_t settings_change_callback = NULL;

esp_err_t settings_set_by_string(const char *key, const char *value) {
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    for (const settings_field_t *field = settings_fields; field->key; field++) {
        if (strcmp(field->key, key) == 0) {
            void *field_ptr = (void *)((uint8_t *)&g_settings + field->offset);

            switch (field->type) {
                case SETTINGS_TYPE_BOOL:
                    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                        *(bool *)field_ptr = true;
                    } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                        *(bool *)field_ptr = false;
                    } else {
                        return ESP_ERR_INVALID_ARG;
                    }
                    break;

                case SETTINGS_TYPE_INT: {
                    char *endptr;
                    long val = strtol(value, &endptr, 10);
                    if (*endptr != '\0') {
                        return ESP_ERR_INVALID_ARG;
                    }
                    *(int *)field_ptr = (int)val;
                    break;
                }

                case SETTINGS_TYPE_STRING:
                    strncpy((char *)field_ptr, value, field->size);
                    ((char *)field_ptr)[field->size - 1] = '\0'; // Ensure null termination
                    break;

                default:
                    return ESP_ERR_NOT_SUPPORTED;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t settings_get_by_string(const char *key, char *out_value, size_t out_size) {
    if (!key || !out_value || out_size < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    for (const settings_field_t *field = settings_fields; field->key; field++) {
        if (strcmp(field->key, key) == 0) {
            const void *field_ptr = (const void *)((const uint8_t *)&g_settings + field->offset);

            switch (field->type) {
                case SETTINGS_TYPE_BOOL:
                    snprintf(out_value, out_size, "%s", *(const bool *)field_ptr ? "true" : "false");
                    break;

                case SETTINGS_TYPE_INT:
                    snprintf(out_value, out_size, "%d", *(const int *)field_ptr);
                    break;

                case SETTINGS_TYPE_STRING:
                    strncpy(out_value, (const char *)field_ptr, out_size);
                    out_value[out_size - 1] = '\0';
                    break;

                default:
                    return ESP_ERR_NOT_SUPPORTED;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void set_default_settings() {
    // Set defaults using the generic setter
    settings_set_by_string("wifi_sta_ssid", DEFAULT_WIFI_SSID);
    settings_set_by_string("wifi_sta_password", DEFAULT_WIFI_PASS);
    settings_set_by_string("ap_mode_enabled", "true");
    settings_set_by_string("ap_ssid", DEFAULT_AP_SSID);
    settings_set_by_string("ap_password", DEFAULT_AP_PASS);

    settings_set_by_string("basic_auth_user", DEFAULT_BASIC_AUTH_USER);
    settings_set_by_string("basic_auth_password", DEFAULT_BASIC_AUTH_PASS);

    settings_set_by_string("mqtt_enabled", "false");
    settings_set_by_string("mqtt_uri", DEFAULT_MQTT_URI);
    settings_set_by_string("mqtt_client_id", DEFAULT_MQTT_CLIENT_ID);
    settings_set_by_string("mqtt_username", DEFAULT_MQTT_USERNAME);
    settings_set_by_string("mqtt_password", DEFAULT_MQTT_PASSWORD);
    settings_set_by_string("mqtt_keepalive", "60");

    settings_set_by_string("distance_threshold", "50");
    settings_set_by_string("distance_trigger_time", "2");

    settings_set_by_string("buzzer_enabled", "true");

    settings_set_by_string("led_enabled", "true");

    settings_set_by_string("log_capture_enabled", DEFAULT_LOG_CAPTURE_ENABLED ? "true" : "false");
    char log_limit[16];
    snprintf(log_limit, sizeof(log_limit), "%d", DEFAULT_LOG_SIZE_LIMIT);
    settings_set_by_string("log_size_limit", log_limit);
}

esp_err_t settings_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nvs_open("storage", NVS_READWRITE, &nvs_settings_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // Try to load settings; if not found, set defaults
    size_t required_size = sizeof(settings_t);
    ret = nvs_get_blob(nvs_settings_handle, "settings", &g_settings, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_INVALID_LENGTH) {
        set_default_settings();
        return settings_save();
    }

    return ret;
}

settings_t* settings_get_settings(void) {
    return &g_settings;
}

esp_err_t settings_save(void) {
    esp_err_t err = nvs_set_blob(nvs_settings_handle, "settings", &g_settings, sizeof(settings_t));
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_commit(nvs_settings_handle);
    if (err == ESP_OK && settings_change_callback != NULL) {
        settings_change_callback(&g_settings);
    }
    return err;
}

void settings_set_change_callback(settings_change_callback_t callback) {
    settings_change_callback = callback;
}

const settings_field_t* settings_get_fields(void) {
    return settings_fields;
}

esp_err_t settings_reset_to_defaults(void) {
    set_default_settings();
    return settings_save();
}

