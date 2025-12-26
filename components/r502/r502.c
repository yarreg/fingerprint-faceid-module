#include "r502.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define MAX_CALLBACKS 8

static const char *TAG = "R502";
static r502_config_t g_config;
static uint32_t g_uart_timeout = R502_DEFAULT_TIMEOUT_MS;

static r502_irq_callback g_irq_callbacks[MAX_CALLBACKS] = {NULL};
static uint8_t g_callback_counter = 0;


static void IRAM_ATTR gpio_isr_handler(void* arg) {
    for (int i = 0; i < g_callback_counter; i++) {
        if (g_irq_callbacks[i]) {
            g_irq_callbacks[i]();
        }
    }
}

// The arithmetic sum of package identifier, package length and all package content.
// Overflowing bits are omitted.
static uint16_t calculate_checksum(uint8_t *data, size_t len) {
    uint16_t sum = 0;
    for (size_t i=0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static bool check_checksum(uint8_t *data, size_t len) {
    if (len < 2)
        return false;
    uint16_t received = (data[len-2] << 8) | data[len-1];
    return received == calculate_checksum(data + 6, len - 2 - 6);
}

static void add_command_arg_8(uint8_t **payload, uint8_t value) {
    *(*payload)++ = value;
}

static void add_command_arg_16(uint8_t **payload, uint16_t value) {
    *(*payload)++ = (uint8_t)((value >> 8) & 0xFF);
    *(*payload)++ = (uint8_t)(value & 0xFF);
}

static void add_command_arg_32(uint8_t **payload, uint32_t value) {
    *(*payload)++ = (uint8_t)((value >> 24) & 0xFF);
    *(*payload)++ = (uint8_t)((value >> 16) & 0xFF);
    *(*payload)++ = (uint8_t)((value >> 8) & 0xFF);
    *(*payload)++ = (uint8_t)(value & 0xFF);
}

// Build command packet header
// * cmd 0 command code (id)
// * packet_size = packet buffer size
// return pointer to the first byte of command payload
static uint8_t *init_command(uint8_t *packet, uint8_t cmd, uint16_t packet_size) {
    uint8_t *ptr = packet;

    // Packet header
    add_command_arg_8(&ptr, 0xEF);
    add_command_arg_8(&ptr, 0x01);

    // Device address
    add_command_arg_32(&ptr, g_config.address);

    // Package identifier ()
    add_command_arg_8(&ptr, 0x01);

    // Data length
    add_command_arg_16(&ptr, packet_size - R502_HEADER_SIZE);

    // Command code (instruction code)
    add_command_arg_8(&ptr, cmd);

    return ptr;
}

static void finalize_command(uint8_t *packet, uint16_t packet_size) {
    // Checksum should be added to last 2 bytes of the packet
    uint8_t *checksum_ptr = packet + (packet_size - 2);

    // Calculate checksum
    // 6 = offset of [packet identifier] in packet
    // 2 = checksum size
    uint16_t checksum = calculate_checksum(packet + 6, packet_size - 2 - 6);
    add_command_arg_16(&checksum_ptr, checksum);
}

static size_t read_bytes(uint8_t *buffer, size_t len) {
    size_t remaining = len;
    size_t offset = 0;

    while (remaining > 0) {
        int read = uart_read_bytes(g_config.uart_num, buffer + offset, remaining,  pdMS_TO_TICKS(g_uart_timeout));
        if(read <= 0)
            break;

        offset += read;
        remaining -= read;
    }
    return offset;
}

void r502_init(r502_config_t cfg) {
    g_config = cfg;

    uart_config_t uart_cfg = {
        .baud_rate = R502_DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(g_config.uart_num, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(g_config.uart_num, g_config.tx_pin, g_config.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(g_config.uart_num, 2048, 2048, 0, NULL, 0));

    // Configure enable pin if used
    if (g_config.en_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << g_config.en_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(g_config.en_pin, 1); // Disabled by default
    }

    // Configure IRQ pin if used
    if (g_config.irq_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << g_config.irq_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE // Will be configured when callback is set
        };
        gpio_config(&io_conf);
    }
}

void r502_set_timeout(uint32_t timeout_ms) {
    g_uart_timeout = timeout_ms;
}

void r502_set_enable(bool enable) {
    // HI = disabled, LOW = enabled
    if (g_config.en_pin >= 0) {
        gpio_set_level(g_config.en_pin, enable ? 0 : 1);
        if (enable) {
            vTaskDelay(pdMS_TO_TICKS(300));
            uart_flush(g_config.uart_num);
        }
    }
}

bool r502_is_enabled(void) {
    if (g_config.en_pin >= 0) {
        return gpio_get_level(g_config.en_pin) == 1;
    }
    return true;
}

void r502_add_irq_callback(r502_irq_callback callback) {
    if (g_config.irq_pin < 0) {
        ESP_LOGE(TAG, "IRQ pin not configured");
        return;
    }

    if (g_callback_counter >= MAX_CALLBACKS) {
        ESP_LOGE(TAG, "Max callbacks reached");
        return;
    }

    // Add callback to list
    g_irq_callbacks[g_callback_counter++] = callback;

    // Configure GPIO interrupt if first callback
    if (g_callback_counter == 1) {
        gpio_set_intr_type(g_config.irq_pin, GPIO_INTR_NEGEDGE);

        // Install ISR service if not already installed
        static bool isr_service_installed = false;
        if (!isr_service_installed) {
            gpio_install_isr_service(0);
            isr_service_installed = true;
        }

        // Hook ISR handler for this pin
        gpio_isr_handler_add(g_config.irq_pin, gpio_isr_handler, NULL);
    }
}

void r502_remove_irq_callback(r502_irq_callback callback) {
    if (g_config.irq_pin < 0) {
        ESP_LOGE(TAG, "IRQ pin not configured");
        return;
    }

    // Find and remove callback
    for (int i = 0; i < g_callback_counter; i++) {
        if (g_irq_callbacks[i] == callback) {
            // Shift remaining callbacks down
            for (int j = i; j < g_callback_counter - 1; j++) {
                g_irq_callbacks[j] = g_irq_callbacks[j + 1];
            }
            g_callback_counter--;
            g_irq_callbacks[g_callback_counter] = NULL;
            break;
        }
    }

    // Disable interrupt if no more callbacks
    if (g_callback_counter == 0) {
        gpio_set_intr_type(g_config.irq_pin, GPIO_INTR_DISABLE);
        gpio_isr_handler_remove(g_config.irq_pin);
    }
}

void r502_clear_irq_callbacks() {
    if (g_config.irq_pin < 0) {
        ESP_LOGE(TAG, "IRQ pin not configured");
        return;
    }

    // Clear all callbacks
    g_callback_counter = 0;
    memset(g_irq_callbacks, 0, sizeof(g_irq_callbacks));

    // Disable interrupt
    gpio_set_intr_type(g_config.irq_pin, GPIO_INTR_DISABLE);
    gpio_isr_handler_remove(g_config.irq_pin);
}

static esp_err_t send_command(uint8_t *packet, size_t packet_len, uint8_t *response, size_t response_len, r502_generic_reply *reply) {
    uart_write_bytes(g_config.uart_num, (char*)packet, packet_len);
    size_t read = read_bytes(response, response_len);
    if (read == 0) {
        return ESP_ERR_TIMEOUT;
    }

    if (read != response_len) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!check_checksum(response, read)) {
        return ESP_ERR_INVALID_CRC;
    }

    reply->conf_code = response[9];
    return ESP_OK;
}

// Command implementations ----------------------------------------------------

 esp_err_t r502_setsyspara(r502_param_num_t param_num, uint8_t value, r502_generic_reply *reply) {
    uint8_t packet[R502_SET_SYS_PARA_PACKET_SIZE] = {0};

    // Build command package (instruction code 0x0E)
    uint8_t *payload = init_command(packet, 0x0E, sizeof(packet));

    // Add parameters
    add_command_arg_8(&payload, (uint8_t)param_num); // Parameter number
    add_command_arg_8(&payload, value);              // Parameter value

    finalize_command(packet, sizeof(packet));

    // Response format: 12 bytes (header + checksum)
    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

// Helper function to get the current status of the sensor using r502_readsyspara command
 esp_err_t r502_get_status(r502_status_t *status) {
    r502_syspara_reply reply;
    esp_err_t err = r502_readsyspara(&reply);
    if (err != ESP_OK) {
        return err;
    }

    // Decode status register bits
    status->busy = (reply.status_register & 0x01) != 0;
    status->pass = (reply.status_register & 0x02) != 0;
    status->pwd = (reply.status_register & 0x04) != 0;
    status->img_buf_stat = (reply.status_register & 0x08) != 0;

    return ESP_OK;
}

esp_err_t r502_readsyspara(r502_syspara_reply *reply) {
    uint8_t packet[R502_READ_SYS_PARA_PACKET_SIZE] = {0};

    // Build command package (instruction code 0x0F)
    init_command(packet, 0x0F, sizeof(packet));
    finalize_command(packet, sizeof(packet));

    // Response format: 28 bytes (header + 16 bytes parameters + checksum)
    uint8_t response[28];
    r502_generic_reply generic_reply;
    esp_err_t err = send_command(packet, sizeof(packet), response, sizeof(response), &generic_reply);

    // Parse system parameters if successful
    if (err == ESP_OK && generic_reply.conf_code == 0x00) {
        reply->status_register = (response[10] << 8) | response[11];
        reply->sys_id_code = (response[12] << 8) | response[13];
        reply->lib_size = (response[14] << 8) | response[15];
        reply->security_level = (response[16] << 8) | response[17];
        reply->device_address = ((uint32_t)response[18] << 24) | ((uint32_t)response[19] << 16) | ((uint32_t)response[20] << 8) | response[21];
        reply->data_packet_size = (response[22] << 8) | response[23];
        reply->baud_rate = (response[24] << 8) | response[25];
    }

    reply->conf_code = generic_reply.conf_code;
    return err;
}

esp_err_t r502_auraledconfig(uint8_t control, uint8_t speed, uint8_t color, uint8_t times, r502_generic_reply *reply) {
    uint8_t packet[R502_AURALED_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x35, sizeof(packet)); // Command code, data length
    add_command_arg_8(&payload, control); // 1 - breathing, 2 - flashing, 3 - on, 4 - off
    add_command_arg_8(&payload, speed); // 0x00-0xff, 256 gears, Minimum 5s cycle.
    add_command_arg_8(&payload, color); // Colors: 1=red, 2=blue, 3=purple
    add_command_arg_8(&payload, times);

    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_genimg(r502_generic_reply *reply) {
    uint8_t packet[R502_GENIMG_PACKET_SIZE] = {0};

    init_command(packet, 0x01, sizeof(packet)); // GenImg command
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_search(uint8_t buffer, uint16_t start, uint16_t count, r502_search_reply *reply) {
    uint8_t packet[R502_SEARCH_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x04, sizeof(packet)); // Search command
    add_command_arg_8(&payload, buffer);
    add_command_arg_16(&payload, start);
    add_command_arg_16(&payload, count);

    finalize_command(packet, sizeof(packet));

    uint8_t response[16];
    r502_generic_reply generic_reply;
    esp_err_t err = send_command(packet, sizeof(packet), response, sizeof(response), &generic_reply);
    if (err != ESP_OK) {
        return err;
    }

    reply->conf_code = generic_reply.conf_code;

    if(generic_reply.conf_code == 0x00) {
        reply->index = (response[10] << 8) | response[11];
        reply->match_score = (response[12] << 8) | response[13];
    }

    return err;
}

esp_err_t r502_vfypwd(uint32_t password, r502_generic_reply *reply) {
    uint8_t packet[R502_VFYPWD_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x13, sizeof(packet)); // VfyPwd command
    add_command_arg_32(&payload, password);
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_img2tz(uint8_t buffer, r502_generic_reply *reply) {
    uint8_t packet[R502_IMG2TZ_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x02, sizeof(packet)); // Img2Tz command
    add_command_arg_8(&payload, buffer);

    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_templatenum(r502_templatenum_reply *reply) {
    uint8_t packet[R502_TEMPLATENUM_PACKET_SIZE] = {0};

    init_command(packet, 0x1d, sizeof(packet)); // TemplateNum command
    finalize_command(packet, sizeof(packet));

    uint8_t response[14];
    r502_generic_reply generic_reply;
    esp_err_t err = send_command(packet, sizeof(packet), response, sizeof(response), &generic_reply);
    if (err != ESP_OK) {
        return err;
    }

    reply->conf_code = generic_reply.conf_code;

    if(generic_reply.conf_code == 0x00) {
        reply->index = (response[10] << 8) | response[11];
    }

    return err;
}

esp_err_t r502_regmodel(r502_generic_reply *reply) {
    uint8_t packet[R502_REGMODEL_PACKET_SIZE] = {0};

    init_command(packet, 0x05, sizeof(packet)); // RegModel command
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_store(uint8_t buffer, uint16_t index, r502_generic_reply *reply) {
    uint8_t packet[R502_STORE_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x06, sizeof(packet)); // Store command
    add_command_arg_8(&payload, buffer);
    add_command_arg_16(&payload, index);
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_handshake(r502_generic_reply *reply) {
    uint8_t packet[R502_HANDSHAKE_PACKET_SIZE] = {0};

    init_command(packet, 0x40, sizeof(packet)); // HandShake command
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_setpwd(uint32_t new_password, r502_generic_reply *reply) {
    uint8_t packet[R502_SETPWD_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x12, sizeof(packet)); // SetPwd command
    add_command_arg_32(&payload, new_password);
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_deletechar(uint16_t start, uint16_t count, r502_generic_reply *reply) {
    uint8_t packet[R502_DELETECHAR_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x0c, sizeof(packet)); // DeleteChar command
    add_command_arg_16(&payload, start);
    add_command_arg_16(&payload, count);
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_empty(r502_generic_reply *reply) {
    uint8_t packet[R502_EMPTY_PACKET_SIZE] = {0};

    init_command(packet, 0x0d, sizeof(packet)); // Empty command
    finalize_command(packet, sizeof(packet));

    uint8_t response[12];
    return send_command(packet, sizeof(packet), response, sizeof(response), reply);
}

esp_err_t r502_readindextable(uint8_t page, r502_indextable_reply *reply) {
    uint8_t packet[R502_READINDEXTABLE_PACKET_SIZE] = {0};

    uint8_t *payload = init_command(packet, 0x1f, sizeof(packet)); // ReadIndexTable command
    add_command_arg_8(&payload, page);
    finalize_command(packet, sizeof(packet));

    uint8_t response[44];
    r502_generic_reply generic_reply;
    esp_err_t err = send_command(packet, sizeof(packet), response, sizeof(response), &generic_reply);

    reply->conf_code = generic_reply.conf_code;

    if (reply->conf_code == 0x00) {
        memcpy(reply->index_page, response + 10, 32);
    }

    return err;
}
