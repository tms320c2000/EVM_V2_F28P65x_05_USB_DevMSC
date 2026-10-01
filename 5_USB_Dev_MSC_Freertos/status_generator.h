// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — Dynamic Status File Generator Header
// File: status_generator.h
// ==============================================================================

#ifndef STATUS_GENERATOR_H
#define STATUS_GENERATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "c28x_uint8_compat.h"	// C28x는 stdint.h가 uint8_t를 정의하지 않아 직접 보강

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t uptimeSec;
    float    adcA0Voltage;
    uint16_t adcA0Raw;
    uint8_t  tactSwitches;     // bit 0..3: SW1..SW4
    uint8_t  toggleSwitches;   // bit 0..7: TOGGLE1..TOGGLE8
    uint16_t currentLedState;
} BoardTelemetry_t;

extern BoardTelemetry_t g_telemetry;

/**
 * 실시간 상태 텍스트 생성 (STATUS.TXT 내용)
 */
uint16_t StatusGenerator_generate(char *outBuffer, uint16_t maxLen);

#ifdef __cplusplus
}
#endif

#endif // STATUS_GENERATOR_H
