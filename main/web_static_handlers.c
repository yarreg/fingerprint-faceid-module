#include "webserver.h"
#include "esp_log.h"
#include "static.h"


const static char *TAG = "WebStaticHandlers";

static const char *get_content_type(const char *file_path) {
    const char *ext = strrchr(file_path, '.');
    if (ext == NULL) {
        return "text/plain";
    }

    if (strcmp(ext, ".html") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }

    return "text/plain";
}

// Static file handler
static esp_err_t static_file_handler(httpd_req_t *req) {
    const char *path = req->uri + 1; // Skip leading '/'
    const char *file_name = path[0] ? path : "index.html";

    size_t size;
    const char *file_data = get_static_file(file_name, &size);
    ESP_LOGI(TAG, "Requesting file: %s", file_name);

    if (!file_data) {
        ESP_LOGW(TAG, "File not found: %s", file_name);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(file_name));
    httpd_resp_send(req, file_data, size);
    return ESP_OK;
}

void register_static_web_handlers(httpd_handle_t server) {
    static const webserver_uri_t static_file_handler_config = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL,
        .require_auth = false
    };
    webserver_register_uri_handler(server, &static_file_handler_config);
}
