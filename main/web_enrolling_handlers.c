#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_log.h"
#include "esp_err.h"
#include "webserver.h"
#include "cJSON.h"
#include "r502.h"
#include "f900.h"
#include "buzzer.h"
#include "tabledb.h"
#include "table_types.h"

static const char *TAG = "ENROLL_HANDLERS";

static tabledb_config_t *table_fingerprint_config;
static tabledb_config_t *table_face_config;

typedef enum {
    ENROLLING_TYPE_FINGERPRINT,
    ENROLLING_TYPE_FACE
} enrolling_type_t;


// Enrollment state tracking
typedef struct {
    bool active;
    uint8_t step;
    uint16_t user_id;
    char user_name[32];
    enrolling_type_t type;
} enrollment_state_t;

static enrollment_state_t current_enrollment = {0};

static bool wait_for_finger_state(bool want_present, int max_retries, int delay_ms, r502_generic_reply *sensor_reply) {
    for (int i = 0; i < max_retries; i++) {
        esp_err_t err = r502_genimg(sensor_reply);
        if (err == ESP_OK) {
            if (want_present && sensor_reply->conf_code == 0x00) {
                return true; // finger detected
            }
            if (!want_present && sensor_reply->conf_code == 0x02) {
                return true; // finger removed
            }
            if (sensor_reply->conf_code != 0x00 && sensor_reply->conf_code != 0x02) {
                ESP_LOGW(TAG, "GenImg unexpected code 0x%02X while waiting for %s", sensor_reply->conf_code,
                         want_present ? "finger" : "removal");
            }
        } else {
            ESP_LOGW(TAG, "GenImg failed while waiting for %s: %s", want_present ? "finger" : "removal", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return false;
}

static void send_error_response(httpd_req_t *req, httpd_err_code_t status, const char *code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "code", code);
        cJSON_AddStringToObject(root, "message", message);
        char *json_str = cJSON_PrintUnformatted(root);
        switch (status) {
            case HTTPD_400_BAD_REQUEST:
                httpd_resp_set_status(req, "400 BAD REQUEST");
                break;
            case HTTPD_404_NOT_FOUND:
                httpd_resp_set_status(req, "404 NOT FOUND");
                break;
            case HTTPD_500_INTERNAL_SERVER_ERROR:
                httpd_resp_set_status(req, "500 INTERNAL SERVER ERROR");
                break;
            default:
                httpd_resp_set_status(req, "400 BAD REQUEST");
                break;
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_str);
        cJSON_free(json_str);
        cJSON_Delete(root);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create error response");
    }
}

static void enroll_face_task(void *arg) {
    enrollment_state_t *enroll = (enrollment_state_t *)arg;
    esp_err_t err = ESP_OK;
    f900_user_info_t user_info;
    uint16_t user_id = 0;

    // Initialize face enrollment
    f900_enroll_data_t enroll_data = {
        .admin = 0,
        .timeout = 10
    };
    strncpy((char*)enroll_data.user_name, enroll->user_name, F900_USER_NAME_SIZE-1);

    const f900_face_dir_t directions[] = {
        FACE_DIRECTION_MIDDLE,
        FACE_DIRECTION_UP,
        FACE_DIRECTION_DOWN,
        FACE_DIRECTION_LEFT,
        FACE_DIRECTION_RIGHT
    };

    // Enroll each direction
    for (enroll->step = 0; enroll->step < 5; enroll->step++) {
        enroll_data.face_direction = directions[enroll->step];
        ESP_LOGI(TAG, "Enrolling face direction %d", enroll->step);
        buzzer_short_beep();

        if (!f900_enroll(&enroll_data, &user_id)) {
            ESP_LOGE(TAG, "Face enrollment failed at step %d", enroll->step);
            f900_face_reset();
            err = ESP_FAIL;
            goto error;
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Short delay between directions
    }

    // Save to database
    table_face_t face_data = {
        .used_count = 0,
        .last_usage_time = 0
    };
    strncpy(face_data.name, enroll->user_name, sizeof(face_data.name));

    if (tabledb_insert(table_face_config, user_id, &face_data) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save face to database");
        goto error;
    }

    ESP_LOGI(TAG, "Face enrolled successfully for %s (ID: %d)", enroll->user_name, user_id);
    buzzer_success_chime();

    enroll->step = 5; // mark completion so remaining_steps reports 0

error:
    if (err != ESP_OK) {
        buzzer_error_honk();
        ESP_LOGE(TAG, "Face enrollment failed: %d", err);
    }

    // Cleanup
    memset(enroll, 0, sizeof(enrollment_state_t));
    vTaskDelete(NULL);
}

static void enroll_fingerprint_task(void *arg) {
    enrollment_state_t *enroll = (enrollment_state_t *)arg;
    esp_err_t err;
    r502_generic_reply sensor_reply;

    // Get next available template index for ID
    r502_templatenum_reply templatenum_reply;
    err = r502_templatenum(&templatenum_reply);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TemplateNum command failed");
        goto error;
    }
    uint16_t next_index = templatenum_reply.index;

    // Scan loop for two fingerprints
    // Step 1
    enroll->step = 0;
    ESP_LOGI(TAG, "Place finger for first scan");

    // Start enrollment - breathing (1) blue
    r502_auraledconfig(1, 100, 2, 0, &sensor_reply);
    buzzer_short_beep();

    // Ensure finger present
    if (!wait_for_finger_state(true, 20, 200, &sensor_reply)) {
        ESP_LOGE(TAG, "Finger detection timeout");
        err = ESP_ERR_TIMEOUT;
        goto error;
    }

    // Convert to template with buffer ID
    err = r502_img2tz(1, &sensor_reply);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Img2Tz failed for scan 1");
        goto error;
    }

    // Step 2
    enroll->step = 1;
    // Require finger removal before next step (or finish)
    ESP_LOGI(TAG, "Remove finger after scan");
    r502_auraledconfig(4, 0, 0, 0, &sensor_reply); // off light
    buzzer_short_beep();

    if (!wait_for_finger_state(false, 20, 200, &sensor_reply)) {
        ESP_LOGE(TAG, "Finger removal timeout after scan 2");
        err = ESP_ERR_TIMEOUT;
        goto error;
    }

    ESP_LOGI(TAG, "Place finger for second scan");
    r502_auraledconfig(1, 100, 3, 0, &sensor_reply); // purple light
    // Ensure finger removed before starting
    if (!wait_for_finger_state(true, 20, 200, &sensor_reply)) {
        ESP_LOGE(TAG, "Finger detection timeout");
        err = ESP_ERR_TIMEOUT;
        goto error;
    }

    // Convert to template with buffer ID
    err = r502_img2tz(2, &sensor_reply);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Img2Tz failed for scan 2");
        goto error;
    }


    // Save to sensor and database
    // Create template model
    err = r502_regmodel(&sensor_reply);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RegModel failed");
        goto error;
    }

    // Store template
    err = r502_store(1, next_index, &sensor_reply);
    if (err  != ESP_OK) {
        ESP_LOGE(TAG, "Store failed");
        goto error;
    }

    // Save to database
    table_fingerprint_t fp_data = {
        .used_count = 0,
        .enabled = true,
        .last_usage_time = 0
    };
    strncpy(fp_data.name, enroll->user_name, sizeof(fp_data.name));

    err = tabledb_insert(table_fingerprint_config, next_index, &fp_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save to database: %s", esp_err_to_name(err));
        goto error;
    }

    ESP_LOGI(TAG, "Fingerprint enrolled successfully for %s (ID: %d)", enroll->user_name, next_index);
    buzzer_success_chime();

error:
    if (err != ESP_OK) {
        TickType_t error_tick_start = xTaskGetTickCount();

        // On error - red (1) breathing light
        r502_auraledconfig(1, 100, 1, 0, &sensor_reply);

        // Play error sound first
        buzzer_error_honk();

        // Calculate remaining delay to ensure total 2 seconds
        TickType_t elapsed_ticks = xTaskGetTickCount() - error_tick_start;
        uint32_t elapsed_ms = elapsed_ticks * portTICK_PERIOD_MS;
        if (elapsed_ms < 2000) {
            vTaskDelay(pdMS_TO_TICKS(2000 - elapsed_ms));
        }
        ESP_LOGE(TAG, "Enrollment failed: %d", err);
    }

    // Cleanup - turn off LED
    r502_auraledconfig(4, 0, 0, 0, &sensor_reply);
    memset(enroll, 0, sizeof(enrollment_state_t));
    vTaskDelete(NULL);
}

static esp_err_t start_enrollment_handler(httpd_req_t *req) {
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content));
    if (received <= 0) {
        return ESP_FAIL;
    }
    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_json", "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *type = cJSON_GetObjectItem(root, "type");
    const cJSON *name = cJSON_GetObjectItem(root, "user_name");

    if (!type || !name) {
        cJSON_Delete(root);
        send_error_response(req, HTTPD_400_BAD_REQUEST, "missing_fields", "Required fields are missing");
        return ESP_FAIL;
    }

    if (current_enrollment.active) {
        cJSON_Delete(root);
        send_error_response(req, HTTPD_400_BAD_REQUEST, "enrollment_in_progress", "Another enrollment is already in progress");
        return ESP_FAIL;
    }

    memset(&current_enrollment, 0, sizeof(enrollment_state_t));
    current_enrollment.active = true;

    char type_str[16] = {0};
    strncpy(type_str, type->valuestring, sizeof(type_str) - 1);
    strncpy(current_enrollment.user_name, name->valuestring, sizeof(current_enrollment.user_name) - 1);
    char name_str[sizeof(current_enrollment.user_name)] = {0};
    strncpy(name_str, current_enrollment.user_name, sizeof(name_str) - 1);

    if (strcmp(type_str, "fingerprint") == 0) {
        current_enrollment.type = ENROLLING_TYPE_FINGERPRINT;

        // Start enrollment task
        xTaskCreate(enroll_fingerprint_task, "enroll_fingerprint_task", 4096,  &current_enrollment, 5, NULL);
    } else if (strcmp(type_str, "face") == 0) {
        current_enrollment.type = ENROLLING_TYPE_FACE;
        buzzer_short_beep();
        // Start face enrollment task
        xTaskCreate(enroll_face_task, "enroll_face_task", 4096, &current_enrollment, 5, NULL);
    } else {
        cJSON_Delete(root);
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_type", "Invalid enrollment type specified");
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", type_str);
    cJSON_AddStringToObject(response, "user_name", name_str);

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str ? json_str : "{}");

    if (json_str) {
        cJSON_free(json_str);
    }
    cJSON_Delete(response);
    return ESP_OK;
}

static esp_err_t get_enrollment_status_handler(httpd_req_t *req) {
    cJSON *response = cJSON_CreateObject();

    if (!current_enrollment.active) {
        cJSON_AddNullToObject(response, "type");
    } else {
        cJSON_AddStringToObject(response, "type",
            current_enrollment.type == ENROLLING_TYPE_FINGERPRINT ? "fingerprint" : "face");
        cJSON_AddStringToObject(response, "user_name", current_enrollment.user_name);

        cJSON *status = cJSON_CreateObject();
        if (current_enrollment.type == ENROLLING_TYPE_FINGERPRINT) {
            cJSON_AddStringToObject(status, "current_step",
                current_enrollment.step == 0 ? "scan_1" : "scan_2");
            cJSON_AddNumberToObject(status, "passed_steps", current_enrollment.step);
            cJSON_AddNumberToObject(status, "remaining_steps", 2 - current_enrollment.step);
        } else {
            const char *steps[] = {"direction_middle", "direction_up", "direction_down",
                                  "direction_left", "direction_right"};
            cJSON *passed = cJSON_CreateArray();
            for (int i=0; i<current_enrollment.step; i++) {
                cJSON_AddItemToArray(passed, cJSON_CreateString(steps[i]));
            }
            cJSON_AddItemToObject(status, "passed_steps", passed);

            cJSON *remaining = cJSON_CreateArray();
            for (int i=current_enrollment.step; i<5; i++) {
                cJSON_AddItemToArray(remaining, cJSON_CreateString(steps[i]));
            }
            cJSON_AddItemToObject(status, "remaining_steps", remaining);
            cJSON_AddStringToObject(status, "current_step",
                current_enrollment.step < 5 ? steps[current_enrollment.step] : "complete");
        }
        if (current_enrollment.type == ENROLLING_TYPE_FINGERPRINT) {
            cJSON_AddItemToObject(response, "fingerprint_enroll_status", status);
        } else {
            cJSON_AddItemToObject(response, "face_enroll_status", status);
        }
    }

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

static esp_err_t cancel_enrollment_handler(httpd_req_t *req) {
    if (!current_enrollment.active) {
        send_error_response(req, HTTPD_404_NOT_FOUND, "no_active_enrollment", "No active enrollment to cancel");
        return ESP_FAIL;
    }

    if (current_enrollment.type == ENROLLING_TYPE_FINGERPRINT) {
        // Cancel fingerprint enrollment
        // TODO: This will reset the sensor state
    } else {
        // TODO: Reset face sensor state
    }

    memset(&current_enrollment, 0, sizeof(enrollment_state_t));
    buzzer_long_beep();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "message", "Enrollment canceled");

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    cJSON_free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// Updated helper: extract enrollment type as enrolling_type_t
static esp_err_t extract_enrollment_type(httpd_req_t *req, enrolling_type_t *etype) {
    char type_str[16];
    int n = sscanf(req->uri, "/api/enrollments/%15s", type_str);
    if(n != 1)
        return ESP_FAIL;
    char *slash = strchr(type_str, '/');
    if(slash)
        *slash = '\0';
    if(strcmp(type_str, "fingerprint") == 0)
        *etype = ENROLLING_TYPE_FINGERPRINT;
    else if(strcmp(type_str, "face") == 0)
        *etype = ENROLLING_TYPE_FACE;
    else
        return ESP_FAIL;
    return ESP_OK;
}

// New helper: extract enrollment id from URI (expected in "/api/enrollments/{type}/{id}")
static esp_err_t extract_enrollment_id(httpd_req_t *req, uint32_t *id) {
    int n = sscanf(req->uri, "/api/enrollments/%*[^/]/%" PRIu32, id);
    return (n == 1) ? ESP_OK : ESP_FAIL;
}

// Updated handler for updating enrollment enabled field, using tabledb_get and tabledb_update.
static esp_err_t update_enrollment_enabled_handler(httpd_req_t *req) {
    enrolling_type_t etype;
    uint32_t id;
    if (extract_enrollment_type(req, &etype) != ESP_OK ||
        extract_enrollment_id(req, &id) != ESP_OK) {
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_uri", "Invalid URI format or enrollment type");
        return ESP_FAIL;
    }

    char buf[255];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        send_error_response(req, HTTPD_400_BAD_REQUEST, "empty_body", "Empty request body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_json", "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
    if (!cJSON_IsBool(enabled_item)) {
        cJSON_Delete(root);
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_enabled", "Enabled field must be a boolean");
        return ESP_FAIL;
    }
    bool enabled = cJSON_IsTrue(enabled_item);
    cJSON_Delete(root);
    esp_err_t result;
    if (etype == ENROLLING_TYPE_FINGERPRINT) {
        table_fingerprint_t record;
        result = tabledb_get(table_fingerprint_config, id, &record);
        if (result != ESP_OK) {
            send_error_response(req, HTTPD_404_NOT_FOUND, "record_not_found", "Fingerprint record not found");
            return result;
        }
        record.enabled = enabled;
        result = tabledb_update(table_fingerprint_config, id, &record);
    } else { // ENROLLING_TYPE_FACE
        table_face_t record;
        result = tabledb_get(table_face_config, id, &record);
        if (result != ESP_OK) {
            send_error_response(req, HTTPD_404_NOT_FOUND, "record_not_found", "Face record not found");
            return result;
        }
        record.enabled = enabled;
        result = tabledb_update(table_face_config, id, &record);
    }
    if (result != ESP_OK) {
        send_error_response(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal_error", "Failed to update enrollment record");
        return ESP_FAIL;
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "message", "Enrollment updated");
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// Handler for DELETE /api/enrollments/{fingerprint|face}/{id}
// This handler deletes a specific enrollment record by ID and all
static esp_err_t delete_enrollment_handler(httpd_req_t *req) {
    enrolling_type_t etype;
    uint32_t id;
    esp_err_t id_status = extract_enrollment_id(req, &id);
    if (extract_enrollment_type(req, &etype) != ESP_OK) {
        send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_uri", "Invalid URI format");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();

    if (id_status == ESP_OK) {
        // Specific record deletion
        ESP_LOGI(TAG, "Deleting specific record with id %" PRIu32, id);
        if (etype == ENROLLING_TYPE_FINGERPRINT) {
            esp_err_t table_del_result = tabledb_delete(table_fingerprint_config, id);
            r502_generic_reply sensor_reply;
            esp_err_t sensor_del_result = r502_deletechar(id, 1, &sensor_reply);
            bool sensor_ok = (sensor_del_result == ESP_OK && sensor_reply.conf_code == 0);
            bool table_ok = (table_del_result == ESP_OK);
            if (sensor_ok && table_ok) {
                cJSON_AddBoolToObject(response, "ok", true);
                cJSON_AddStringToObject(response, "message", "Deleted fingerprint record");
            } else {
                send_error_response(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal_error", "Failed to delete fingerprint record");
                cJSON_Delete(response);
                return ESP_FAIL;
            }
        } else { // ENROLLING_TYPE_FACE
            esp_err_t table_del_result = tabledb_delete(table_face_config, id);
            bool sensor_del_result = f900_delete_user(id);
            if (sensor_del_result && table_del_result == ESP_OK) {
                cJSON_AddBoolToObject(response, "ok", true);
                cJSON_AddStringToObject(response, "message", "Deleted face record");
            } else {
                send_error_response(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal_error", "Failed to delete face record");
                cJSON_Delete(response);
                return ESP_FAIL;
            }
        }
    } else {
        // Deletion of all records for the type
        ESP_LOGI(TAG, "Deleting all records of type %s",
            etype == ENROLLING_TYPE_FINGERPRINT ? "fingerprint" : "face");
        if (etype == ENROLLING_TYPE_FINGERPRINT) {
            r502_generic_reply sensor_reply;
            esp_err_t sensor_result = r502_empty(&sensor_reply);
            esp_err_t table_result = tabledb_drop(table_fingerprint_config);
            bool sensor_ok = (sensor_result == ESP_OK && sensor_reply.conf_code == 0);
            bool table_ok = (table_result == ESP_OK);
            if (sensor_ok && table_ok) {
                cJSON_AddBoolToObject(response, "ok", true);
                cJSON_AddStringToObject(response, "message", "Cleared all fingerprint records");
            } else {
                send_error_response(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal_error", "Failed to clear all fingerprints");
                cJSON_Delete(response);
                return ESP_FAIL;
            }
        } else { // ENROLLING_TYPE_FACE
            bool sensor_result = f900_delete_all_users();
            esp_err_t table_result = tabledb_drop(table_face_config);
            if (sensor_result && table_result == ESP_OK) {
                cJSON_AddBoolToObject(response, "ok", true);
                cJSON_AddStringToObject(response, "message", "Cleared all face records");
            } else {
                send_error_response(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal_error", "Failed to clear all face records");
                cJSON_Delete(response);
                return ESP_FAIL;
            }
        }
    }

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// Handler for GET /api/enrollments/{fingerprint|face}
static esp_err_t list_enrollments_handler(httpd_req_t *req) {
    char type[16] = {0};
    if (sscanf(req->uri, "/api/enrollments/%15s", type) != 1) {
         send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_uri", "Invalid URI format");
         return ESP_FAIL;
    }
    // Remove any trailing slash if present
    char *slash = strchr(type, '/');
    if (slash) *slash = '\0';

    tabledb_config_t *config = NULL;
    if (strcmp(type, "fingerprint") == 0) {
        config = table_fingerprint_config;
    } else if (strcmp(type, "face") == 0) {
        config = table_face_config;
    } else {
         send_error_response(req, HTTPD_400_BAD_REQUEST, "invalid_type", "Invalid enrollment type specified");
         return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{ \"items\": [");

    uint32_t last_id = 0;
    uint32_t record_id = 0;
    bool first_item = true;
    bool first_pass = true;
    while (true) {
        esp_err_t err;
        if (strcmp(type, "fingerprint") == 0) {
            table_fingerprint_t record;
            err = tabledb_get_next(config, first_pass ? 0 : last_id, &record_id, &record);
            if (err != ESP_OK) {
                break;
            }
            last_id = record_id;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", record_id);
            cJSON_AddStringToObject(item, "name", record.name);
            cJSON_AddBoolToObject(item, "enabled", record.enabled);
            cJSON_AddNumberToObject(item, "usage_count", record.used_count);
            char *item_str = cJSON_PrintUnformatted(item);
            if (!first_item) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            httpd_resp_sendstr_chunk(req, item_str);
            first_item = false;
            cJSON_free(item_str);
            cJSON_Delete(item);
        } else {
            table_face_t record;
            err = tabledb_get_next(config, first_pass ? 0 : last_id, &record_id, &record);
            if (err != ESP_OK) {
                break;
            }
            last_id = record_id;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", record_id);
            cJSON_AddStringToObject(item, "name", record.name);
            cJSON_AddBoolToObject(item, "enabled", record.enabled);
            cJSON_AddNumberToObject(item, "usage_count", record.used_count);
            char *item_str = cJSON_PrintUnformatted(item);
            if (!first_item) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            httpd_resp_sendstr_chunk(req, item_str);
            first_item = false;
            cJSON_free(item_str);
            cJSON_Delete(item);
        }
        first_pass = false;
        // If the first record has id 0, tabledb_get_next cannot advance; avoid infinite loop.
        if (last_id == 0) {
            break;
        }
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL); // finalize chunked response
    return ESP_OK;
}

void register_enrollment_web_handlers(httpd_handle_t server, tabledb_config_t *face_config, tabledb_config_t *fingerprint_config) {
    table_fingerprint_config = fingerprint_config;
    table_face_config = face_config;

    const webserver_uri_t enrollment_handlers[] = {
        {.uri = "/api/enrollment",    .method = HTTP_POST,   .handler = start_enrollment_handler,             .require_auth = true},
        {.uri = "/api/enrollment",    .method = HTTP_GET,    .handler = get_enrollment_status_handler,          .require_auth = true},
        {.uri = "/api/enrollment",    .method = HTTP_DELETE, .handler = cancel_enrollment_handler,              .require_auth = true},
        {.uri = "/api/enrollments/*", .method = HTTP_POST,   .handler = update_enrollment_enabled_handler,      .require_auth = true},
        {.uri = "/api/enrollments/*", .method = HTTP_DELETE, .handler = delete_enrollment_handler,      .require_auth = true},
        {.uri = "/api/enrollments/*", .method = HTTP_GET,    .handler = list_enrollments_handler,               .require_auth = true}
    };

    for (int i = 0; i < sizeof(enrollment_handlers)/sizeof(enrollment_handlers[0]); i++) {
        webserver_register_uri_handler(server, &enrollment_handlers[i]);
    }
}
