#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "cJSON.h"
#include "log_redirect.h"
#include "webserver.h"

static char level_to_char(esp_log_level_t level) {
    switch (level) {
        case ESP_LOG_ERROR:   return 'E';
        case ESP_LOG_WARN:    return 'W';
        case ESP_LOG_INFO:    return 'I';
        case ESP_LOG_DEBUG:   return 'D';
        case ESP_LOG_VERBOSE: return 'V';
        default:              return '?';
    }
}

typedef struct {
    cJSON *array;
    bool error;
} log_json_ctx_t;

static esp_err_t append_log_entry(const log_entry_view_t *entry, void *user_ctx) {
    log_json_ctx_t *ctx = (log_json_ctx_t *)user_ctx;
    if (!ctx->array) {
        ctx->error = true;
        return ESP_FAIL;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) {
        ctx->error = true;
        return ESP_ERR_NO_MEM;
    }

    char level_buf[2] = {level_to_char(entry->level), '\0'};
    cJSON_AddNumberToObject(item, "index", (double)entry->index);
    cJSON_AddNumberToObject(item, "timestamp", (double)entry->timestamp);
    cJSON_AddStringToObject(item, "level", level_buf);
    cJSON_AddStringToObject(item, "tag", entry->tag ? entry->tag : "");
    cJSON_AddStringToObject(item, "message", entry->message ? entry->message : "");

    cJSON_AddItemToArray(ctx->array, item);
    return ESP_OK;
}

static esp_err_t log_get_handler(httpd_req_t *req) {
    if (!log_redirect_is_enabled()) {
        httpd_resp_set_status(req, "503 SERVICE UNAVAILABLE");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"enabled\":false,\"message\":\"Log capture is disabled\"}");
        return ESP_OK;
    }

    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    uint64_t from_index = 0;

    if (query_len > 1) {
        if (query_len > 64) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query too long");
            return ESP_FAIL;
        }
        char query[64] = {0};
        if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
            char value[32] = {0};
            if (httpd_query_key_value(query, "from_index", value, sizeof(value)) == ESP_OK) {
                char *endptr = NULL;
                unsigned long long parsed = strtoull(value, &endptr, 10);
                if (!endptr || *endptr != '\0') {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid from_index");
                    return ESP_FAIL;
                }
                from_index = (uint64_t)parsed;
            }
        }
    }

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate JSON");
        return ESP_FAIL;
    }

    log_json_ctx_t ctx = {.array = array, .error = false};
    uint64_t last_index = 0;
    esp_err_t res = log_redirect_consume(from_index, append_log_entry, &ctx, &last_index);

    if (res != ESP_OK || ctx.error) {
        cJSON_Delete(array);
        if (res == ESP_ERR_INVALID_STATE) {
            httpd_resp_set_status(req, "503 SERVICE UNAVAILABLE");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"enabled\":false,\"message\":\"Log buffer unavailable\"}");
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to collect logs");
        }
        return ESP_FAIL;
    }

    char index_header[32];
    snprintf(index_header, sizeof(index_header), "%llu", (unsigned long long)log_redirect_get_next_index());
    httpd_resp_set_hdr(req, "X-Log-Next-Index", index_header);

    char oldest_header[32];
    snprintf(oldest_header, sizeof(oldest_header), "%llu", (unsigned long long)log_redirect_get_oldest_index());
    httpd_resp_set_hdr(req, "X-Log-Oldest-Index", oldest_header);

    char *json_str = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);

    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize logs");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    return ESP_OK;
}

void register_log_web_handlers(httpd_handle_t server) {
    const webserver_uri_t log_handlers[] = {
        {.uri = "/api/log", .method = HTTP_GET, .handler = log_get_handler, .require_auth = true},
    };

    for (int i = 0; i < sizeof(log_handlers) / sizeof(log_handlers[0]); i++) {
        webserver_register_uri_handler(server, &log_handlers[i]);
    }
}
