#ifndef _LOG_REDIRECT_H_
#define _LOG_REDIRECT_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

typedef struct {
    uint64_t index;
    uint32_t timestamp;
    esp_log_level_t level;
    const char *tag;
    const char *message;
} log_entry_view_t;

typedef esp_err_t (*log_entry_consumer_t)(const log_entry_view_t *entry, void *user_ctx);

esp_err_t log_redirect_init(size_t buffer_size_bytes, bool enabled);
void log_redirect_set_enabled(bool enabled);
bool log_redirect_is_enabled(void);
uint64_t log_redirect_get_oldest_index(void);
uint64_t log_redirect_get_next_index(void);
esp_err_t log_redirect_consume(uint64_t from_index, log_entry_consumer_t consumer, void *user_ctx, uint64_t *last_index);

#endif /* _LOG_REDIRECT_H_ */
