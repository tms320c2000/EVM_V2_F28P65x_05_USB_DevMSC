// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — INI Configuration Parser Header
// File: config_parser.h
// ==============================================================================

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t ledPattern;    // 1: Sequential, 2: Bounce, 3: All Blink, 4: Binary Counter
    uint16_t ledSpeedMs;    // LED 전환 속도 (ms)
    uint16_t pwmDutyPct;    // PWM 출력 듀티 (0 ~ 100%)
    bool     updated;       // 변경 감지 플래그
} SystemConfig_t;

extern SystemConfig_t g_sysConfig;

/**
 * 기본 설정 초기화
 */
void ConfigParser_init(void);

/**
 * INI 텍스트 파싱
 */
void ConfigParser_parse(const char *iniText);

/**
 * 기본 CONFIG.INI 템플릿 문자열 생성
 */
void ConfigParser_generateDefaultText(char *outBuffer, uint16_t maxLen);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_PARSER_H
