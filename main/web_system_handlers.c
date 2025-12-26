#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "webserver.h"

// Function to restart the system
void restart_task(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Delay to ensure packet is sent
    esp_restart();
}

static esp_err_t reboot_handler(httpd_req_t *req) {
    // Create response first
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "message", "Rebooting");
    
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    // Cleanup JSON resources
    cJSON_free(json_str);
    cJSON_Delete(response);

    // Create a task to restart the system
    xTaskCreate(&restart_task, "restart_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t get_firmware_info_handler(httpd_req_t *req) {
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "fw_version", app_desc->version);
    cJSON_AddStringToObject(root, "idf_version", app_desc->idf_ver);
    cJSON_AddStringToObject(root, "build_date", app_desc->date);
    cJSON_AddStringToObject(root, "build_time", app_desc->time);
    cJSON_AddStringToObject(root, "git_hash", FW_GIT_HASH);

    // Convert to string and send as HTTP response
    char *json_response = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);

    cJSON_free(json_response);
    cJSON_Delete(root);
    return ESP_OK;
}

void register_system_web_handlers(httpd_handle_t server) {
    const webserver_uri_t system_handlers[] = {
        {.uri = "/api/system/reboot", .method = HTTP_POST, .handler = reboot_handler, .require_auth = true},
        {.uri = "/api/system/firmware", .method = HTTP_GET, .handler = get_firmware_info_handler, .require_auth = true},
    };

    for (int i = 0; i < sizeof(system_handlers)/sizeof(system_handlers[0]); i++) {
        webserver_register_uri_handler(server, &system_handlers[i]);
    }
}
