#include "tabledb.h"
#include "nvs_flash.h"
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "nvs.h"
#include "esp_log.h"

#define EXIT_WITH_MUTEX(expr)                                                                      \
    do {                                                                                           \
        xSemaphoreGive(config->mutex);                                                             \
        return expr;                                                                               \
    } while (0)

static const char *TAG = "TABLE_DB";

static esp_err_t tabledb_rollback(tabledb_config_t *config) {
    nvs_close(config->handle);

    esp_err_t err = nvs_open(config->namespace, NVS_READWRITE, &config->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed");
    }
    return err;
}

// Helper: Load metadata blob from NVS (key: _meta_<namespace>)
static esp_err_t load_meta(tabledb_config_t *config, tabledb_meta_t *meta) {
    char meta_key[15];
    snprintf(meta_key, sizeof(meta_key), "_meta_%s", config->namespace);
    size_t    size = sizeof(tabledb_meta_t);
    esp_err_t err  = nvs_get_blob(config->handle, meta_key, meta, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        meta->count       = 0;
        meta->head_key[0] = '\0';
        return ESP_OK;
    }
    return err;
}

// Helper: Save metadata blob to NVS.
static esp_err_t save_meta(tabledb_config_t *config, tabledb_meta_t *meta) {
    char meta_key[15];
    snprintf(meta_key, sizeof(meta_key), "_meta_%s", config->namespace);
    return nvs_set_blob(config->handle, meta_key, meta, sizeof(tabledb_meta_t));
}


// Initialize the tabledb library
/*
 * @brief Initialize the table database.
 *
 * This function opens the NVS namespace specified in the configuration and prepares the table database for operations.
 *
 * @param config Pointer to the table database configuration structure.
 *
 * @return
 *    - ESP_OK: Success.
 *    - ESP_ERR_INVALID_ARG: Null pointer or invalid arguments.
 *    - Other error codes from nvs_open.
 */
esp_err_t tabledb_init(tabledb_config_t *config) {
    if (config->namespace == NULL || config->version == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->size > TABLEDB_MAX_OBJECT_SIZE) {
        ESP_LOGE(TAG, "Data size is too large");
        return ESP_ERR_INVALID_SIZE;
    }

    // Create mutex (initialie)
    config->mutex = xSemaphoreCreateMutex();
    if (config->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_open(config->namespace, NVS_READWRITE, &config->handle);
    if (err != ESP_OK) {
        vSemaphoreDelete(config->mutex);
        return err;
    }

    return err;
}

// Function traverses the linked list and call the update callback for each record
// then updates the version of the record.
esp_err_t tabledb_upgrade(tabledb_config_t *config) {
    if (xSemaphoreTake(config->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    tabledb_meta_t meta;
    esp_err_t      err = load_meta(config, &meta);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    char cur_key[15];
    strncpy(cur_key, meta.head_key, sizeof(cur_key));

    // Traverse the linked list
    while (cur_key[0] != '\0') {
        size_t blob_size;
        err = nvs_get_blob(config->handle, cur_key, NULL, &blob_size);
        if (err != ESP_OK) {
            EXIT_WITH_MUTEX(err);
        }

        // If stored blob version is smaller than new version, lets use the new version
        // if stored blob size is bigger than the new size, use blob_size
        if (blob_size < sizeof(tabledb_internal_record_t) + config->size) {
            blob_size = sizeof(tabledb_internal_record_t) + config->size;
        }

        uint8_t buffer[blob_size];
        err = nvs_get_blob(config->handle, cur_key, buffer, &blob_size);
        if (err != ESP_OK) {
            EXIT_WITH_MUTEX(err);
        }

        tabledb_internal_record_t *record = (tabledb_internal_record_t *) buffer;
        // Save next key before updating current record.
        char next_key[15];
        strncpy(next_key, record->next_key, sizeof(next_key));

        // Check if upgrade is needed.
        if (record->version != config->version) {
            if (config->update_cb == NULL) {
                EXIT_WITH_MUTEX(ESP_ERR_INVALID_VERSION);
            }
            // Temporary buffer for updated payload.
            uint8_t  new_payload[config->size];
            uint8_t *old_payload = buffer + sizeof(tabledb_internal_record_t);
            err                  = config->update_cb(record->version, old_payload, new_payload);
            if (err != ESP_OK) {
                EXIT_WITH_MUTEX(err);
            }
            memcpy(old_payload, new_payload, config->size);
            record->version = config->version;

            err = nvs_set_blob(config->handle, cur_key, buffer, sizeof(buffer));
            if (err != ESP_OK) {
                tabledb_rollback(config);
                EXIT_WITH_MUTEX(err);
            }
        }
        // Move to the next record
        strncpy(cur_key, next_key, sizeof(cur_key));
    }

    err = nvs_commit(config->handle);
    EXIT_WITH_MUTEX(err);
}

/***************** tabledb_insert ******************/
/*
 * Insert a new record using linked-list mechanism.
 */
esp_err_t tabledb_insert(tabledb_config_t *config, uint32_t id, void *data) {
    if (xSemaphoreTake(config->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char key[15];
    snprintf(key, sizeof(key), "rec_%" PRIu32, id);

    size_t    blob_size;
    esp_err_t err = nvs_get_blob(config->handle, key, NULL, &blob_size);
    if (err == ESP_OK) {
        EXIT_WITH_MUTEX(ESP_ERR_INVALID_STATE);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        EXIT_WITH_MUTEX(err);
    }

    tabledb_meta_t meta;
    err = load_meta(config, &meta);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    // Prepare new record with next pointer = current head and prev pointer empty.
    tabledb_internal_record_t record = {.id = id, .version = config->version, .size = config->size};
    strncpy(record.next_key, meta.head_key, sizeof(record.next_key));
    record.prev_key[0] = '\0';

    uint8_t buffer[sizeof(tabledb_internal_record_t) + config->size];
    memcpy(buffer, &record, sizeof(tabledb_internal_record_t));
    memcpy(buffer + sizeof(tabledb_internal_record_t), data, config->size);

    err = nvs_set_blob(config->handle, key, buffer, sizeof(buffer));
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    // If list is not empty, update previous head's prev_key to new record.
    if (meta.head_key[0] != '\0') {
        size_t  head_size;
        err = nvs_get_blob(config->handle, meta.head_key, NULL, &head_size);
        if (err != ESP_OK) {
            tabledb_rollback(config);
            EXIT_WITH_MUTEX(err);
        }

        uint8_t head_buffer[head_size];
        err = nvs_get_blob(config->handle, meta.head_key, head_buffer, &head_size);
        if (err == ESP_OK) {
            tabledb_internal_record_t *head_record = (tabledb_internal_record_t *) head_buffer;
            strncpy(head_record->prev_key, key, sizeof(head_record->prev_key));
            err = nvs_set_blob(config->handle, meta.head_key, head_buffer, head_size);
            if (err != ESP_OK) {
                tabledb_rollback(config);
                EXIT_WITH_MUTEX(err);
            }
        }
    }

    // Update meta: new record becomes head; count++.
    strncpy(meta.head_key, key, sizeof(meta.head_key));
    meta.count++;
    err = save_meta(config, &meta);
    if (err != ESP_OK) {
        tabledb_rollback(config);
        EXIT_WITH_MUTEX(err);
    }

    err = nvs_commit(config->handle);
    EXIT_WITH_MUTEX(err);
}

/***************** tabledb_delete ******************/
/*
 * Delete a record from the linked list.
 */
esp_err_t tabledb_delete(tabledb_config_t *config, uint32_t id) {
    if (xSemaphoreTake(config->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    char key[15];
    snprintf(key, sizeof(key), "rec_%" PRIu32, id);

    // Load meta to update list.
    tabledb_meta_t meta;
    esp_err_t      err = load_meta(config, &meta);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    size_t rec_size;
    err = nvs_get_blob(config->handle, key, NULL, &rec_size);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    // Load record to be deleted.
    uint8_t rec_buffer[rec_size];
    err = nvs_get_blob(config->handle, key, rec_buffer, &rec_size);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }
    tabledb_internal_record_t *record = (tabledb_internal_record_t *) rec_buffer;

    // Update previous neighbor, if exists.
    if (record->prev_key[0] != '\0') {
        size_t prev_size;
        err = nvs_get_blob(config->handle, record->prev_key, NULL, &prev_size);
        if (err != ESP_OK) {
            EXIT_WITH_MUTEX(err);
        }

        uint8_t prev_buffer[prev_size];
        err = nvs_get_blob(config->handle, record->prev_key, prev_buffer, &prev_size);
        if (err != ESP_OK) {
            EXIT_WITH_MUTEX(err);
        }
        tabledb_internal_record_t *prev_record = (tabledb_internal_record_t *) prev_buffer;
        // Set previous record's next pointer to current record's next.
        strncpy(prev_record->next_key, record->next_key, sizeof(prev_record->next_key));
        err = nvs_set_blob(config->handle, record->prev_key, prev_buffer, prev_size);
        if (err != ESP_OK) {
            EXIT_WITH_MUTEX(err);
        }
    } else {
        // If no previous pointer, update meta if this record is head.
        if (strncmp(meta.head_key, key, sizeof(meta.head_key)) == 0) {
            strncpy(meta.head_key, record->next_key, sizeof(meta.head_key));
        }
    }

    // Update next neighbor, if exists.
    if (record->next_key[0] != '\0') {
        size_t  next_size;
        err = nvs_get_blob(config->handle, record->next_key, NULL, &next_size);
        if (err != ESP_OK) {
            tabledb_rollback(config);
            EXIT_WITH_MUTEX(err);
        }

        uint8_t next_buffer[next_size];
        err = nvs_get_blob(config->handle, record->next_key, next_buffer, &next_size);
        if (err != ESP_OK) {
            tabledb_rollback(config);
            EXIT_WITH_MUTEX(err);
        }

        tabledb_internal_record_t *next_record = (tabledb_internal_record_t *) next_buffer;
        // Set next record's prev pointer to current record's prev.
        strncpy(next_record->prev_key, record->prev_key, sizeof(next_record->prev_key));
        err = nvs_set_blob(config->handle, record->next_key, next_buffer, next_size);
        if (err != ESP_OK) {
            tabledb_rollback(config);
            EXIT_WITH_MUTEX(err);
        }
    }

    // Erase the record.
    err = nvs_erase_key(config->handle, key);
    if (err != ESP_OK) {
        tabledb_rollback(config);
        EXIT_WITH_MUTEX(err);
    }
    meta.count = (meta.count > 0) ? meta.count - 1 : 0;
    err        = save_meta(config, &meta);
    if (err != ESP_OK) {
        tabledb_rollback(config);
        EXIT_WITH_MUTEX(err);
    }

    err = nvs_commit(config->handle);
    EXIT_WITH_MUTEX(err);
}

/*
 * @brief Drop the entire table and delete all records.
 *
 * This function erases all records in the table by clearing the entire NVS namespace.
 *
 * @param config Pointer to the table database configuration structure.
 *
 * @return
 *    - ESP_OK: Success.
 *    - Other error codes from NVS functions.
 */
esp_err_t tabledb_drop(tabledb_config_t *config) {
    if (xSemaphoreTake(config->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = nvs_erase_all(config->handle);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    // Commit changes
    err = nvs_commit(config->handle);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    EXIT_WITH_MUTEX(ESP_OK);
}

/*
 * @brief Retrieve a record by ID.
 *
 * This function retrieves a record from the table using the specified ID. If the record's version
 * differs from the current version, the update callback will be called if provided.
 *
 * @param config Pointer to the table database configuration structure.
 * @param id Unique ID of the record to be retrieved.
 * @param data Pointer to the buffer where the data will be stored.
 *
 * @return
 *    - ESP_OK: Success.
 *    - ESP_ERR_INVALID_ARG: Null pointer or invalid arguments.
 *    - ESP_ERR_NOT_FOUND: Record with the given ID does not exist.
 *    - ESP_ERR_INVALID_VERSION: Record version mismatch and no update callback provided.
 *    - ESP_ERR_INVALID_SIZE: Provided buffer size is too small.
 *    - Other error codes from NVS functions.
 */
esp_err_t tabledb_get(tabledb_config_t *config, uint32_t id, void *data) {
    char key[15];
    snprintf(key, sizeof(key), "rec_%" PRIu32, id);

    size_t    required_size;
    esp_err_t err = nvs_get_blob(config->handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        return err;
    }

    if (required_size < sizeof(tabledb_internal_record_t)) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buffer[required_size];
    err = nvs_get_blob(config->handle, key, buffer, &required_size);
    if (err != ESP_OK) {
        return err;
    }

    tabledb_internal_record_t *record = (tabledb_internal_record_t *) buffer;
    if (record->version != config->version && config->update_cb == NULL) {
        return ESP_ERR_INVALID_VERSION;
    }

    uint8_t *payload = buffer + sizeof(tabledb_internal_record_t);
    if (record->version != config->version) {
        err = config->update_cb(record->version, payload, data);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        memcpy(data, payload, record->size);
    }

    return ESP_OK;
}

/***************** tabledb_get_next ******************/
/*
 * Get the next record in the linked list.
 * If id==0, return the head record.
 */
esp_err_t tabledb_get_next(tabledb_config_t *config, uint32_t id, uint32_t *data_id, void *data) {
    char key[15];
    if (id == 0) {
        tabledb_meta_t meta;
        esp_err_t      err = load_meta(config, &meta);
        if (err != ESP_OK) {
            return err;
        }
        if (meta.head_key[0] == '\0') {
            return ESP_ERR_NOT_FOUND;
        }
        strncpy(key, meta.head_key, sizeof(key));
    } else {
        snprintf(key, sizeof(key), "rec_%" PRIu32, id);

        size_t curr_size;
        esp_err_t err = nvs_get_blob(config->handle, key, NULL, &curr_size);
        if (err != ESP_OK) {
            return err;
        }

        uint8_t curr_buffer[curr_size];
        err  = nvs_get_blob(config->handle, key, curr_buffer, &curr_size);
        if (err != ESP_OK) {
            return err;
        }
        tabledb_internal_record_t *curr_record = (tabledb_internal_record_t *) curr_buffer;
        if (curr_record->next_key[0] == '\0') {
            return ESP_ERR_NOT_FOUND;
        }
        strncpy(key, curr_record->next_key, sizeof(key));
    }

    // Now load the next record.
    size_t    next_size;
    esp_err_t err = nvs_get_blob(config->handle, key, NULL, &next_size);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t next_buffer[next_size];
    err = nvs_get_blob(config->handle, key, next_buffer, &next_size);
    if (err != ESP_OK) {
        return err;
    }

    tabledb_internal_record_t *next_record = (tabledb_internal_record_t *) next_buffer;
    *data_id                               = next_record->id;

    // New update callback logic in tabledb_get_next:
    uint8_t *payload = next_buffer + sizeof(tabledb_internal_record_t);
    if (next_record->version != config->version) {
        if (config->update_cb) {
            err = config->update_cb(next_record->version, payload, data);
            if (err != ESP_OK) {
                return err;
            }
        } else {
            return ESP_ERR_INVALID_VERSION;
        }
    } else {
        memcpy(data, payload, config->size);
    }
    return ESP_OK;
}

/***************** tabledb_get_count ******************/
/*
 * Get the number of records from the metadata blob.
 */
esp_err_t tabledb_get_count(tabledb_config_t *config, size_t *count) {
    tabledb_meta_t meta;
    esp_err_t      err = load_meta(config, &meta);
    if (err != ESP_OK) {
        return err;
    }
    *count = meta.count;
    return ESP_OK;
}

/***************** tabledb_update ******************/
esp_err_t tabledb_update(tabledb_config_t *config, uint32_t id, void *data) {
    if (xSemaphoreTake(config->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char key[15];
    snprintf(key, sizeof(key), "rec_%" PRIu32, id);

    size_t blob_size;
    esp_err_t err = nvs_get_blob(config->handle, key, NULL, &blob_size);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    if (blob_size < config->size + sizeof(tabledb_internal_record_t)) {
        blob_size = sizeof(tabledb_internal_record_t) + config->size;
    }

    uint8_t buffer[blob_size];
    err = nvs_get_blob(config->handle, key, buffer, &blob_size);
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    tabledb_internal_record_t *record = (tabledb_internal_record_t *) buffer;

    // Update the internal values to make sure record is having the latest version and size.
    record->size    = config->size;
    record->version = config->version;

    // Copy the new data into the buffer.
    memcpy(buffer + sizeof(tabledb_internal_record_t), data, config->size);

    err = nvs_set_blob(config->handle, key, buffer, sizeof(buffer));
    if (err != ESP_OK) {
        EXIT_WITH_MUTEX(err);
    }

    err = nvs_commit(config->handle);
    EXIT_WITH_MUTEX(err);
}
