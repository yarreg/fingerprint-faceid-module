#ifndef _SENSOR_MANAGER_H_
#define _SENSOR_MANAGER_H_   

#include <freertos/FreeRTOS.h>                                                          
#include <freertos/semphr.h>                                                            

typedef enum {                                                                          
    SENSOR_TYPE_R502 = 0,                                                               
    SENSOR_TYPE_F900 = 1,
    SENSOR_VL53L0X = 2,                                                            
    SENSOR_TYPE_MAX                                                                     
} sensor_type_t;                                                                        

typedef enum {                                                                          
    SPRIORITY_LOW = 0,                                                                  
    SPRIORITY_HIGH = 1                                                                  
} sensor_priority_t;                                                                    

typedef void (*sensor_release_callback_t)(char *requester);                                 

// Sensor access request structure                                                      
typedef struct {                                                                        
    sensor_priority_t priority;                                                         
    char* owner;                                                                        
    TickType_t timeout;                                                                 
} sensor_access_request_t;                                                              

// Public API                                                                           
void sensor_manager_init(void);                                                         
bool sensor_request_access(uint32_t sensor_mask, const sensor_access_request_t* request); 
void sensor_release_access(sensor_type_t type, char *owner);                            
sensor_priority_t sensor_current_priority(sensor_type_t type); 
bool sensor_is_release_requested(sensor_type_t type, const char* owner);  

 #endif /* _SENSOR_MANAGER_H_ */
