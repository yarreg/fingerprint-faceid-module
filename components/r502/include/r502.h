#ifndef __R502_H__
#define __R502_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#define R502_PACKET_SIZE 128
#define R502_DEFAULT_ADDRESS 0xFFFFFFFF
#define R502_DEFAULT_TIMEOUT_MS 5000
#define R502_DEFAULT_BAUD_RATE 57600

#define R502_HEADER_SIZE 9

// Command packet sizes (including checksum)
#define R502_AURALED_PACKET_SIZE 16
#define R502_GENIMG_PACKET_SIZE 12
#define R502_SEARCH_PACKET_SIZE 17
#define R502_VFYPWD_PACKET_SIZE 16
#define R502_IMG2TZ_PACKET_SIZE 13
#define R502_TEMPLATENUM_PACKET_SIZE 12
#define R502_REGMODEL_PACKET_SIZE 12
#define R502_STORE_PACKET_SIZE 15
#define R502_HANDSHAKE_PACKET_SIZE 12
#define R502_SETPWD_PACKET_SIZE 16
#define R502_DELETECHAR_PACKET_SIZE 16
#define R502_EMPTY_PACKET_SIZE 12
#define R502_READINDEXTABLE_PACKET_SIZE 13
#define R502_READ_SYS_PARA_PACKET_SIZE 12
#define R502_SET_SYS_PARA_PACKET_SIZE 14

typedef struct {
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int en_pin;  // Enable pin (optional, set to -1 if not used)
    int irq_pin; // IRQ pin (optional, set to -1 if not used)
    uint32_t address;
} r502_config_t;

typedef struct {
    uint8_t conf_code;
} r502_generic_reply;

typedef struct {
    uint8_t conf_code;
    uint16_t index;
} r502_templatenum_reply;

typedef struct {
    uint8_t conf_code;
    uint16_t index;
    uint16_t match_score;
} r502_search_reply;

typedef struct {
    uint8_t conf_code;
    uint8_t index_page[32];
} r502_indextable_reply;

typedef struct {
    uint8_t conf_code;
    uint16_t status_register;
    uint16_t sys_id_code;
    uint16_t lib_size;
    uint16_t security_level;
    uint32_t device_address;
    uint16_t data_packet_size;
    uint16_t baud_rate;
 } r502_syspara_reply;

  typedef struct {
     bool busy;       // System is executing commands
     bool pass;       // Found matching finger
     bool pwd;        // Verified device's handshaking password
     bool img_buf_stat; // Image buffer contains valid image
 } r502_status_t;

 typedef enum {
    R502_PARAM_BAUD_RATE = 4,      // Baud rate control (N=1/2/4/6/12)
    R502_PARAM_SECURITY_LEVEL = 5, // Security level (1-5)
    R502_PARAM_PACKET_SIZE = 6     // Data package length (0=32,1=64,2=128,3=256 bytes)
 } r502_param_num_t;

/* Callback function type */
typedef void (*r502_irq_callback)(void);

// Initialization
void r502_init(r502_config_t config);

void r502_set_timeout(uint32_t timeout_ms);
void r502_set_enable(bool enable);
bool r502_is_enabled(void);

void r502_add_irq_callback(r502_irq_callback callback);
void r502_remove_irq_callback(r502_irq_callback callback);
void r502_clear_irq_callbacks();

esp_err_t r502_get_status(r502_status_t *status);

// Module commands
esp_err_t r502_setsyspara(r502_param_num_t param_num, uint8_t value, r502_generic_reply *reply);
esp_err_t r502_readsyspara(r502_syspara_reply *reply);
esp_err_t r502_auraledconfig(uint8_t control, uint8_t speed, uint8_t color, uint8_t times, r502_generic_reply *reply);
esp_err_t r502_genimg(r502_generic_reply *reply);
esp_err_t r502_img2tz(uint8_t buffer, r502_generic_reply *reply);
esp_err_t r502_search(uint8_t buffer, uint16_t start, uint16_t count, r502_search_reply *reply);
esp_err_t r502_vfypwd(uint32_t password, r502_generic_reply *reply);
esp_err_t r502_templatenum(r502_templatenum_reply *reply);
esp_err_t r502_regmodel(r502_generic_reply *reply);
esp_err_t r502_store(uint8_t buffer, uint16_t index, r502_generic_reply *reply);
esp_err_t r502_handshake(r502_generic_reply *reply);
esp_err_t r502_setpwd(uint32_t new_password, r502_generic_reply *reply);
esp_err_t r502_deletechar(uint16_t start, uint16_t count, r502_generic_reply *reply);
esp_err_t r502_empty(r502_generic_reply *reply);
esp_err_t r502_readindextable(uint8_t page, r502_indextable_reply *reply);

#endif // __R502_H__
