#include <string.h>
#include "esp_http_server.h"
#include "cJSON.h"
#include "settings.h"
#include "webserver.h"


static esp_err_t get_settings_handler(httpd_req_t *req) {
    char value[SETTINGS_VALUE_MAX_LEN];
    cJSON *root = cJSON_CreateObject();

    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }

    const settings_field_t *fields = settings_get_fields();
    for (int i = 0; fields[i].key != NULL; i++) {
        if (settings_get_by_string(fields[i].key, value, sizeof(value)) == ESP_OK) {
            switch (fields[i].type) {
                case SETTINGS_TYPE_BOOL:
                    cJSON_AddBoolToObject(root, fields[i].key, strcmp(value, "true") == 0);
                    break;
                case SETTINGS_TYPE_INT: {
                    int int_val = atoi(value);
                    cJSON_AddNumberToObject(root, fields[i].key, int_val);
                    break;
                }
                case SETTINGS_TYPE_STRING:
                    cJSON_AddStringToObject(root, fields[i].key, value);
                    break;
            }
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t update_settings_handler(httpd_req_t *req) {
    char content[1024];
    if (req->content_len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Content too long");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, content, sizeof(content));
    if (received <= 0) {
        return ESP_FAIL;
    }
    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const settings_field_t *fields = settings_get_fields();
    bool config_changed = false;

    for (int i = 0; fields[i].key != NULL; i++) {
        cJSON *item = cJSON_GetObjectItem(root, fields[i].key);
        if (item) {
            char value_str[SETTINGS_VALUE_MAX_LEN];

            switch (fields[i].type) {
                case SETTINGS_TYPE_BOOL:
                    snprintf(value_str, sizeof(value_str), "%s",
                            cJSON_IsTrue(item) ? "true" : "false");
                    break;
                case SETTINGS_TYPE_INT:
                    snprintf(value_str, sizeof(value_str), "%d",
                            item->valueint);
                    break;
                case SETTINGS_TYPE_STRING:
                    strncpy(value_str, item->valuestring, sizeof(value_str));
                    value_str[sizeof(value_str) - 1] = '\0';
                    break;
            }

            if (settings_set_by_string(fields[i].key, value_str) == ESP_OK) {
                config_changed = true;
            }
        }
    }

    cJSON_Delete(root);

    if (!config_changed) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No valid config changes");
        return ESP_FAIL;
    }

    if (settings_save() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddStringToObject(response, "message", "Settings updated");
        char *json_str = cJSON_PrintUnformatted(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_str);
        cJSON_free(json_str);
        cJSON_Delete(response);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void register_settings_web_handlers(httpd_handle_t server) {
    const webserver_uri_t app_handlers[] = {
        {.uri = "/api/settings", .method = HTTP_GET, .handler = get_settings_handler, .require_auth = true},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = update_settings_handler, .require_auth = true}
    };

    for (int i = 0; i < sizeof(app_handlers)/sizeof(app_handlers[0]); i++) {
        webserver_register_uri_handler(server, &app_handlers[i]);
    }
}
