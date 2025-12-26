// VL53L0X control
// Copyright © 2019 Adrian Kennard, Andrews & Arnold Ltd. See LICENCE file for details. GPL 3.0

#ifndef VL53L0X_H
#define VL53L0X_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>


typedef enum
{ VcselPeriodPreRange, VcselPeriodFinalRange } vl53l0x_vcselPeriodType;

// confgure the VL53L0X sensor
bool vl53l0x_config(int8_t port, int8_t scl, int8_t sda, int8_t xshut, int8_t irq, uint8_t address, uint8_t io_2v8);

// Functions returning const char * are OK for NULL, else error string
// Initialise the VL53L0X
const char *vl53l0x_init ();

// End I2C and free the structure
void vl53l0x_end ();

void vl53l0x_setAddress (uint8_t new_addr);
uint8_t vl53l0x_getAddress ();

void vl53l0x_writeReg8Bit (uint8_t reg, uint8_t value);
void vl53l0x_writeReg16Bit (uint8_t reg, uint16_t value);
void vl53l0x_writeReg32Bit (uint8_t reg, uint32_t value);
uint8_t vl53l0x_readReg8Bit (uint8_t reg);
uint16_t vl53l0x_readReg16Bit (uint8_t reg);
uint32_t vl53l0x_readReg32Bit (uint8_t reg);

void vl53l0x_writeMulti (uint8_t reg, uint8_t const *src, uint8_t count);
void vl53l0x_readMulti (uint8_t reg, uint8_t * dst, uint8_t count);

const char *vl53l0x_setSignalRateLimit (float limit_Mcps);
float vl53l0x_getSignalRateLimit ();

const char *vl53l0x_setMeasurementTimingBudget (uint32_t budget_us);
uint32_t vl53l0x_getMeasurementTimingBudget ();

const char *vl53l0x_setVcselPulsePeriod (vl53l0x_vcselPeriodType type, uint8_t period_pclks);
uint8_t vl53l0x_getVcselPulsePeriod (vl53l0x_vcselPeriodType type);

void vl53l0x_clearInterrupt();

void vl53l0x_startContinuous (uint32_t period_ms);
void vl53l0x_stopContinuous ();
uint16_t vl53l0x_readResultRangeStatus();
uint16_t vl53l0x_readRangeContinuousMillimeters ();
uint16_t vl53l0x_readRangeSingleMillimeters ();

void vl53l0x_setTimeout (uint16_t timeout);
uint16_t vl53l0x_getTimeout ();
int vl53l0x_timeoutOccurred ();
int vl53l0x_i2cFail ();

void vl53l0x_addInterruptHandler(void (*handler)(void *), void *arg);

#endif
