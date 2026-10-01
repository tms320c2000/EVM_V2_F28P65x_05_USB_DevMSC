// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — INI Configuration Parser Source
// File: config_parser.c
// ==============================================================================

#include "config_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

SystemConfig_t g_sysConfig;

void ConfigParser_init(void) {
    g_sysConfig.ledPattern = 2;       // 기본: 왕복 점등 (Bounce)
    g_sysConfig.ledSpeedMs = 100;     // 기본: 100ms
    g_sysConfig.pwmDutyPct = 50;      // 기본: 50%
    g_sysConfig.updated = true;
}

void ConfigParser_generateDefaultText(char *outBuffer, uint16_t maxLen) {
    snprintf(outBuffer, maxLen,
        "; ==================================================\r\n"
        "; SyncWorks TMS320F28P659 EVM V2 Configuration File\r\n"
        "; Note: Modify values and save (Ctrl+S) in Notepad.\r\n"
        "; ==================================================\r\n"
        "LED_PATTERN  = %u   ; 1:Shift, 2:Bounce, 3:Blink, 4:Counter\r\n"
        "LED_SPEED_MS = %u   ; Blinking speed (20 - 2000 ms)\r\n"
        "PWM_DUTY_PCT = %u   ; Analog/PWM output duty (0 - 100 %%)\r\n",
        g_sysConfig.ledPattern,
        g_sysConfig.ledSpeedMs,
        g_sysConfig.pwmDutyPct
    );
}

void ConfigParser_parse(const char *iniText) {
    if (!iniText) return;

    const char *line = iniText;
    while (*line != '\0') {
        // Skip leading spaces
        while (*line == ' ' || *line == '\t') line++;

        // Ignore comments and empty lines
        if (*line != ';' && *line != '#' && *line != '\r' && *line != '\n') {
            if (strncmp(line, "LED_PATTERN", 11) == 0) {
                const char *eq = strchr(line, '=');
                if (eq) {
                    uint16_t val = (uint16_t)atoi(eq + 1);
                    if (val >= 1 && val <= 4) {
                        g_sysConfig.ledPattern = val;
                        g_sysConfig.updated = true;
                    }
                }
            } else if (strncmp(line, "LED_SPEED_MS", 12) == 0) {
                const char *eq = strchr(line, '=');
                if (eq) {
                    uint16_t val = (uint16_t)atoi(eq + 1);
                    if (val >= 20 && val <= 5000) {
                        g_sysConfig.ledSpeedMs = val;
                        g_sysConfig.updated = true;
                    }
                }
            } else if (strncmp(line, "PWM_DUTY_PCT", 12) == 0) {
                const char *eq = strchr(line, '=');
                if (eq) {
                    uint16_t val = (uint16_t)atoi(eq + 1);
                    if (val <= 100) {
                        g_sysConfig.pwmDutyPct = val;
                        g_sysConfig.updated = true;
                    }
                }
            }
        }

        // Advance to next line
        const char *next = strchr(line, '\n');
        if (next) {
            line = next + 1;
        } else {
            break;
        }
    }
}
