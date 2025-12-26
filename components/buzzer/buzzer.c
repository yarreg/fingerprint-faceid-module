#include "buzzer.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUZZER_TIMER     LEDC_TIMER_0
#define BUZZER_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL   LEDC_CHANNEL_0
#define BUZZER_RES       LEDC_TIMER_8_BIT          // 0-255
#define BUZZER_DUTY      128                       // 50 %
#define BUZZER_CLK       LEDC_AUTO_CLK
#define BUZZER_DEF_FREQ  4000                      // Default frequency 4 kHz

typedef struct {
    uint16_t freq;
    uint16_t ms;
} note_t;

static bool g_ready = false;

void buzzer_init(gpio_num_t pin) {
    ledc_timer_config_t tcfg = {
        .speed_mode       = BUZZER_MODE,
        .timer_num        = BUZZER_TIMER,
        .duty_resolution  = BUZZER_RES,
        .freq_hz          = BUZZER_DEF_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);

    ledc_channel_config_t ccfg = {
        .gpio_num   = pin,
        .speed_mode = BUZZER_MODE,
        .channel    = BUZZER_CHANNEL,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,           // off on start
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);

    g_ready = true;
}

static inline void set_freq(uint32_t hz) {
    if (g_ready) ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, hz);
}

static inline void buzzer_on(void) {
    if (g_ready) {
        ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
        ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    }
}

static inline void buzzer_off(void) {
    if (g_ready) {
        ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
        ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    }
}

void buzzer_tone(uint32_t freq_hz, uint16_t duration_ms) {
    if (!g_ready) return;
    set_freq(freq_hz);
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
}

static void play_melody(const note_t *mel, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (mel[i].freq) {
            buzzer_tone(mel[i].freq, mel[i].ms);
        } else {
            vTaskDelay(pdMS_TO_TICKS(mel[i].ms));  // pause
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // small pause between notes
    }
}



/*--------------------------------------------------------------------*/
void buzzer_short_beep(void) {
    static const note_t short_beep[] = {
        { 1000, 100 }, { 0, 50 }, { 1000, 100 },
    };
    play_melody(short_beep, sizeof short_beep / sizeof short_beep[0]);
}

void buzzer_long_beep(void) {
    static const note_t long_beep[] = {
        { 1000, 500 },
    };
    play_melody(long_beep, sizeof long_beep / sizeof long_beep[0]);
}

void buzzer_success_chime(void){
    static const note_t ok[] = {
        { 1500, 120 }, { 2000, 150 }, { 2500, 180 },
    };
    play_melody(ok, sizeof ok / sizeof ok[0]);
}

void buzzer_error_honk(void) {
    static const note_t err[] = {
        {  800, 400 }, { 0, 80 }, { 800, 150 }, { 0, 60 }, { 800, 150 },
    };
    play_melody(err, sizeof err / sizeof err[0]);
}
