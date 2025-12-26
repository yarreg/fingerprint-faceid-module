#ifndef _ACCESS_CONTROL_H_
#define _ACCESS_CONTROL_H_

#include <stdint.h>


typedef void (*access_control_callback_t)(uint32_t user_id);


void  access_control_start();

void access_control_set_fingerprint_success_callback(access_control_callback_t callback);
void access_control_set_face_success_callback(access_control_callback_t callback);

#endif /* _ACCESS_CONTROL_H_ */
