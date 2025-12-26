#ifndef _WEB_HANDLERS_H_
#define _WEB_HANDLERS_H_

#include "esp_http_server.h"
#include "tabledb.h"

void register_config_web_handlers(httpd_handle_t server);
void register_enrollment_web_handlers(httpd_handle_t server, tabledb_config_t *face_config, tabledb_config_t *fingerprint_config);
void register_photo_web_handlers(httpd_handle_t server);
void register_system_web_handlers(httpd_handle_t server);
void register_settings_web_handlers(httpd_handle_t server);
void register_ota_web_handlers(httpd_handle_t server);
void register_log_web_handlers(httpd_handle_t server);
void register_static_web_handlers(httpd_handle_t server);

#endif /* _WEB_HANDLERS_H_ */
