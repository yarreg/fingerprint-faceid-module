#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "cJSON.h"
#include "webserver.h"

static const char *TAG = "OTA";

// From web_system_handlers.c - declared here since it's static there
static void restart_task(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Delay to ensure response is sent
    esp_restart();
}

static esp_err_t ota_update_handler(httpd_req_t *req) {
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "Failed to find OTA update partition");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition not found");
        return ESP_FAIL;
    }

    char boundary[128] = {0};
    bool is_multipart = false;
    
    // Check for multipart/form-data
    if (httpd_req_get_hdr_value_str(req, "Content-Type", boundary, sizeof(boundary))) {
        if (strstr(boundary, "multipart/form-data")) {
            is_multipart = true;
            // Extract boundary string
            char *b_start = strstr(boundary, "boundary=");
            if (b_start) {
                snprintf(boundary, sizeof(boundary), "--%s", b_start + 9);
            }
        }
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA initialization failed");
        return err;
    }

    char buf[1024];
    int received;
    int total_received = 0;
    bool ota_in_progress = false;
    bool error = false;

    while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
        if (is_multipart) {
            // Handle multipart form data parsing
            char *payload_start = strstr(buf, "\r\n\r\n");
            if (payload_start) {
                payload_start += 4;
                int payload_len = received - (payload_start - buf);
                err = esp_ota_write(ota_handle, payload_start, payload_len);
                ota_in_progress = true;
            } else if (ota_in_progress) {
                // Check for boundary in the end
                char *boundary_pos = strstr(buf, boundary);
                if (boundary_pos) {
                    int payload_len = boundary_pos - buf - 2; // Subtract preceding \r\n
                    err = esp_ota_write(ota_handle, buf, payload_len);
                    break; // End of file
                } else {
                    err = esp_ota_write(ota_handle, buf, received);
                }
            }
        } else {
            // Handle direct binary upload
            err = esp_ota_write(ota_handle, buf, received);
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            error = true;
            break;
        }
        total_received += received;
    }

    cJSON *response = cJSON_CreateObject();
    if (!error && received >= 0 && esp_ota_end(ota_handle) == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful, written %d bytes", total_received);
        err = esp_ota_set_boot_partition(update_partition);
        if (err == ESP_OK) {
            cJSON_AddBoolToObject(response, "ok", true);
            cJSON_AddStringToObject(response, "message", "Firmware update successful. Rebooting...");
            
            // Send response before restarting
            char *json_str = cJSON_PrintUnformatted(response);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json_str);
            cJSON_free(json_str);
            
            // Schedule restart after short delay
            xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
            return ESP_OK;
        }
    }

    // If we got here, there was an error
    esp_ota_abort(ota_handle);
    cJSON_AddBoolToObject(response, "ok", false);
    cJSON_AddStringToObject(response, "error", "Failed to complete OTA update");
    
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    cJSON_Delete(response);
    return ESP_FAIL;
}

void register_ota_web_handlers(httpd_handle_t server) {
    const webserver_uri_t ota_handlers[] = {
        {.uri = "/api/system/update", .method = HTTP_POST, .handler = ota_update_handler, .require_auth = true},
    };

    for (int i = 0; i < sizeof(ota_handlers)/sizeof(ota_handlers[0]); i++) {
        webserver_register_uri_handler(server, &ota_handlers[i]);
    }
}
