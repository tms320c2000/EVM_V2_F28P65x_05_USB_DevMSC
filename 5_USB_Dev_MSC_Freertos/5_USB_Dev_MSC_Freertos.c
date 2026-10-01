// 파일이름	:	5_USB_Dev_MSC_Freertos.c
// 대상장치	:	TMS320F28X EVM V2, TMS320F28P65x(F28P650DK9 산업용 / F28P659DK8-Q1 차량용) 모듈
// 파일버전	:	1.00
// 갱신이력	:	2026-09-20, 버전 1.00
// 예제설명	:

//************************************************************************************************************************************************************************
//
// 본 예제는 TMS320F28P65x 모듈이 탑재된 TMS320F28X 개발보드(EVM) V2를 대상으로 하며,
// F28P65x의 On-Chip USB(USB0)를 디바이스(Device) 모드 대용량 저장장치(MSC)로 동작시켜,
// PC에 연결하면 가상 이동식 디스크(FAT12)로 인식되게 합니다. README.TXT(안내),
// STATUS.TXT(실시간 센서/스위치 모니터링), CONFIG.INI(메모장으로 LED 패턴/속도/PWM
// 듀티를 실시간 반영)를 제공합니다 - 기능은 5_USB_Dev_MSC_Driverlib.c와 100% 동일합니다.
//
// 본 예제는 FreeRTOS 커널 위에서 하나의 정적(Static, 힙 미사용) 태스크로 동작합니다:
//   UsbDevMscTask (우선순위 1): USBDevMSC_process()를 계속 호출해 USB 대용량 저장장치
//   상태 머신을 폴링하고, vTaskDelay(1ms)로 스케줄러에 양보하면서 10ms 센서/스위치
//   읽기, LED 패턴 갱신(설정된 속도), 1초 Uptime 카운트를 처리합니다. 이 프로젝트에는
//   다른 태스크가 없으므로 베어메탈 버전의 while(1) 루프와 사실상 동일하게 동작합니다
//   (busy-wait 대신 vTaskDelay로 CPU를 양보한다는 점만 다릅니다).
//
// a. 배선 안내:
//
//		>> USB: 개발보드 (5) USB 통신 회로(USB0, CN4600 계열 커넥터)를 PC와 USB 케이블로
//		   연결하세요. USB0DM(GPIO42)/USB0DP(GPIO43)는 칩 전용 USB 핀이라 별도 점퍼가
//		   필요 없습니다(VBUS 감지/ID도 마찬가지).
//		>> LED(권장): A Side 핀-헤더 65~95번(홀수, GPIO0~15)을 (8) 범용 LED 16개 블록에
//		   연결하면 CONFIG.INI 변경의 시각 피드백을 볼 수 있습니다.
//		>> 스위치(선택): 택트 1~4번(GPIO70,69,68,67), 토글 1~8번(GPIO25~18)을 연결하면
//		   STATUS.TXT에서 스위치 상태를 확인할 수 있습니다.
//
// 본 예제는 TI DriverLib API + FreeRTOS 정적 할당(configSUPPORT_DYNAMIC_ALLOCATION=0) +
// USB Device Library(usblib)로 제작되었습니다. 같은 동작을 베어메탈 DriverLib으로 구현한
// 5_USB_Dev_MSC_Driverlib.c, 비트필드로 구현한 5_USB_Dev_MSC_Bitfield.c와 함께 세 가지
// 방식을 비교해 보실 수 있습니다.
//
// b. USB 60MHz 클럭: F28P65x의 USB 컨트롤러는 SYSCLK가 아니라 별도의 AUXPLL 60MHz 클럭이
//    필요합니다. `SysCtl_setAuxClock(DEVICE_AUXSETCLOCK_CONFIG_USB)`(usb_hal.h)가 보드의
//    25MHz BAW 오실레이터 기준으로 AUXPLLCLK 120MHz -> /2 -> 60MHz를 만들어 줍니다.
//
//************************************************************************************************************************************************************************


// 헤더 파일들
#include "driverlib.h"		// TI 제공 Driver API Library 헤더파일 (driverlib)
#include "device.h"
#include "usb_hal.h"		// USBGPIOEnable(), DEVICE_AUXSETCLOCK_CONFIG_USB (TI c2000_specific 헬퍼)
#include "usb_dev_msc.h"
#include "virtual_disk.h"
#include "config_parser.h"
#include "status_generator.h"
#include "FreeRTOS.h"
#include "task.h"

// -----------------------------------------------------------------------------
// Pin Definitions on EVM V2 (DriverLib/비트필드 버전과 동일)
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

#define STACK_SIZE        2048U   // 태스크 스택 크기, 워드 단위 (RAMD0 넉넉히 활용)
#define IDLE_STACK_SIZE   1024U   // Idle 태스크 스택 크기, 워드 단위 (ISR 실행 대비)

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
void UsbDevMscTask(void *pvParameters);

// FreeRTOS 태스크용 정적 메모리 선언 (힙 미사용, 정적 할당)
static StaticTask_t usbTaskBuffer;
static StackType_t  usbTaskStack[STACK_SIZE];
#pragma DATA_SECTION(usbTaskStack, ".freertosStaticStack")
#pragma DATA_ALIGN(usbTaskStack, portBYTE_ALIGNMENT)

static StaticTask_t idleTaskBuffer;
static StackType_t  idleTaskStack[IDLE_STACK_SIZE];
#pragma DATA_SECTION(idleTaskStack, ".freertosStaticStack")
#pragma DATA_ALIGN(idleTaskStack, portBYTE_ALIGNMENT)

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------
void main(void) {
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

    // 5. USB 인터럽트 핸들러 등록 및 활성화 (USBDevMSC_init 이전에 등록 필수)
    Interrupt_register(INT_USBA, &USBDevMSC_intHandler);
    Interrupt_enable(INT_USBA);
    Interrupt_enableGlobal();
    ERTM;

    // 6. USB MSC 디바이스 스택 초기화 (디바이스 모드 설정 및 버스 연결)
    USBDevMSC_init();

    // 7. FreeRTOS 태스크 생성 및 스케줄러 시작
    //    - UsbDevMscTask (우선순위 1): USBDevMSC_process() 폴링 + 센서/LED/시간 관리
    xTaskCreateStatic(UsbDevMscTask,
                       "USB Dev MSC Task",
                       STACK_SIZE,
                       NULL,
                       tskIDLE_PRIORITY + 1,
                       usbTaskStack,
                       &usbTaskBuffer);

    vTaskStartScheduler();      // 이 아래로는 절대 돌아오지 않음

    for(;;)
    {
        // 여기 도달하면 스케줄러 시작 실패 (메모리 부족 등)
    }
}

//
// UsbDevMscTask - USBDevMSC_process()를 계속 호출해 USB 대용량 저장장치 상태 머신을
// 폴링하고, vTaskDelay(1ms)로 스케줄러에 양보하면서 베어메탈 버전과 동일한 1ms
// 타임베이스로 10ms 센서/스위치 갱신, LED 패턴 갱신, 1초 Uptime 카운트를 처리합니다.
// 이 프로젝트의 유일한 태스크이므로 베어메탈 버전의 while(1) 루프와 사실상 동일하게
// 동작합니다.
//
void UsbDevMscTask(void *pvParameters)
{
    uint32_t lastLedTick = 0;
    uint32_t lastSensorTick = 0;
    uint32_t lastSecTick = 0;
    uint32_t sysTicks = 0;
    (void)pvParameters;

    for(;;)
    {
        // USB 대용량 전송 및 상태 머신 폴링
        USBDevMSC_process();

        // 1ms 타임베이스 (FreeRTOS 틱, configTICK_RATE_HZ=1000)
        vTaskDelay(pdMS_TO_TICKS(1));
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
    // 아니므로 반드시 설정해야 합니다(없으면 ADC 결과가 고정값으로 읽힐 수 있습니다).
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

//
// vApplicationStackOverflowHook - FreeRTOS가 스택오버플로우를 감지하면 호출
//
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    uint16_t i;
    (void)xTask;
    (void)pcTaskName;
    for(;;) {
        for (i = 0; i < NUM_LEDS; i++) {
            GPIO_togglePin(s_ledGpios[i]);
        }
        DEVICE_DELAY_US(50000);
    }
}

//
// vApplicationGetIdleTaskMemory - configSUPPORT_STATIC_ALLOCATION=1 이면 Idle 태스크
// 메모리도 애플리케이션이 직접 제공해야 함(힙을 안 쓰므로)
//
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idleTaskBuffer;
    *ppxIdleTaskStackBuffer = idleTaskStack;
    *pulIdleTaskStackSize = IDLE_STACK_SIZE;
}

// 파일 끝.
//
// 참고: DriverLib ASSERT()/usblib.lib이 호출하는 __error__ 핸들러는 이 프로젝트가
// 복사해 온 device/device.c에 이미 정의되어 있어 별도로 정의하지 않습니다.
