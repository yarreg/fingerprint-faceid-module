#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_tls_crypto.h"
#include "webserver.h"
#include "static.h"


static const char *TAG = "WebServer";

// Auth info structure
typedef struct {
    const char *username;
    const char *password;
} auth_info_t;

typedef struct {
    void *user_ctx;         // Original user context
    esp_err_t (*orig_handler)(httpd_req_t *r); // Original handler
    bool require_auth;      // Handler configuration
} handler_wrapper_ctx_t;

static auth_info_t g_auth_info = {0};

// Forward declarations
static esp_err_t basic_auth_middleware(httpd_req_t *req);

static char *http_auth_basic(const char *username, const char *password) {
    size_t out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    int rc = asprintf(&user_info, "%s:%s", username, password);
    if (rc < 0) {
        ESP_LOGE(TAG, "asprintf() returned: %d", rc);
        return NULL;
    }

    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, &out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

static esp_err_t uri_handler_wrapper(httpd_req_t *req) {
    handler_wrapper_ctx_t *wrapper_ctx = (handler_wrapper_ctx_t *)req->user_ctx;

    // Check authentication if required
    if (wrapper_ctx->require_auth) {
        esp_err_t ret = basic_auth_middleware(req);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // Set the original user context for the handler
    req->user_ctx = wrapper_ctx->user_ctx;

    // Call the original handler through the wrapper context
    return wrapper_ctx->orig_handler(req);
}

void webserver_register_uri_handler(httpd_handle_t server, const webserver_uri_t *config) {
    // Create wrapper context
    handler_wrapper_ctx_t *wrapper_ctx = malloc(sizeof(handler_wrapper_ctx_t));
    if (wrapper_ctx) {
        wrapper_ctx->user_ctx = config->user_ctx;
        wrapper_ctx->require_auth = config->require_auth;
        wrapper_ctx->orig_handler = config->handler;
    }

    httpd_uri_t uri = {
        .uri = config->uri,
        .method = config->method,
        .handler = uri_handler_wrapper,
        .user_ctx = wrapper_ctx
    };
    httpd_register_uri_handler(server, &uri);
}

static esp_err_t basic_auth_middleware(httpd_req_t *req)
{
    if (!g_auth_info.username || !g_auth_info.password) {
        return ESP_OK; // No auth required if credentials not set
    }

    char *buf = NULL;
    size_t buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            char *auth_credentials = http_auth_basic(g_auth_info.username, g_auth_info.password);
            if (!auth_credentials) {
                ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
                free(buf);
                return ESP_ERR_NO_MEM;
            }

            if (strlen(auth_credentials) != strlen(buf) || strcmp(auth_credentials, buf) != 0) {
                ESP_LOGE(TAG, "Not authenticated");
                httpd_resp_set_status(req, "401 UNAUTHORIZED");
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Access Control\"");
                httpd_resp_send(req, NULL, 0);
                free(auth_credentials);
                free(buf);
                return ESP_FAIL;
            }
            free(auth_credentials);
        }
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, "401 UNAUTHORIZED");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Access Control\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Credentials management endpoint

void webserver_set_auth(const char *username, const char *password) {
    g_auth_info.username = username;
    g_auth_info.password = password;
}

// Ping handler
static esp_err_t ping_handler(httpd_req_t *req) {
    const char *resp = "pong";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

httpd_handle_t webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 100;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register ping handler
        static const webserver_uri_t uri_ping_handler = {
            .uri = "/ping",
            .method = HTTP_GET,
            .handler = ping_handler,
            .user_ctx = NULL,
            .require_auth = false
        };
        webserver_register_uri_handler(server, &uri_ping_handler);

        ESP_LOGI(TAG, "Web server started");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }

    return server;
}
