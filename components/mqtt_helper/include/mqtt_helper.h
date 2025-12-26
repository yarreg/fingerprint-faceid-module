#ifndef MQTT_HELPER_H
#define MQTT_HELPER_H

#include "esp_event.h"
#include "mqtt_client.h"


/**
 * @brief MQTT client connection states
 */
typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_RECONNECTING
} mqtt_client_state_t;

/**
 * @brief MQTT client configuration structure
 */
typedef struct {
    const char *uri;                   ///< MQTT broker URI (mqtt[s]://host[:port])
    const char *username;              ///< Username for authentication
    const char *password;              ///< Password for authentication
    const char *client_id;             ///< Client identifier
    uint32_t keepalive_sec;            ///< Keepalive interval in seconds
    uint32_t timeout_ms;               ///< Network operation timeout
    size_t buffer_size;                ///< Internal buffer size
    int message_retry_count;           ///< QoS>0 message retry attempts
} mqtt_client_config_t;


/**
 * @brief Initialize MQTT client instance
 * @param config Client configuration
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_init(const mqtt_client_config_t *config);

/**
 * @brief Start MQTT connection
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_connect(void);

/**
 * @brief Disconnect from broker
 * @param force Immediate disconnect if true
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_disconnect(bool force);

/**
 * @brief Publish message
 * @param topic Topic string
 * @param data Message payload
 * @param len Payload length
 * @param qos Quality of Service level
 * @param retain Retain flag
 * @return Message ID or -1 on error
 */
int mqtt_client_publish(const char *topic, 
                       const void *data, size_t len, int qos, bool retain);

/**
 * @brief Get current connection state
 * @return Connection state
 */
mqtt_client_state_t mqtt_client_get_state(void);

/**
 * @brief Register event handler
 * @param event_handler Callback function
 * @param handler_arg User context
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_register_event_handler(esp_event_handler_t event_handler,
                                            void *handler_arg);

/**
 * @brief Destroy client instance
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_destroy(void);

#endif