
#include "sensor_manager.h" 
#include <stdint.h>                                                            
#include <string.h>     
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"                                                                

typedef struct {                                                                        
    SemaphoreHandle_t semaphore;                                                            
    sensor_priority_t current_prio;                                                     
    const char* current_owner;                                                                
    bool release_requested;                                        
} sensor_state_t;

// Global mutex to protect entire acquisition process
static SemaphoreHandle_t global_acq_mutex = NULL;

// Sensor acquisition order to prevent deadlocks
static const sensor_type_t sensor_acquisition_order[] = {
    SENSOR_TYPE_R502,
    SENSOR_TYPE_F900,
    SENSOR_VL53L0X
};

static sensor_state_t sensors[SENSOR_TYPE_MAX];                                         

void sensor_manager_init(void) {                                                        
    // Create global acquisition mutex with priority inheritance
    global_acq_mutex = xSemaphoreCreateMutex();

    for (uint8_t i = 0; i < SENSOR_TYPE_MAX; i++) {                                         
        // Use mutexes instead of binary semaphores for priority inheritance
        sensors[i].semaphore = xSemaphoreCreateMutex();

        sensors[i].current_prio = SPRIORITY_LOW;                                        
        sensors[i].current_owner = NULL;                                                
        sensors[i].release_requested = false;                                                
    }                                                                                   
}                                                                                       

bool sensor_request_access(uint32_t sensor_mask, const sensor_access_request_t* request) {
    // Validate mask is within valid sensor range
    if (sensor_mask == 0 || (sensor_mask & ~((1 << SENSOR_TYPE_MAX) - 1))) {
        return false;
    }

    // 1. Acquire global acquisition mutex first
    if (xSemaphoreTake(global_acq_mutex, request->timeout) != pdTRUE) {
        return false;
    }

    // 2. Collect requested sensors in predefined acquisition order
    uint8_t sensors_to_acquire[SENSOR_TYPE_MAX];
    uint8_t count = 0;

    // Follow predefined order to prevent deadlocks between different requests
    for (uint8_t i = 0; i < sizeof(sensor_acquisition_order)/sizeof(sensor_type_t); i++) {
        sensor_type_t type = sensor_acquisition_order[i];
        if (sensor_mask & (1 << type)) {
            sensors_to_acquire[count++] = type;
        }
    }

    // 3. Attempt atomic acquisition of all sensors
    bool success = true;
    TickType_t remaining_ticks = request->timeout;
    TickType_t start_ticks = xTaskGetTickCount();

    for (uint8_t i = 0; i < count; i++) {
        sensor_state_t* sensor = &sensors[sensors_to_acquire[i]];

        // Check if we need to request release from current owner
        if (xSemaphoreTake(sensor->semaphore, 0) != pdTRUE) {
            if (request->priority > sensor->current_prio) {
                sensor->release_requested = true;
            }

            // Calculate remaining timeout time
            remaining_ticks = request->timeout - (xTaskGetTickCount() - start_ticks);
            if (remaining_ticks <= 0) {
                success = false;
                break;
            }

            // Blocking acquire with remaining timeout
            if (xSemaphoreTake(sensor->semaphore, remaining_ticks) != pdTRUE) {
                success = false;
                break;
            }
        }
    }

    // 4. Handle success/failure
    if (success) {
        // Update state for all acquired sensors
        for (uint8_t i = 0; i < count; i++) {
            sensor_state_t* sensor = &sensors[sensors_to_acquire[i]];
            sensor->current_prio = request->priority;
            sensor->current_owner = request->owner;
            sensor->release_requested = false;
        }
    } else {
        // Release any sensors we did acquire
        for (uint8_t i = 0; i < count; i++) {
            sensor_state_t* sensor = &sensors[sensors_to_acquire[i]];
            if (xSemaphoreGetMutexHolder(sensor->semaphore) == xTaskGetCurrentTaskHandle()) {
                xSemaphoreGive(sensor->semaphore);
                sensor->current_owner = NULL;
                sensor->current_prio = SPRIORITY_LOW;
                sensor->release_requested = false;
            }
        }
    }

    // Always release global mutex before returning
    xSemaphoreGive(global_acq_mutex);

    return success;
}                                                                                       

void sensor_release_access(sensor_type_t type, char* owner) {                           
    sensor_state_t* sensor = &sensors[type];                                      

     if (sensor->current_owner != NULL && strcmp(sensor->current_owner, owner) == 0) {                                 
        sensor->current_owner = NULL;                                                   
        sensor->current_prio = SPRIORITY_LOW;                                           
        sensor->release_requested = false;
        xSemaphoreGive(sensor->semaphore);                                              
    }                                                                                
}                                                                                       

sensor_priority_t sensor_current_priority(sensor_type_t type) {                         
   sensor_state_t *sensor = &sensors[type];

    if (xSemaphoreTake(sensor->semaphore, 0) == pdTRUE) {
        // if sensor is free = "LOW"
        xSemaphoreGive(sensor->semaphore);
        return SPRIORITY_LOW;
    }

    return sensor->current_prio;                                                                       
}  

bool sensor_is_release_requested(sensor_type_t type, const char* owner) {
    sensor_state_t* sensor = &sensors[type];

    // check if the owner is the current owner and release is requested
    if (sensor->current_owner && strcmp(sensor->current_owner, owner) == 0) {
        return sensor->release_requested;
    }
    return false;
}
