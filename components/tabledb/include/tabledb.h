#ifndef _TABLEDB_H_
#define _TABLEDB_H_ 

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>  // For PRIu32 macro
#include "nvs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Maximum size of an object that can be stored in the tabledb.
#define TABLEDB_MAX_OBJECT_SIZE 512

typedef struct {
    uint32_t id;
    uint8_t version;
    uint16_t size;
    char next_key[16]; // NVS key for the next record; empty if none.
    char prev_key[16]; // NVS key for the previous record; empty if none.
} tabledb_internal_record_t;

// Metadata structure stored in a dedicated NVS blob _meta_<table name>
typedef struct {
    uint32_t count;
    char head_key[16]; // NVS key of the first record in the list.
} tabledb_meta_t;

// Update callback. This callback will be called when the version of the data stored in NVS.
// If function return != ESP_OK caller should return error
typedef esp_err_t (*tabledb_upgrade_cb)(uint8_t old_version, const void* old_data, void* new_data);

typedef struct {
    nvs_handle handle;
    const uint32_t size;
    const char* namespace;
    // Version of the data stored in NVS
    uint8_t version;
    // If version is different from the one stored in NVS, this callback will be called
    // to upgrade the data to the data.
    tabledb_upgrade_cb update_cb;
    SemaphoreHandle_t mutex;  // Mutex for thread-safe operations (must be initialized)
} tabledb_config_t;


esp_err_t tabledb_init(tabledb_config_t *config);
esp_err_t tabledb_upgrade(tabledb_config_t *config);
esp_err_t tabledb_insert(tabledb_config_t *config, uint32_t id, void *data);
esp_err_t tabledb_delete(tabledb_config_t *config, uint32_t id);
esp_err_t tabledb_drop(tabledb_config_t *config);
esp_err_t tabledb_get(tabledb_config_t *config, uint32_t id, void *data);
esp_err_t tabledb_get_next(tabledb_config_t *config, uint32_t id, uint32_t *data_id, void *data);
esp_err_t tabledb_get_count(tabledb_config_t *config, size_t *count);
esp_err_t tabledb_update(tabledb_config_t *config, uint32_t id, void *data);

#endif /* _TABLEDB_H_ */
