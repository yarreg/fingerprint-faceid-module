#include "mqtt_helper.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sys/queue.h"
#include "esp_err.h"

struct mqtt_client {
    esp_mqtt_client_handle_t mqtt_hdl;
    SemaphoreHandle_t lock;
    mqtt_client_state_t state;
    mqtt_client_config_t config;
};

typedef struct mqtt_client *mqtt_client_handle_t;

static const char *TAG = "mqtt_client";
static mqtt_client_handle_t global_client = NULL;  // Singleton instance

// Add helper to safely get error description
static inline const char *get_error_name(esp_mqtt_event_handle_t event) {
    if (event->error_handle) {
        return esp_err_to_name(event->error_handle->error_type);
    }
    return "unknown error";
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                              int32_t event_id, void *event_data) {
    mqtt_client_handle_t client = (mqtt_client_handle_t)handler_args;
    esp_mqtt_event_handle_t event = event_data;

    xSemaphoreTake(client->lock, portMAX_DELAY);
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        client->state = MQTT_STATE_CONNECTED;
        ESP_LOGI(TAG, "Connected to MQTT broker");
        break;
    case MQTT_EVENT_DISCONNECTED:
        client->state = MQTT_STATE_DISCONNECTED; // or set to RECONNECTING as needed
        ESP_LOGW(TAG, "Disconnected: %s", get_error_name(event));
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error: %s", get_error_name(event));
        client->state = MQTT_STATE_DISCONNECTED;
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        client->state = MQTT_STATE_CONNECTING;
        break;
    default:
        break;
    }
    xSemaphoreGive(client->lock);
}

esp_err_t mqtt_client_init(const mqtt_client_config_t *config) {
    if (!config || !config->uri) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    if(global_client) {
        ESP_LOGW(TAG, "Client already initialized");
        return ESP_OK;
    }

    global_client = calloc(1, sizeof(*global_client));
    if (!global_client) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }

    global_client->lock = xSemaphoreCreateMutex();
    if (!global_client->lock) {
        ESP_LOGE(TAG, "Mutex creation failed");
        free(global_client);
        global_client = NULL;
        return ESP_ERR_NO_MEM;
    }

    memcpy(&global_client->config, config, sizeof(*config));
    global_client->state = MQTT_STATE_DISCONNECTED;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config->uri,
        .credentials.username = config->username,
		.credentials.authentication.password = config->password,
        .credentials.client_id = config->client_id,
        .session.keepalive = config->keepalive_sec,
        .network.disable_auto_reconnect = true,
        .network.reconnect_timeout_ms = 4000,
        .buffer.size = config->buffer_size
    };

    global_client->mqtt_hdl = esp_mqtt_client_init(&mqtt_cfg);
    if (!global_client->mqtt_hdl) {
        ESP_LOGE(TAG, "ESP-MQTT initialization failed");
        vSemaphoreDelete(global_client->lock);
        free(global_client);
        global_client = NULL;
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(global_client->mqtt_hdl, ESP_EVENT_ANY_ID,
                                  mqtt_event_handler, global_client);
    return ESP_OK;
}

esp_err_t mqtt_client_connect(void) {
    if (!global_client) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(global_client->lock, portMAX_DELAY);
    esp_err_t ret = esp_mqtt_client_start(global_client->mqtt_hdl);
    xSemaphoreGive(global_client->lock);
    return ret;
}

esp_err_t mqtt_client_disconnect(bool force) {
    if (!global_client) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(global_client->lock, portMAX_DELAY);
    esp_err_t ret = ESP_OK;

    if (force) {
        ret = esp_mqtt_client_destroy(global_client->mqtt_hdl);
        global_client->mqtt_hdl = NULL;
    } else {
        ret = esp_mqtt_client_stop(global_client->mqtt_hdl);
    }

    if (ret == ESP_OK) {
        global_client->state = MQTT_STATE_DISCONNECTED;
    }

    xSemaphoreGive(global_client->lock);
    return ret;
}

int mqtt_client_publish(const char *topic, const void *data, size_t len, int qos, bool retain) {
    if (!global_client || !topic || !data || qos < 0 || qos > 2) {
        ESP_LOGE(TAG, "Invalid publish parameters");
        return -1;
    }

    xSemaphoreTake(global_client->lock, portMAX_DELAY);
    int msg_id = esp_mqtt_client_publish(global_client->mqtt_hdl, topic, data, len, qos, retain);
    xSemaphoreGive(global_client->lock);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed: %s", esp_err_to_name(msg_id));
    }
    return msg_id;
}

mqtt_client_state_t mqtt_client_get_state(void) {
    if (!global_client) {
        return MQTT_STATE_DISCONNECTED;
    }

    xSemaphoreTake(global_client->lock, portMAX_DELAY);
    mqtt_client_state_t state = global_client->state;
    xSemaphoreGive(global_client->lock);
    return state;
}

esp_err_t mqtt_client_register_event_handler(esp_event_handler_t event_handler, void *handler_arg) {
    if (!global_client || !event_handler) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_event_handler_register_with(global_client->mqtt_hdl,
                                         ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID,
                                         event_handler, handler_arg);
}

esp_err_t mqtt_client_destroy(void) {
    if (!global_client) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(global_client->lock, portMAX_DELAY);

    if (global_client->mqtt_hdl) {
        esp_mqtt_client_stop(global_client->mqtt_hdl);
        esp_mqtt_client_destroy(global_client->mqtt_hdl);
    }

    vSemaphoreDelete(global_client->lock);
    free(global_client);
    global_client = NULL;
    return ESP_OK;
}
