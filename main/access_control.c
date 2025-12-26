#include "access_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "vl53l0x.h"
#include "r502.h"
#include "f900.h"
#include "tabledb.h"
#include "buzzer.h"
#include "table_types.h"

static const char *TAG = "ACCESS_CONTROL";

// Event group for triggering sensor tasks
static EventGroupHandle_t xAccessControlEventGroup;
#define EVENT_TRIGGER_DISTANCE_REACHED BIT0
#define EVENT_TRIGGER_VL53L0X_MEASURE_DONE BIT1

/************ Algorithm Constants ************/
// Interval between measurements (approximately)
static const uint16_t MEASUREMENT_INTERVAL_MS = 200; // ms

// Time thresholds (in ms)
// 1 second – user must be within 50cm continuously for detection
static const uint16_t DETECTION_DURATION_MS =1000;
// 10 seconds – after flag is set, do not clear even if user leaves
static const uint16_t MIN_ACTIVE_DURATION_MS =10000;
// 3 seconds – after 10 sec, allow flag removal if user is absent (>50cm)
static const uint16_t REMOVAL_DURATION_MS = 3000;
// 60 seconds – maximum time the flag remains active (then cleared even if user is present)
static const uint16_t MAX_ACTIVE_DURATION_MS = 60000;
// 10 seconds – period before a new detection is allowed after flag removal
static const uint16_t COOLDOWN_DURATION_MS =10000;

// Macros to calculate cycle counts from time durations
#define MS_TO_CYCLES(ms) ((ms) / MEASUREMENT_INTERVAL_MS)

// Converting time thresholds into number of measurement cycles:
static const uint16_t DETECTION_COUNT_THRESHOLD = MS_TO_CYCLES(DETECTION_DURATION_MS);  // 5 cycles
static const uint16_t MIN_ACTIVE_COUNT          = MS_TO_CYCLES(MIN_ACTIVE_DURATION_MS); // 50 cycles
static const uint16_t REMOVAL_COUNT_THRESHOLD   = MS_TO_CYCLES(REMOVAL_DURATION_MS);    // 15 cycles
static const uint16_t MAX_ACTIVE_COUNT         = MS_TO_CYCLES(MAX_ACTIVE_DURATION_MS); // 300 cycles
static const uint16_t COOLDOWN_COUNT_THRESHOLD = MS_TO_CYCLES(COOLDOWN_DURATION_MS);   // 50 cycles

// Distance threshold (50 cm = 500 mm)
static const uint16_t DISTANCE_THRESHOLD_MM = 500;

/************ State Machine Definition ************/
typedef enum {
    // Waiting for the user to approach (i.e., be within the distance threshold)
    ACCESS_STATE_WAITING_FOR_USER = 0,
    // User detected continuously (access granted; latched for a minimum of 10 seconds)
    ACCESS_STATE_USER_CONFIRMED,
    // After the minimum period, monitoring the user’s presence; if the user is absent for 3 sec, access is revoked
    ACCESS_STATE_USER_MONITORING,
    // Cooldown period after access is revoked, delaying new detection for 10 seconds
    ACCESS_STATE_COOLDOWN
} access_state_t;

static access_control_callback_t user_fingerprint_callback = NULL;
static access_control_callback_t user_face_callback = NULL;

static void IRAM_ATTR vl53l0x_irq_handler(void *arg) {
    // Set freertos event flag
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(xAccessControlEventGroup, EVENT_TRIGGER_VL53L0X_MEASURE_DONE, &xHigherPriorityTaskWoken);
}

// ToF Task
static void tof_task(void *arg) {
    vl53l0x_addInterruptHandler(vl53l0x_irq_handler, NULL);

    ESP_LOGI(TAG, "Starting ToF sensor task...");

    uint16_t distance = 0;

    // Variables for implementing the state machine
    access_state_t state = ACCESS_STATE_WAITING_FOR_USER;
    // Counts consecutive measurements below threshold (in detection phase)
    uint16_t detection_counter = 0;
    // Counts cycles in the flag active state (since flag was set)
    uint16_t state_counter = 0;
    // Counts consecutive measurements above threshold (for flag removal)
    uint16_t above_counter = 0;
    // Counts cycles in the cooldown state
    uint16_t cooldown_counter = 0;

    vl53l0x_clearInterrupt();
    vl53l0x_startContinuous(MEASUREMENT_INTERVAL_MS);

    while (1) {
        // Wait for the sensor measurement done event (with timeout)
        EventBits_t event_bits =
            xEventGroupWaitBits(xAccessControlEventGroup, EVENT_TRIGGER_VL53L0X_MEASURE_DONE,
                                pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
        if (!(event_bits & EVENT_TRIGGER_VL53L0X_MEASURE_DONE)) {
            ESP_LOGE(TAG, "ToF sensor timeout.");
            continue;
        }

        // Read the measurement result
        distance = vl53l0x_readResultRangeStatus();
        xEventGroupClearBits(xAccessControlEventGroup, EVENT_TRIGGER_VL53L0X_MEASURE_DONE);
        vl53l0x_clearInterrupt();

        //ESP_LOGI(TAG, "Distance: %dmm", distance);

        switch (state) {
        case ACCESS_STATE_WAITING_FOR_USER:
            // If the distance is below the threshold, increment the detection counter
            if (distance < DISTANCE_THRESHOLD_MM) {
                detection_counter++;
            } else {
                detection_counter = 0;
            }
            // If the condition holds for 1 second (5 cycles), set the flag
            if (detection_counter >= DETECTION_COUNT_THRESHOLD) {
                ESP_LOGI(TAG, "User detected within %d mm for %d ms. Setting flag.",
                         DISTANCE_THRESHOLD_MM, DETECTION_DURATION_MS);
                xEventGroupSetBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED);
                state             = ACCESS_STATE_USER_CONFIRMED;
                state_counter     = 0;
                detection_counter = 0;
            }
            break;

        case ACCESS_STATE_USER_CONFIRMED:
            state_counter++;
            // During the first 10 seconds (50 cycles), do not clear the flag even if the user leaves
            if (state_counter >= MIN_ACTIVE_COUNT) {
                state         = ACCESS_STATE_USER_MONITORING;
                above_counter = 0;
            }
            // If the maximum flag active time is reached, clear the flag
            if (state_counter >= MAX_ACTIVE_COUNT) {
                ESP_LOGI(TAG, "Maximum active duration reached. Clearing flag.");
                xEventGroupClearBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED);
                state            = ACCESS_STATE_COOLDOWN;
                cooldown_counter = 0;
            }
            break;

        case ACCESS_STATE_USER_MONITORING:
            state_counter++;
            // If the measured distance is greater than the threshold, count consecutive "absence" cycles
            if (distance > DISTANCE_THRESHOLD_MM) {
                above_counter++;
            } else {
                above_counter = 0;
            }
            // If the user is absent for 3 seconds consecutively (15 cycles), clear the flag
            if (above_counter >= REMOVAL_COUNT_THRESHOLD) {
                ESP_LOGI(TAG, "User absent for %d ms. Clearing flag.", REMOVAL_DURATION_MS);
                xEventGroupClearBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED);
                state            = ACCESS_STATE_COOLDOWN;
                cooldown_counter = 0;
            }
            // Also, if the maximum flag active time is reached, clear the flag
            if (state_counter >= MAX_ACTIVE_COUNT) {
                ESP_LOGI(TAG, "Maximum active duration reached. Clearing flag.");
                xEventGroupClearBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED);
                state            = ACCESS_STATE_COOLDOWN;
                cooldown_counter = 0;
            }
            break;

        case ACCESS_STATE_COOLDOWN:
            cooldown_counter++;
            // After 10 seconds (50 cycles) cooldown, new detection is allowed
            if (cooldown_counter >= COOLDOWN_COUNT_THRESHOLD) {
                ESP_LOGI(TAG, "Cooldown period ended. Ready for new detection.");
                state             = ACCESS_STATE_WAITING_FOR_USER;
                detection_counter = 0;
            }
            break;

        default:
            state = ACCESS_STATE_WAITING_FOR_USER;
            break;
        }

        // Optionally, add a delay for the next cycle:
        // vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
        // However, the loop is typically driven by sensor interrupts.

        // Add a small delay at the end of each loop iteration
        // to prevent watchdog timeouts
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Fingerprint Task
static void fingerprint_task(void *arg) {
    r502_generic_reply reply;

    while (1) {
        // Wait for the trigger signal
        xEventGroupWaitBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED, pdTRUE,
                            pdFALSE, portMAX_DELAY);

        // Turn on the sensor's LED (e.g., breathing blue light)
        r502_auraledconfig(1, 100, 2, 0, &reply);

        // Wait for finger detection
        int retries = 0;
        while (retries++ < 15) { // Total wait time 15 * 200ms = 3 seconds
            if (r502_genimg(&reply) == ESP_OK && reply.conf_code == 0x00) {
                // Finger detected
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if (retries >= 15) {
            ESP_LOGW(TAG, "No finger detected");
            // Turn off the sensor's LED
            r502_auraledconfig(4, 0, 0, 0, &reply);
            continue;
        }

        // Convert image to character file
        if (r502_img2tz(1, &reply) != ESP_OK || reply.conf_code != 0x00) {
            ESP_LOGW(TAG, "Failed to convert image to character file");
            // Handle error
            continue;
        }

        // Search for matching fingerprint
        r502_search_reply search_reply;
        if (r502_search(1, 0, 0xFFFF, &search_reply) == ESP_OK &&
            search_reply.conf_code == 0x00) {
            uint16_t            matched_id = search_reply.index;

            // Invoke fingerprint verification callback instead of inline handling
            if (user_fingerprint_callback != NULL) {
                user_fingerprint_callback(matched_id);
            }
        } else {
            ESP_LOGW(TAG, "No matching fingerprint found");
            buzzer_error_honk();
        }

        // Turn off the sensor's LED
        r502_auraledconfig(4, 0, 0, 0, &reply);

    }
}

// Face Task
// F900 will generate heat during operation and should not be used for a long time.
// It is recommended to power off and reset after working for a few minutes
static void face_task(void *arg) {
    const TickType_t cooldown_period   = pdMS_TO_TICKS(30000); // 30 seconds
    TickType_t       last_attempt_time = xTaskGetTickCount() - cooldown_period;

    ESP_LOGI(TAG, "Starting Face detection task");

    while (1) {
        // Wait for the trigger signal
        xEventGroupWaitBits(xAccessControlEventGroup, EVENT_TRIGGER_DISTANCE_REACHED, pdTRUE,
                            pdFALSE, portMAX_DELAY);

        TickType_t current_time = xTaskGetTickCount();
        if (current_time - last_attempt_time < cooldown_period) {
            ESP_LOGI(TAG, "Face scan cooldown active");
            continue;
        }

        ESP_LOGI(TAG, "Face detection started");

        // Start face verification with a timeout of 30 seconds
        f900_user_info_t user_info;
        if (f900_verify(30, &user_info)) {
            uint16_t user_id = (user_info.user_id_heb << 8) | user_info.user_id_leb;

            // Invoke callback when faceid verified
            if (user_face_callback != NULL) {
                user_face_callback(user_id);
            }
        } else {
            ESP_LOGW(TAG, "Face verification failed or timed out");
            buzzer_error_honk();
        }

        last_attempt_time = xTaskGetTickCount();
    }
}

void access_control_start() {
    // Initialize the event group before starting tasks
    xAccessControlEventGroup = xEventGroupCreate();
    if (xAccessControlEventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create Access Control Event Group!");
        return;
    }
    xTaskCreate(tof_task, "ToF Task", 4096, NULL, 5, NULL);
    xTaskCreate(fingerprint_task, "Fingerprint Task", 4096, NULL, 5, NULL);
    xTaskCreate(face_task, "Face Task", 7168, NULL, 5, NULL);
}


void access_control_set_fingerprint_success_callback(access_control_callback_t callback) {
    user_fingerprint_callback = callback;
}

void access_control_set_face_success_callback(access_control_callback_t callback) {
    user_face_callback = callback;
}