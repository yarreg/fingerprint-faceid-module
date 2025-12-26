#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "log_redirect.h"

typedef struct {
    uint64_t index;
    uint32_t timestamp;
    uint16_t tag_len;
    uint16_t msg_len;
    uint8_t  level;
    uint8_t  reserved[3];
    char     data[];
} log_item_t;

static const char *TAG = "log_redirect";

static RingbufHandle_t g_ringbuf = NULL;
static size_t g_buffer_size = 0;
static uint64_t g_next_index = 1;
static uint64_t g_oldest_index = 1;
static size_t g_entry_count = 0;
static bool g_enabled = false;
static bool g_initialized = false;
static vprintf_like_t g_prev_vprintf = NULL;
static portMUX_TYPE g_log_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_log_level_t parse_level_from_format(const char *fmt) {
    while (fmt && *fmt) {
        if (*fmt == '\x1b') {
            const char *esc_end = strchr(fmt, 'm');
            if (!esc_end) {
                break;
            }
            fmt = esc_end + 1;
            continue;
        }
        if (*fmt == 'E') {
            return ESP_LOG_ERROR;
        }
        if (*fmt == 'W') {
            return ESP_LOG_WARN;
        }
        if (*fmt == 'I') {
            return ESP_LOG_INFO;
        }
        if (*fmt == 'D') {
            return ESP_LOG_DEBUG;
        }
        if (*fmt == 'V') {
            return ESP_LOG_VERBOSE;
        }
        fmt++;
    }
    return ESP_LOG_NONE;
}

static const char *find_message_format(const char *fmt) {
    if (!fmt) {
        return NULL;
    }
    const char *tag_marker = strstr(fmt, "%s: ");
    if (!tag_marker) {
        return NULL;
    }
    return tag_marker + 4; // Skip "%s: "
}

static void update_counters_on_add(uint64_t index) {
    portENTER_CRITICAL(&g_log_lock);
    if (g_entry_count == 0) {
        g_oldest_index = index;
    }
    g_entry_count++;
    portEXIT_CRITICAL(&g_log_lock);
}

static void update_counters_on_remove(uint64_t removed_index) {
    portENTER_CRITICAL(&g_log_lock);
    if (g_entry_count > 0) {
        g_entry_count--;
    }
    if (g_entry_count == 0) {
        g_oldest_index = g_next_index;
    } else {
        g_oldest_index = removed_index + 1;
    }
    portEXIT_CRITICAL(&g_log_lock);
}

static void drop_oldest_until_space(size_t required_size) {
    if (!g_ringbuf) {
        return;
    }

    while (xRingbufferGetCurFreeSize(g_ringbuf) < required_size) {
        size_t item_size = 0;
        log_item_t *oldest = (log_item_t *)xRingbufferReceive(g_ringbuf, &item_size, 0);
        if (!oldest) {
            break;
        }
        update_counters_on_remove(oldest->index);
        vRingbufferReturnItem(g_ringbuf, oldest);
    }
}

static log_item_t *acquire_slot(size_t total_size) {
    if (!g_ringbuf) {
        return NULL;
    }
    size_t max_item_size = xRingbufferGetMaxItemSize(g_ringbuf);
    if (total_size > max_item_size) {
        ESP_LOGW(TAG, "Log entry too large for ring buffer (%zu > %zu)", total_size, max_item_size);
        return NULL;
    }

    log_item_t *slot = NULL;
    BaseType_t status = xRingbufferSendAcquire(g_ringbuf, (void **)&slot, total_size, 0);
    while (status != pdTRUE || slot == NULL) {
        drop_oldest_until_space(total_size);
        status = xRingbufferSendAcquire(g_ringbuf, (void **)&slot, total_size, 0);
        if (status != pdTRUE || !slot) {
            break;
        }
    }
    return slot;
}

static uint64_t next_log_index(void) {
    portENTER_CRITICAL(&g_log_lock);
    uint64_t index = g_next_index++;
    portEXIT_CRITICAL(&g_log_lock);
    return index;
}

static void trim_message(char *message, size_t max_len) {
    if (!message || max_len == 0) {
        return;
    }
    size_t len = strnlen(message, max_len);
    while (len > 0 && (message[len - 1] == '\n' || message[len - 1] == '\r')) {
        message[len - 1] = '\0';
        len--;
    }
    const char *reset_seq = "\x1b[0m";
    size_t reset_len = strlen(reset_seq);
    if (len >= reset_len && strcmp(message + len - reset_len, reset_seq) == 0) {
        message[len - reset_len] = '\0';
    }
}

static void handle_log_capture(const char *fmt, va_list args) {
    if (!g_initialized || !g_enabled || !g_ringbuf) {
        return;
    }

    const char *msg_fmt = find_message_format(fmt);
    if (!msg_fmt) {
        return;
    }

    va_list working_args;
    va_copy(working_args, args);
    uint32_t timestamp = va_arg(working_args, uint32_t);
    const char *tag = va_arg(working_args, const char *);
    if (!tag) {
        va_end(working_args);
        return;
    }

    size_t tag_len = strlen(tag) + 1;
    if (tag_len > UINT16_MAX) {
        va_end(working_args);
        return;
    }

    va_list msg_args_len;
    va_copy(msg_args_len, working_args);
    int computed_len = vsnprintf(NULL, 0, msg_fmt, msg_args_len);
    va_end(msg_args_len);
    if (computed_len < 0) {
        va_end(working_args);
        return;
    }

    size_t msg_len = (size_t)computed_len + 1;
    if (msg_len > UINT16_MAX) {
        va_end(working_args);
        return;
    }

    size_t total_size = sizeof(log_item_t) + tag_len + msg_len;
    if (total_size > g_buffer_size) {
        va_end(working_args);
        return;
    }

    log_item_t *slot = acquire_slot(total_size);
    if (!slot) {
        va_end(working_args);
        return;
    }

    uint64_t index = next_log_index();
    slot->index = index;
    slot->timestamp = timestamp;
    slot->tag_len = (uint16_t)tag_len;
    slot->msg_len = (uint16_t)msg_len;
    slot->level = (uint8_t)parse_level_from_format(fmt);
    memset(slot->reserved, 0, sizeof(slot->reserved));

    memcpy(slot->data, tag, tag_len);

    va_list msg_args_write;
    va_copy(msg_args_write, working_args);
    vsnprintf(slot->data + tag_len, msg_len, msg_fmt, msg_args_write);
    va_end(msg_args_write);
    va_end(working_args);

    trim_message(slot->data + tag_len, msg_len);

    (void)xRingbufferSendComplete(g_ringbuf, slot);
    update_counters_on_add(index);
}

static int log_redirect_vprintf(const char *fmt, va_list args) {
    va_list store_args;
    va_copy(store_args, args);

    int printed = 0;
    if (g_prev_vprintf) {
        va_list print_args;
        va_copy(print_args, args);
        printed = g_prev_vprintf(fmt, print_args);
        va_end(print_args);
    }

    handle_log_capture(fmt, store_args);
    va_end(store_args);
    return printed;
}

esp_err_t log_redirect_init(size_t buffer_size_bytes, bool enabled) {
    if (g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer_size_bytes < 256) {
        buffer_size_bytes = 256;
    }

    g_ringbuf = xRingbufferCreate(buffer_size_bytes, RINGBUF_TYPE_NOSPLIT);
    if (!g_ringbuf) {
        return ESP_ERR_NO_MEM;
    }

    g_prev_vprintf = esp_log_set_vprintf(log_redirect_vprintf);
    g_buffer_size = buffer_size_bytes;
    g_enabled = enabled;
    g_initialized = true;
    return ESP_OK;
}

void log_redirect_set_enabled(bool enabled) {
    g_enabled = enabled;
}

bool log_redirect_is_enabled(void) {
    return g_initialized && g_enabled;
}

uint64_t log_redirect_get_oldest_index(void) {
    portENTER_CRITICAL(&g_log_lock);
    uint64_t index = g_oldest_index;
    portEXIT_CRITICAL(&g_log_lock);
    return index;
}

uint64_t log_redirect_get_next_index(void) {
    portENTER_CRITICAL(&g_log_lock);
    uint64_t index = g_next_index;
    portEXIT_CRITICAL(&g_log_lock);
    return index;
}

esp_err_t log_redirect_consume(uint64_t from_index, log_entry_consumer_t consumer, void *user_ctx, uint64_t *last_index) {
    if (!g_initialized || !g_ringbuf) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!consumer) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t status = ESP_OK;
    while (true) {
        size_t item_size = 0;
        log_item_t *item = (log_item_t *)xRingbufferReceive(g_ringbuf, &item_size, 0);
        if (!item) {
            break;
        }

        if (item->index < from_index) {
            update_counters_on_remove(item->index);
            vRingbufferReturnItem(g_ringbuf, item);
            continue;
        }

        log_entry_view_t view = {
            .index = item->index,
            .timestamp = item->timestamp,
            .level = (esp_log_level_t)item->level,
            .tag = item->data,
            .message = item->data + item->tag_len
        };

        status = consumer(&view, user_ctx);
        update_counters_on_remove(item->index);
        vRingbufferReturnItem(g_ringbuf, item);
        if (last_index && status == ESP_OK) {
            *last_index = view.index;
        }
        if (status != ESP_OK) {
            break;
        }
    }

    return status;
}
