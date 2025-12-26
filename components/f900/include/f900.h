#ifndef _F900_H_
#define _F900_H_

#include <stdint.h>
#include <stdbool.h>

#include "driver/uart.h"

#define F900_SYNC_WORD 0xEFAA
#define F900_MAX_DATA_SIZE 4000
#define F900_USER_NAME_SIZE 32
#define F900_DEFAULT_BAUDRATE 115200

#ifdef __cplusplus
extern "C" {
#endif

// Message IDs
typedef enum {
    MID_REPLY = 0x00,
    MID_NOTE = 0x01,
    MID_IMAGE = 0x02,
    MID_RESET = 0x10,
    MID_GETSTATUS = 0x11,
    MID_VERIFY = 0x12,
    MID_ENROLL = 0x13,
    MID_SNAPIMAGE = 0x16,
    MID_GETSAVEDIMAGE = 0x17,
    MID_UPLOADIMAGE = 0x18,
    MID_ENROLL_SINGLE = 0x1D,
    MID_DELUSER = 0x20,
    MID_DELALL = 0x21,
    MID_GETUSERINFO = 0x22,
    MID_FACERESET = 0x23,
    MID_GET_ALL_USERID = 0x24,
    MID_ENROLL_ITG = 0x26,
    MID_GET_VERSION = 0x30,
    MID_START_OTA = 0x40,
    MID_STOP_OTA = 0x41,
    MID_GET_OTA_STATUS = 0x42,
    MID_OTA_HEADER = 0x43,
    MID_OTA_PACKET = 0x44,
    MID_INIT_ENCRYPTION = 0x50,
    MID_CONFIG_BAUDRATE = 0x51,
    MID_SET_RELEASE_ENC_KEY = 0x52,
    MID_SET_DEBUG_ENC_KEY = 0x53,
    MID_GET_LOGFILE = 0x60,
    MID_UPLOAD_LOGFILE = 0x61,
    MID_SET_THRESHOLD_LEVEL = 0xD4,
    MID_POWERDOWN = 0xED,
    MID_DEMOMODE = 0xFE
} f900_msg_id_t;

// Result codes
typedef enum {
    MR_SUCCESS = 0,
    MR_REJECTED = 1,
    MR_ABORTED = 2,
    MR_FAILED4_CAMERA = 4,
    MR_FAILED4_UNKNOWNREASON = 5,
    MR_FAILED4_INVALIDPARAM = 6,
    MR_FAILED4_NOMEMORY = 7,
    MR_FAILED4_UNKNOWNUSER = 8,
    MR_FAILED4_MAXUSER = 9,
    MR_FAILED4_FACEENROLLED = 10,
    MR_FAILED4_LIVENESSCHECK = 12,
    MR_FAILED4_TIMEOUT = 13,
    MR_FAILED4_AUTHORIZATION = 14,
    MR_FAILED4_READ_FILE = 19,
    MR_FAILED4_WRITE_FILE = 20,
    MR_FAILED4_NO_ENCRYPT = 21
} f900_result_t;

// Face directions
typedef enum {
    FACE_DIRECTION_UP = 0x10,
    FACE_DIRECTION_DOWN = 0x08,
    FACE_DIRECTION_LEFT = 0x04,
    FACE_DIRECTION_RIGHT = 0x02,
    FACE_DIRECTION_MIDDLE = 0x01
} f900_face_dir_t;

// Module status
typedef enum {
    MS_STANDBY = 0,
    MS_BUSY = 1,
    MS_ERROR = 2,
    MS_INVALID = 3
} f900_status_t;

// Message structures
typedef struct {
    uint16_t sync_word;
    uint8_t msg_id;
    uint16_t size;
    uint8_t data[F900_MAX_DATA_SIZE];
    uint8_t parity;
} f900_message_t;

typedef struct {
    uint8_t admin;
    uint8_t user_name[F900_USER_NAME_SIZE];
    f900_face_dir_t face_direction;
    uint8_t timeout;
} f900_enroll_data_t;

typedef struct {
    uint8_t user_id_heb;
    uint8_t user_id_leb;
    uint8_t user_name[F900_USER_NAME_SIZE];
    uint8_t admin;
} f900_user_info_t;

typedef struct {
    int16_t state; // corresponding to FACE_STATE_*
    // position
    int16_t left; // in pixel
    int16_t top;
    int16_t right;
    int16_t bottom;
    // pose
    int16_t yaw; // up and down in vertical orientation
    int16_t pitch; // right or left turned in horizontal orientation
    int16_t roll;
} f900_note_data_face_t;

// Function prototypes
typedef struct {
    int rx_pin;
    int tx_pin;
    int en_pin;
    uart_port_t uart_num;  // UART port number to use
} f900_config_t;

void f900_init(f900_config_t config);
bool f900_set_baudrate(uint32_t baud);
uint32_t f900_get_baudrate(void);
bool f900_send_message(f900_msg_id_t msg_id, const uint8_t* data, uint16_t size);
bool f900_receive_message(f900_message_t* msg);
bool f900_reset(void);
bool f900_get_status(f900_status_t* status);
bool f900_verify(uint8_t timeout, f900_user_info_t* user_info);
bool f900_enroll(const f900_enroll_data_t* enroll_data, uint16_t* user_id);
bool f900_delete_user(uint16_t user_id);
bool f900_delete_all_users(void);
bool f900_get_user_info(uint16_t user_id, f900_user_info_t* user_info);
bool f900_power_down(void);

// Face reset function
// WARNING: Calling during successful enrollment will clear all progress
// Only use to:
// - Abort failed enrollment attempts
// - Start new enrollment sessions
// - Recover from errors
bool f900_face_reset(void);

// Get all registered user IDs 
bool f900_get_all_user_ids(uint16_t* user_ids, uint16_t* count);

// Enable/disable function
void f900_set_enable(bool enable);

// Set security threshold levels
bool f900_set_threshold_level(uint8_t verify_level, uint8_t liveness_level);

// Function for encrypted communication
bool f900_set_encryption_key(const uint8_t key[16]);
bool f900_init_encryption_session(const uint8_t seed[4]);
bool f900_send_encrypted_message(f900_msg_id_t msg_id, const uint8_t* data, uint16_t size);
bool f900_receive_encrypted_message(f900_message_t* msg);

// Image capture functions
bool f900_capture_images(uint8_t image_count, uint8_t start_number);
bool f900_get_saved_image_size(uint8_t image_number, uint32_t* size);
bool f900_get_saved_image(uint8_t image_number, uint32_t offset, uint32_t chunk_size, uint8_t* buffer);


#ifdef __cplusplus
}
#endif

#endif /* _F900_H_ */
