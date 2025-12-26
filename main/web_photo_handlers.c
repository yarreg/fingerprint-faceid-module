#include "webserver.h"
#include "f900.h"

static esp_err_t get_photo(httpd_req_t *req) {
    if (!f900_capture_images(1, 1)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to capture photo");
    }

    uint32_t image_size = 0;
    if (!f900_get_saved_image_size(1, &image_size)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get photo size");
    }

    char content_length_buf[16];
    snprintf(content_length_buf, sizeof(content_length_buf), "%" PRIu32, image_size);
    httpd_resp_set_hdr(req, "Content-Length", content_length_buf);
    httpd_resp_set_type(req, "image/jpeg");

    uint8_t image_data[1024];
    uint32_t offset = 0;

    while (offset < image_size) {
        uint32_t read_size = sizeof(image_data);
        if (offset + sizeof(image_data) > image_size) {
            // read last chunk
            read_size = image_size - offset;
        }

        if (!f900_get_saved_image(1, offset, read_size, image_data)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read photo data");
        }

        httpd_resp_send_chunk(req, (const char *)image_data, read_size);
        offset += read_size;
    }
    httpd_resp_send_chunk(req, NULL, 0); // terminate chunked response
    return ESP_OK;
}

void register_photo_web_handlers(httpd_handle_t server) {
    const webserver_uri_t enrollment_handlers[] = {
        {.uri = "/api/photo", .method = HTTP_GET, .handler = get_photo, .require_auth = true},
    };

    for (int i = 0; i < sizeof(enrollment_handlers)/sizeof(enrollment_handlers[0]); i++) {
        webserver_register_uri_handler(server, &enrollment_handlers[i]);
    }
}
