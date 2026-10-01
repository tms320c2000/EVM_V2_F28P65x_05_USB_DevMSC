// ==============================================================================
// 파일이름 : 5_USB_Dev_MSC_Driverlib.c
// 대상장치 : TMS320F28X EVM V2, TMS320F28P65x(F28P650DK9 / F28P659DK8-Q1) 모듈
// 파일버전 : 1.00
// 설명     : 회로블록 (5) USB 통신 회로 — USB MSC 가상 이동식 디스크 (Virtual Disk)
//            F28P659를 PC에 연결하면 이동식 디스크(FAT12)로 인식되어,
//            README.TXT(안내), STATUS.TXT(실시간 센서/스위치 모니터링),
//            CONFIG.INI(메모장으로 파라미터 수정 시 LED/PWM 실시간 반영)를 제공합니다.
// ==============================================================================

#include "device.h"
#include "driverlib.h"
#include "usb_hal.h"
#include "usb_dev_msc.h"
#include "virtual_disk.h"
#include "config_parser.h"
#include "status_generator.h"

// -----------------------------------------------------------------------------
// Pin Definitions on EVM V2
// -----------------------------------------------------------------------------
#define NUM_LEDS            16
static const uint32_t s_ledGpios[NUM_LEDS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const uint32_t s_ledPinConfigs[NUM_LEDS] = {
    GPIO_0_GPIO0, GPIO_1_GPIO1, GPIO_2_GPIO2, GPIO_3_GPIO3,
    GPIO_4_GPIO4, GPIO_5_GPIO5, GPIO_6_GPIO6, GPIO_7_GPIO7,
    GPIO_8_GPIO8, GPIO_9_GPIO9, GPIO_10_GPIO10, GPIO_11_GPIO11,
    GPIO_12_GPIO12, GPIO_13_GPIO13, GPIO_14_GPIO14, GPIO_15_GPIO15
};

#define NUM_TACT_SW         4
static const uint32_t s_tactGpios[NUM_TACT_SW] = {
    70, 69, 68, 67  // SW1..SW4 (점퍼선 꼬임 방지 역순 배치)
};

#define NUM_TOGGLE_SW       8
static const uint32_t s_toggleGpios[NUM_TOGGLE_SW] = {
    25, 24, 23, 22, 21, 20, 19, 18 // TOGGLE1..TOGGLE8
};

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
static void initPeripherals(void);
static void initLeds(void);
static void initSwitches(void);
static void initAdcA(void);
static void initEPwm1(void);
static void updateLedOutputs(uint16_t mask);
static void processLedPattern(void);
static void readSensors(void);

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------
int main(void) {
    // 루프 타이머 변수 (C89 호환을 위해 블록 선두에 선언)
    uint32_t lastLedTick = 0;
    uint32_t lastSensorTick = 0;
    uint32_t lastSecTick = 0;
    uint32_t sysTicks = 0;

    // 1. 시스템 클럭 (200MHz) 및 주변장치 초기화
    Device_init();
    Device_initGPIO();

    // 2. 인터럽트 모듈 초기화
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // 3. USB용 60MHz AUXPLL 클럭 설정
    SysCtl_setAuxClock(DEVICE_AUXSETCLOCK_CONFIG_USB);

    // 4. 하드웨어 주변장치 초기화 (LED, 스위치, ADC, PWM)
    initPeripherals();

    // 5. USB 인터럽트 핸들러 등록 및 인터럽트 활성화
    Interrupt_register(INT_USBA, &USBDevMSC_intHandler);
    Interrupt_enable(INT_USBA);
    Interrupt_enableGlobal();
    ERTM;

    // 6. USB MSC 디바이스 스택 초기화 (디바이스 모드 설정 및 버스 연결)
    USBDevMSC_init();

    while (1) {
        // USB 대용량 전송 및 상태 머신 폴링
        USBDevMSC_process();

        // 1ms 타임베이스 (소프트웨어 지연 기반)
        DEVICE_DELAY_US(1000);
        sysTicks++;

        // 10ms 주기: 센서 및 스위치 입력 갱신
        if ((sysTicks - lastSensorTick) >= 10) {
            lastSensorTick = sysTicks;
            readSensors();
        }

        // 설정된 속도 주기: 16채널 LED 점등 패턴 갱신
        if ((sysTicks - lastLedTick) >= g_sysConfig.ledSpeedMs) {
            lastLedTick = sysTicks;
            processLedPattern();
        }

        // 1초 주기: Uptime 계수
        if ((sysTicks - lastSecTick) >= 1000) {
            lastSecTick = sysTicks;
            g_telemetry.uptimeSec++;
        }
    }
}

// -----------------------------------------------------------------------------
// Hardware Initialization
// -----------------------------------------------------------------------------
static void initPeripherals(void) {
    initLeds();
    initSwitches();
    initAdcA();
    initEPwm1();
}

static void initLeds(void) {
    uint16_t i;
    for (i = 0; i < NUM_LEDS; i++) {
        GPIO_setPinConfig(s_ledPinConfigs[i]);
        GPIO_setDirectionMode(s_ledGpios[i], GPIO_DIR_MODE_OUT);
        GPIO_setPadConfig(s_ledGpios[i], GPIO_PIN_TYPE_STD);
        GPIO_writePin(s_ledGpios[i], 0);
    }
}

static void initSwitches(void) {
    uint16_t i;

    // Tactile Switches (Active High)
    for (i = 0; i < NUM_TACT_SW; i++) {
        GPIO_setDirectionMode(s_tactGpios[i], GPIO_DIR_MODE_IN);
        GPIO_setPadConfig(s_tactGpios[i], GPIO_PIN_TYPE_STD);
    }

    // Toggle Switches (Active High)
    for (i = 0; i < NUM_TOGGLE_SW; i++) {
        GPIO_setDirectionMode(s_toggleGpios[i], GPIO_DIR_MODE_IN);
        GPIO_setPadConfig(s_toggleGpios[i], GPIO_PIN_TYPE_STD);
    }
}

static void initAdcA(void) {
    // A0(AIO227, B Side 111번) 아날로그 모드 활성화 - F28P65x는 전용 아날로그 핀이
    // 아니므로 반드시 설정해야 합니다(9_VariableVoltage2Ch_Driverlib.c와 동일한 이유로
    // 원본 코드에서 누락되어 있던 부분을 보완했습니다 - 없으면 ADC 결과가 고정값으로
    // 읽힐 수 있습니다).
    GPIO_setPinConfig(GPIO_227_GPIO227);
    GPIO_setAnalogMode(227, GPIO_ANALOG_ENABLED);

    // ADC-A Ch0 (가변저항) 초기화
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_enableConverter(ADCA_BASE);
    DEVICE_DELAY_US(1000); // 파워업 딜레이

    // SOC0 설정: 소프트웨어 트리거, ADCIN A0
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_SW_ONLY, ADC_CH_ADCIN0, 15);
}

static void initEPwm1(void) {
    // ePWM1A 초기화 (Duty 제어용)
    EPWM_setPeriodLoadMode(EPWM1_BASE, EPWM_PERIOD_SHADOW_LOAD);
    EPWM_setTimeBasePeriod(EPWM1_BASE, 2000); // 100kHz @ 200MHz TBCLK
    EPWM_setTimeBaseCounter(EPWM1_BASE, 0);
    EPWM_setTimeBaseCounterMode(EPWM1_BASE, EPWM_COUNTER_MODE_UP);
    EPWM_setClockPrescaler(EPWM1_BASE, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);

    // Action Qualifier: 0에서 SET, CMPA에서 CLEAR
    EPWM_setActionQualifierAction(EPWM1_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(EPWM1_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, 1000); // 50%
}

// -----------------------------------------------------------------------------
// Sensor Reading & LED Pattern Handling
// -----------------------------------------------------------------------------
static void readSensors(void) {
    uint8_t  tact = 0;
    uint8_t  toggle = 0;
    uint16_t cmpVal;
    uint16_t i;

    // 1. ADC-A0 읽기 (가변저항)
    ADC_forceSOC(ADCA_BASE, ADC_SOC_NUMBER0);
    while (ADC_isBusy(ADCA_BASE));
    g_telemetry.adcA0Raw = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
    g_telemetry.adcA0Voltage = ((float)g_telemetry.adcA0Raw / 4095.0f) * 3.3f;

    // 2. Tactile Switches
    for (i = 0; i < NUM_TACT_SW; i++) {
        if (GPIO_readPin(s_tactGpios[i]) == 1) {
            tact |= (1 << i);
        }
    }
    g_telemetry.tactSwitches = tact;

    // 3. Toggle Switches
    for (i = 0; i < NUM_TOGGLE_SW; i++) {
        if (GPIO_readPin(s_toggleGpios[i]) == 1) {
            toggle |= (1 << i);
        }
    }
    g_telemetry.toggleSwitches = toggle;

    // 4. PWM Duty 반영
    cmpVal = (uint16_t)((2000UL * g_sysConfig.pwmDutyPct) / 100UL);
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpVal);
}

static void updateLedOutputs(uint16_t mask) {
    uint16_t i;
    g_telemetry.currentLedState = mask;
    for (i = 0; i < NUM_LEDS; i++) {
        uint32_t val = (mask & (1 << i)) ? 1 : 0;
        GPIO_writePin(s_ledGpios[i], val);
    }
}

static void processLedPattern(void) {
    static uint16_t s_ledIdx = 0;
    static int16_t  s_ledDir = 1;
    static uint16_t s_counter = 0;
    static bool     s_toggleAll = false;

    switch (g_sysConfig.ledPattern) {
        case 1: { // 1. 순차 점등 (Sequential Shift)
            updateLedOutputs(1U << s_ledIdx);
            s_ledIdx = (s_ledIdx + 1) % NUM_LEDS;
            break;
        }

        case 2: { // 2. 왕복 점등 (Bounce / Ping-Pong)
            updateLedOutputs(1U << s_ledIdx);
            if (s_ledIdx == 0) s_ledDir = 1;
            else if (s_ledIdx == (NUM_LEDS - 1)) s_ledDir = -1;
            s_ledIdx += s_ledDir;
            break;
        }

        case 3: { // 3. 전체 깜빡임 (All Blink)
            s_toggleAll = !s_toggleAll;
            updateLedOutputs(s_toggleAll ? 0xFFFF : 0x0000);
            break;
        }

        case 4: { // 4. 이진 카운터 (Binary Counter)
            updateLedOutputs(s_counter++);
            break;
        }

        default:
            updateLedOutputs(0x5555);
            break;
    }
}
