// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — Dynamic Status File Generator Source
// File: status_generator.c
// ==============================================================================

#include "status_generator.h"
#include "config_parser.h"
#include <stdio.h>

BoardTelemetry_t g_telemetry = { 0 };

uint16_t StatusGenerator_generate(char *outBuffer, uint16_t maxLen) {
    uint32_t hrs;
    uint32_t mins;
    uint32_t secs;
    uint32_t mV;
    int n;

    hrs  = g_telemetry.uptimeSec / 3600;
    mins = (g_telemetry.uptimeSec % 3600) / 60;
    secs = g_telemetry.uptimeSec % 60;
    mV   = ((uint32_t)g_telemetry.adcA0Raw * 3300UL) / 4095UL;

    n = snprintf(outBuffer, maxLen,
        "================================================\r\n"
        " SyncWorks F28P659 EVM V2 Live Board Status\r\n"
        " (Reopen file or run monitor.bat to refresh)\r\n"
        "================================================\r\n"
        "Uptime          : %02lu:%02lu:%02lu\r\n"
        "CPU Clock       : 200 MHz (Dual C28x)\r\n"
        "ADC A0 (VR1)    : %lu.%03lu V (%4u / 4095)\r\n"
        "Tact SW (1~4)   : [%s] [%s] [%s] [%s]\r\n"
        "Toggle SW (1~8) : 0x%02X\r\n"
        "LED Pattern     : %u (Speed: %u ms)\r\n"
        "LED State (1~16): 0x%04X\r\n"
        "PWM1 Duty       : %u %%\r\n",
        hrs, mins, secs,
        mV / 1000UL, mV % 1000UL,
        g_telemetry.adcA0Raw,
        (g_telemetry.tactSwitches & 0x01) ? "ON " : "OFF",
        (g_telemetry.tactSwitches & 0x02) ? "ON " : "OFF",
        (g_telemetry.tactSwitches & 0x04) ? "ON " : "OFF",
        (g_telemetry.tactSwitches & 0x08) ? "ON " : "OFF",
        g_telemetry.toggleSwitches,
        g_sysConfig.ledPattern,
        g_sysConfig.ledSpeedMs,
        g_telemetry.currentLedState,
        g_sysConfig.pwmDutyPct
    );

    if (n < 0) return 0;
    return (uint16_t)n;
}
