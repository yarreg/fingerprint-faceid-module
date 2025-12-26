#include "f900.h"
#include <string.h>

#include "mbedtls/aes.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "F900";

// Encryption state
static uint8_t encryption_key[16] = {0};
static uint8_t session_key[16] = {0};
static bool encryption_enabled = false;
static mbedtls_aes_context aes_ctx;
static uint32_t current_baudrate = F900_DEFAULT_BAUDRATE;

static f900_config_t f900_config;

void f900_init(f900_config_t config) {
    f900_config = config;
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = F900_DEFAULT_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    // Install UART driver
    uart_driver_install(f900_config.uart_num, 2048, 2048, 0, NULL, 0);
    uart_param_config(f900_config.uart_num, &uart_config);
    uart_set_pin(f900_config.uart_num, f900_config.tx_pin, f900_config.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Configure enable pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << f900_config.en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Enable module
    // gpio_set_level(f900_config.en_pin, 1);
}

static uint8_t calculate_parity(const uint8_t* data, uint16_t size) {
    uint8_t parity = 0;
    for (uint16_t i = 0; i < size; i++) {
        parity ^= data[i];
    }
    return parity;
}

bool f900_set_baudrate(uint32_t baud) {
    // Send baudrate configuration command (32-bit little-endian)
    uint8_t data[4] = {
        (uint8_t)(baud & 0xFF),
        (uint8_t)((baud >> 8) & 0xFF),
        (uint8_t)((baud >> 16) & 0xFF),
        (uint8_t)((baud >> 24) & 0xFF)
    };

    if (!f900_send_message(MID_CONFIG_BAUDRATE, data, sizeof(data))) {
        return false;
    }

    // Reconfigure UART with new baudrate after 100ms delay (per docs)
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_set_baudrate(f900_config.uart_num, baud);

    // Verify the configuration
    uart_wait_tx_done(f900_config.uart_num, pdMS_TO_TICKS(100));

    // Update current baudrate
    current_baudrate = baud;
    return true;
}

uint32_t f900_get_baudrate(void) {
    return current_baudrate;
}

bool f900_set_encryption_key(const uint8_t key[16]) {
    memcpy(encryption_key, key, 16);
    return f900_send_message(MID_SET_RELEASE_ENC_KEY, key, 16);
}

bool f900_init_encryption_session(const uint8_t seed[4]) {
    // Generate session key using seed and encryption key
    mbedtls_aes_init(&aes_ctx);
    mbedtls_aes_setkey_enc(&aes_ctx, encryption_key, 128);

    // Encrypt seed to create session key
    uint8_t expanded_seed[16] = {0};
    memcpy(expanded_seed, seed, 4);
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, expanded_seed, session_key);

    // Send initialization command
    uint8_t init_data[8] = {0};
    memcpy(init_data, seed, 4);
    init_data[4] = 0x01; // AES-128 mode

    encryption_enabled = true;
    return f900_send_message(MID_INIT_ENCRYPTION, init_data, sizeof(init_data));
}

bool f900_send_encrypted_message(f900_msg_id_t msg_id, const uint8_t* data, uint16_t size) {
    if(!encryption_enabled) return false;

    uint8_t encrypted_data[size + 16 - (size % 16)];
    memcpy(encrypted_data, data, size);

    // Pad data to AES block size
    uint16_t padded_size = (size + 15) & ~15;
    memset(encrypted_data + size, 0, padded_size - size);

    // Encrypt in ECB mode (module uses same encryption)
    for(int i=0; i<padded_size; i+=16) {
        mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, encrypted_data+i, encrypted_data+i);
    }

    return f900_send_message(msg_id, encrypted_data, padded_size);
}

bool f900_receive_encrypted_message(f900_message_t* msg) {
    if(!f900_receive_message(msg)) return false;

    if(encryption_enabled && msg->size > 0) {
        // Decrypt received data
        for(int i=0; i<msg->size; i+=16) {
            mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_DECRYPT, msg->data+i, msg->data+i);
        }
    }

    return true;
}

bool f900_send_message(f900_msg_id_t msg_id, const uint8_t* data, uint16_t size) {
    // Handle encryption if enabled
    if(encryption_enabled && msg_id != MID_SET_RELEASE_ENC_KEY) {
        return f900_send_encrypted_message(msg_id, data, size);
    }
    f900_message_t msg;
    msg.sync_word = F900_SYNC_WORD;
    msg.msg_id = msg_id;
    msg.size = size;

    if (data && size > 0) {
        memcpy(msg.data, data, size);
    }

    // Calculate parity from msg_id to end of data
    uint8_t* parity_start = (uint8_t*)&msg.msg_id;
    uint16_t parity_size = sizeof(msg.msg_id) + sizeof(msg.size) + size;
    msg.parity = calculate_parity(parity_start, parity_size);

    // Send message via UART
    // Write sync word
    uart_write_bytes(f900_config.uart_num, (const char*)&msg.sync_word, sizeof(msg.sync_word));

    // Write msg_id
    uart_write_bytes(f900_config.uart_num, (const char*)&msg.msg_id, sizeof(msg.msg_id));

    // Write size
    uart_write_bytes(f900_config.uart_num, (const char*)&msg.size, sizeof(msg.size));

    // Write data if present
    if (size > 0) {
        uart_write_bytes(f900_config.uart_num, (const char*)msg.data, size);
    }

    // Write parity
    uart_write_bytes(f900_config.uart_num, (const char*)&msg.parity, sizeof(msg.parity));

    // Wait for transmission to complete
    uart_wait_tx_done(f900_config.uart_num, pdMS_TO_TICKS(100));

    return true;
}

bool f900_receive_message(f900_message_t* msg) {
    uint8_t sync_buf[2];
    int len;

    // Wait for sync word (0xEFAA)
    while(1) {
        len = uart_read_bytes(f900_config.uart_num, sync_buf, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            ESP_LOGE(TAG, "Failed to read sync word");
            return false;
        }
        if (len == 1 && sync_buf[0] == 0xEF) {
            len = uart_read_bytes(f900_config.uart_num, &sync_buf[1], 1, pdMS_TO_TICKS(100));
            if (len == 1 && sync_buf[1] == 0xAA) {
                break;
            }
        }
    }

    // Read message header (msg_id + size)
    uint8_t header[3];
    len = uart_read_bytes(f900_config.uart_num, header, sizeof(header), pdMS_TO_TICKS(100));
    if (len != sizeof(header)) {
        return false;
    }

    msg->sync_word = F900_SYNC_WORD;
    msg->msg_id = header[0];
    msg->size = (header[1] << 8) | header[2];

    // Read message data
    if (msg->size > 0) {
        if (msg->size > F900_MAX_DATA_SIZE) {
            ESP_LOGE(TAG, "Received message size exceeds maximum allowed size");
            return false;
        }

        len = uart_read_bytes(f900_config.uart_num, msg->data, msg->size, pdMS_TO_TICKS(100));
        if (len != msg->size) {
            return false;
        }
    }

    // Read parity byte
    uint8_t received_parity;
    len = uart_read_bytes(f900_config.uart_num, &received_parity, 1, pdMS_TO_TICKS(100));
    if (len != 1) {
        return false;
    }

    // Calculate and verify parity
    uint8_t* parity_start = (uint8_t*)&msg->msg_id;
    uint16_t parity_size = sizeof(msg->msg_id) + sizeof(msg->size) + msg->size;
    uint8_t calculated_parity = calculate_parity(parity_start, parity_size);

    if (received_parity != calculated_parity) {
        return false;
    }

    return true;
}

bool f900_reset(void) {
    return f900_send_message(MID_RESET, NULL, 0);
}

bool f900_get_status(f900_status_t* status) {
    f900_message_t msg;
    if (!f900_send_message(MID_GETSTATUS, NULL, 0)) {
        return false;
    }

    if (!f900_receive_message(&msg)) {
        return false;
    }

    if (msg.msg_id == MID_REPLY && msg.size == 1) {
        *status = (f900_status_t)msg.data[0];
        return true;
    }

    return false;
}

bool f900_verify(uint8_t timeout, f900_user_info_t* user_info) {
    uint8_t data[2] = {0, timeout}; // pd_rightaway = 0
    if (!f900_send_message(MID_VERIFY, data, sizeof(data))) {
        return false;
    }

    TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout * 1000);
    f900_message_t msg;

    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
        if (!f900_receive_message(&msg)) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Small delay between attempts
            continue;
        }

        // Handle NOTE messages with face state
        if (msg.msg_id == MID_NOTE) {
            ESP_LOGI(TAG, "f900_verify(). Received NOTE message with size: %d", msg.size);

            if (msg.size == 17 && msg.data[0] == 1) { // NID_FACE_STATE
                 // Parse the face state information into f900_note_data_face_t
                 f900_note_data_face_t face_data;
                 face_data.state = (msg.data[1] << 8) | msg.data[2];
                 face_data.left = (msg.data[3] << 8) | msg.data[4];
                 face_data.top = (msg.data[5] << 8) | msg.data[6];
                 face_data.right = (msg.data[7] << 8) | msg.data[8];
                 face_data.bottom = (msg.data[9] << 8) | msg.data[10];
                 face_data.yaw = (msg.data[11] << 8) | msg.data[12];
                 face_data.pitch = (msg.data[13] << 8) | msg.data[14];
                 face_data.roll = (msg.data[15] << 8) | msg.data[16];

                 // Log the parsed face data
                 ESP_LOGI(TAG, "f900_verify(). Face state: %d, Position: (%d, %d, %d, %d), Pose: (yaw: %d, pitch: %d, roll: %d)", face_data.state, face_data.left, face_data.top, face_data.right, face_data.bottom, face_data.yaw, face_data.pitch, face_data.roll);
            } else {
                ESP_LOGW(TAG, "f900_verify(). Received NOTE message with unknown type. Size: %d", msg.size);
            }
            continue;
        }

        // Handle final REPLY
        if (msg.msg_id == MID_REPLY) {
            if (msg.size >= sizeof(f900_user_info_t)) {
                memcpy(user_info, msg.data, sizeof(f900_user_info_t));
                return true;
            }
            return false;
        }
    }

    return false; // Timeout
}

bool f900_enroll(const f900_enroll_data_t* enroll_data, uint16_t* user_id) {
    if (!f900_send_message(MID_ENROLL, (uint8_t*)enroll_data, sizeof(f900_enroll_data_t))) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg)) {
        return false;
    }

    if (msg.msg_id == MID_REPLY && msg.size == 2) {
        *user_id = (msg.data[0] << 8) | msg.data[1];
        return true;
    }

    return false;
}

bool f900_delete_user(uint16_t user_id) {
    uint8_t data[2] = {(uint8_t)(user_id >> 8), (uint8_t)user_id};
    return f900_send_message(MID_DELUSER, data, sizeof(data));
}

bool f900_delete_all_users(void) {
    return f900_send_message(MID_DELALL, NULL, 0);
}

bool f900_get_user_info(uint16_t user_id, f900_user_info_t* user_info) {
    uint8_t data[2] = {(uint8_t)(user_id >> 8), (uint8_t)user_id};
    if (!f900_send_message(MID_GETUSERINFO, data, sizeof(data))) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg)) {
        return false;
    }

    if (msg.msg_id == MID_REPLY && msg.size >= sizeof(f900_user_info_t)) {
        memcpy(user_info, msg.data, sizeof(f900_user_info_t));
        return true;
    }

    return false;
}

bool f900_power_down(void) {
    return f900_send_message(MID_POWERDOWN, NULL, 0);
}

bool f900_face_reset(void) {
    // Send reset command
    if (!f900_send_message(MID_FACERESET, NULL, 0)) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg)) {
        return false;
    }

    // Validate response
    if (msg.msg_id == MID_REPLY && msg.size >= 1) {
        if(msg.data[0] == MR_SUCCESS) {
            // Reset successful - clear any cached enrollment state
            return true;
        }
    }

    return false;
}

bool f900_get_all_user_ids(uint16_t* user_ids, uint16_t* count) {
    if (!f900_send_message(MID_GET_ALL_USERID, NULL, 0)) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg)) {
        return false;
    }

    // Check if we got a valid reply
    if (msg.msg_id == MID_REPLY && msg.size >= 1) {
        // First byte is the count of users
        *count = msg.data[0];

        // Each user ID is 2 bytes (high and low byte)
        for (uint16_t i = 0; i < *count; i++) {
            user_ids[i] = (msg.data[1 + i*2] << 8) | msg.data[2 + i*2];
        }
        return true;
    }

    return false;
}

bool f900_set_threshold_level(uint8_t verify_level, uint8_t liveness_level) {
    // Validate input levels (0-4)
    if (verify_level > 4 || liveness_level > 4) {
        return false;
    }

    uint8_t data[2] = {verify_level, liveness_level};
    if (!f900_send_message(MID_SET_THRESHOLD_LEVEL, data, sizeof(data))) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg)) {
        return false;
    }

    // Check if we got a valid reply
    if (msg.msg_id == MID_REPLY && msg.size >= 1) {
        return msg.data[0] == MR_SUCCESS;
    }

    return false;
}

void f900_set_enable(bool enable) {
    gpio_set_level(f900_config.en_pin, enable ? 1 : 0);
}

bool f900_capture_images(uint8_t image_count, uint8_t start_number) {
    uint8_t data[2] = {image_count, start_number};
    return f900_send_message(MID_SNAPIMAGE, data, sizeof(data));
}

bool f900_get_saved_image_size(uint8_t image_number, uint32_t* size) {
    uint8_t data[1] = {image_number};
    if (!f900_send_message(MID_GETSAVEDIMAGE, data, sizeof(data))) {
        return false;
    }

    f900_message_t msg;
    if (!f900_receive_message(&msg) || msg.msg_id != MID_REPLY) {
        return false;
    }

    // Extract image size from the reply
    *size = (msg.data[0] << 24) | (msg.data[1] << 16) | (msg.data[2] << 8) | msg.data[3];
    return true;
}

// Read the saved image data from the device
bool f900_get_saved_image(uint8_t image_number, uint32_t offset, uint32_t chunk_size, uint8_t* buffer) {
    f900_message_t msg;

    uint8_t upload_data[8] = {
        (offset >> 24) & 0xFF, (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF,
        (chunk_size >> 24) & 0xFF, (chunk_size >> 16) & 0xFF, (chunk_size >> 8) & 0xFF, chunk_size & 0xFF
    };

    if (!f900_send_message(MID_UPLOADIMAGE, upload_data, sizeof(upload_data))) {
        return false;
    }

    if (!f900_receive_message(&msg) || msg.msg_id != MID_IMAGE) {
        return false;
    }

    memcpy(buffer, msg.data, chunk_size);

    return true;
}
