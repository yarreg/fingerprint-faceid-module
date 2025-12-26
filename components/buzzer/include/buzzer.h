#ifndef _BUZZER_H_
#define _BUZZER_H_

#include <stdint.h>
#include "driver/gpio.h"


void buzzer_init(gpio_num_t pin);
void buzzer_tone(uint32_t freq_hz, uint16_t duration_ms);
void buzzer_short_beep();
void buzzer_long_beep();
void buzzer_success_chime();
void buzzer_error_honk();


#endif