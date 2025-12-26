#ifndef _SETTINGS_MANAGEMENT_H_
#define _SETTINGS_MANAGEMENT_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    SETTINGS_TYPE_BOOL,
    SETTINGS_TYPE_INT,
    SETTINGS_TYPE_STRING
} settings_type_t;

typedef struct {
    const char     *key;
    settings_type_t type;
    size_t          offset;
    size_t          size;
} settings_field_t;

#define SETTINGS_VALUE_MAX_LEN 128

#define WIFI_SSID_MAX_LEN 64
#define WIFI_PASS_MAX_LEN 64
#define BASIC_AUTH_USER_MAX_LEN 32
#define BASIC_AUTH_PASS_MAX_LEN 32
#define MQTT_URI_MAX_LEN 100
#define MQTT_CLIENT_ID_MAX_LEN 20
#define MQTT_USERNAME_MAX_LEN 16
#define MQTT_PASSWORD_MAX_LEN 32

typedef struct {
    // Wi-Fi
    char wifi_sta_ssid[WIFI_SSID_MAX_LEN];
    char wifi_sta_password[WIFI_PASS_MAX_LEN];
    bool ap_mode_enabled;
    char ap_ssid[WIFI_SSID_MAX_LEN];
    char ap_password[WIFI_PASS_MAX_LEN];

    // Basic Auth
    char basic_auth_user[BASIC_AUTH_USER_MAX_LEN];
    char basic_auth_password[BASIC_AUTH_PASS_MAX_LEN];

    // MQTT
    bool mqtt_enabled;
    char mqtt_uri[MQTT_URI_MAX_LEN];
    char mqtt_client_id[MQTT_CLIENT_ID_MAX_LEN];
    char mqtt_username[MQTT_USERNAME_MAX_LEN];
    char mqtt_password[MQTT_PASSWORD_MAX_LEN];
    int mqtt_keepalive;

    // ToF Sensor
    int  distance_threshold;
    int  distance_trigger_time;

    // Buzzer
    bool buzzer_enabled;

    // LED
    bool led_enabled;

    // Log
    bool log_capture_enabled;
    int  log_size_limit;
} settings_t;

// Callback type for settings changes
typedef void (*settings_change_callback_t)(settings_t *new_settings);

// Initialize NVS and load settings
esp_err_t settings_init(void);

// Get current settings
settings_t* settings_get_settings(void);

// Save current settings to NVS
esp_err_t settings_save(void);

// Reset settings to defaults
esp_err_t settings_reset_to_defaults(void);

// Get list of settings fields
const settings_field_t* settings_get_fields(void);

// Set settings value by string key
esp_err_t settings_set_by_string(const char *key, const char *value);

// Get settings value by string key
esp_err_t settings_get_by_string(const char *key, char *out_value, size_t out_size);

// Set settings change callback
void settings_set_change_callback(settings_change_callback_t callback);

#endif /* _SETTINGS_MANAGEMENT_H_ */
