#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

#include "esp_http_server.h"
#
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;      // Original user context
    bool require_auth;   // Handler configuration flag
} webserver_uri_t;

/**
 * @brief Start the web server
 * 
 * @return httpd_handle_t Handle to the started server, or NULL on failure
 */
httpd_handle_t webserver_start(void);

/**
 * @brief Register a URI handler with configuration
 * 
 * @param server HTTP server handle
 * @param config Handler configuration
 */
void webserver_register_uri_handler(httpd_handle_t server, const webserver_uri_t *config);
void webserver_set_auth(const char *username, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* _WEBSERVER_H_ */
