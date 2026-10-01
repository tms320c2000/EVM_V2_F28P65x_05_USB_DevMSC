// ==============================================================================
// 파일이름 : 5_USB_Dev_MSC_Bitfield.c
// 대상장치 : TMS320F28X EVM V2, TMS320F28P65x(F28P650DK9 / F28P659DK8-Q1) 모듈
// 파일버전 : 1.00
// 설명     : 회로블록 (5) USB 통신 회로 — USB MSC 가상 이동식 디스크 (Virtual Disk)
//            F28P659를 PC에 연결하면 이동식 디스크(FAT12)로 인식되어,
//            README.TXT(안내), STATUS.TXT(실시간 센서/스위치 모니터링),
//            CONFIG.INI(메모장으로 파라미터 수정 시 LED/PWM 실시간 반영)를 제공합니다.
//
//            본 예제는 28X 칩 MMR을 직접 조작하는 Bit-Field Approach로 GPIO/클럭/
//            인터럽트/ADC/EPWM을 초기화합니다(애플리케이션 코드는 DriverLib을 호출하지
//            않습니다). USB 프로토콜 스택 자체(usblib.lib)는 DriverLib 버전과 동일한
//            usb_dev_msc.c/virtual_disk.c/config_parser.c/status_generator.c를 그대로
//            재사용합니다 - usblib.lib이 내부적으로 DriverLib 저수준 함수를 호출하도록
//            빌드되어 있어 driverlib.lib도 함께 링크합니다(5_USB_HostMouse_Bitfield.c와
//            동일한 이유).
// ==============================================================================

#include "f28x_project.h"		// TI 제공 칩-지원 헤더 통합 Include 용 헤더파일 (bit-field)
#include "c28x_uint8_compat.h"	// usb_dev_msc.h/virtual_disk.h의 uint8_t 사용을 위한 shim
#include "usb_dev_msc.h"
#include "virtual_disk.h"
#include "config_parser.h"
#include "status_generator.h"

// -----------------------------------------------------------------------------
// Pin Definitions on EVM V2 (DriverLib 버전과 동일)
// -----------------------------------------------------------------------------
#define NUM_LEDS            16
static const Uint16 s_ledGpios[NUM_LEDS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

#define NUM_TACT_SW         4
static const Uint16 s_tactGpios[NUM_TACT_SW] = {
    70, 69, 68, 67  // SW1..SW4 (점퍼선 꼬임 방지 역순 배치)
};

#define NUM_TOGGLE_SW       8
static const Uint16 s_toggleGpios[NUM_TOGGLE_SW] = {
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
static void initUsbAuxClock(void);
static void updateLedOutputs(uint16_t mask);
static void processLedPattern(void);
static void readSensors(void);

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------
void main(void) {
    // 루프 타이머 변수
    Uint32 lastLedTick = 0;
    Uint32 lastSensorTick = 0;
    Uint32 lastSecTick = 0;
    Uint32 sysTicks = 0;

    // 0. 전역 인터럽트 스위치 OFF, CPU 인터럽트 벡터 비-활성화 및 플래그 클리어
    DINT;
    IER = 0x0000;
    IFR = 0x0000;

    // 1. 시스템 클럭(200MHz) 및 GPIO 초기화 - InitSysCtrl()/InitGpio()
    InitSysCtrl();
    InitGpio();

    // 2. 인터럽트 확장회로 초기화
    InitPieCtrl();
    InitPieVectTable();

    // 3. 인터럽트 벡터 - USBA_INT 연결
    EALLOW;
    PieVectTable.USBA_INT = &USBDevMSC_intHandler;
    EDIS;

    // 4. USB용 60MHz AUXPLL 클럭 설정 (25MHz BAW 오실레이터 기준)
    initUsbAuxClock();

    // 5. 하드웨어 주변장치 초기화 (LED, 스위치, ADC, PWM)
    initPeripherals();

    // 6. USB 인터럽트 활성화
    PieCtrlRegs.PIEIER9.bit.INTx15 = 1;	// PIE 그룹9.15(USBA_INT) 활성화
    IER |= M_INT9;						// CPU 인터럽트 9번 활성화
    EINT;								// 전역 인터럽트 스위치 ON
    ERTM;

    // 7. USB MSC 디바이스 스택 초기화 (usb_dev_msc.c, 세 버전 공용)
    USBDevMSC_init();

    for(;;) {
        // USB 대용량 전송 및 상태 머신 폴링
        USBDevMSC_process();

        // 1ms 타임베이스 (소프트웨어 지연 기반)
        DELAY_US(1000);
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

//
// initUsbAuxClock - AUXPLL을 이용해 USB 컨트롤러용 60MHz 클럭을 만듭니다
// (5_USB_HostMouse_Bitfield.c의 initUsbAuxClock()과 동일한 로직입니다).
//
static void initUsbAuxClock(void)
{
    EALLOW;

    ClkCfgRegs.AUXPLLCTL1.bit.PLLCLKEN = 0;
    DELAY_US(10);

    ClkCfgRegs.AUXPLLCTL1.bit.PLLEN = 0;
    DELAY_US(10);

    ClkCfgRegs.CLKSRCCTL2.bit.AUXOSCCLKSRCSEL = 1;		// 1 = XTAL (25MHz BAW 오실레이터)
    DELAY_US(10);

    ClkCfgRegs.AUXPLLMULT.bit.IMULT = 48;
    ClkCfgRegs.AUXPLLMULT.bit.REFDIV = 1;		// 필드값 = 분주값(2) - 1
    ClkCfgRegs.AUXPLLMULT.bit.ODIV = 4;		// 필드값 = 분주값(5) - 1

    ClkCfgRegs.AUXPLLCTL1.bit.PLLEN = 1;
    while(ClkCfgRegs.AUXPLLSTS.bit.LOCKS != 1)
    {
    }

    ClkCfgRegs.AUXPLLCTL1.bit.PLLCLKEN = 1;
    DELAY_US(5);

    EDIS;
}

static void initLeds(void) {
    Uint16 i;
    for (i = 0; i < NUM_LEDS; i++) {
        GPIO_SetupPinMux(s_ledGpios[i], GPIO_MUX_CPU1, 0);		// Mux=0: GPIO 기능
        GPIO_SetupPinOptions(s_ledGpios[i], GPIO_OUTPUT, GPIO_PUSHPULL);
        GpioDataRegs.GPACLEAR.all = (1UL << s_ledGpios[i]);	// 초기값 0
    }
}

static void initSwitches(void) {
    Uint16 i;

    // Tactile Switches (Active High, 풀업 없음 - DriverLib 버전과 동일하게 STD 패드)
    for (i = 0; i < NUM_TACT_SW; i++) {
        GPIO_SetupPinMux(s_tactGpios[i], GPIO_MUX_CPU1, 0);
        GPIO_SetupPinOptions(s_tactGpios[i], GPIO_INPUT, 0);
    }

    // Toggle Switches (Active High, 풀업 없음)
    for (i = 0; i < NUM_TOGGLE_SW; i++) {
        GPIO_SetupPinMux(s_toggleGpios[i], GPIO_MUX_CPU1, 0);
        GPIO_SetupPinOptions(s_toggleGpios[i], GPIO_INPUT, 0);
    }
}

static void initAdcA(void) {
    EALLOW;

    // A0(AIO227, B Side 111번) 아날로그 모드 활성화 - F28P65x는 전용 아날로그 핀이
    // 아니므로 반드시 설정해야 합니다(DriverLib 버전과 동일한 이유로 원본 코드에서
    // 누락되어 있던 부분을 보완했습니다 - 없으면 ADC 결과가 고정값으로 읽힐 수 있습니다).
    GpioCtrlRegs.GPHMUX1.bit.GPIO227 = 0;			// GPIO227 -> GPIO 기능(Mux=0)
    GpioCtrlRegs.GPHAMSEL.bit.GPIO227 = 1;			// GPIO227 아날로그 모드 활성화
    AnalogSubsysRegs.AGPIOCTRLH.bit.GPIO227 = 1;	// 아날로그 서브시스템 스위치 연결

    // ADC-A Ch0 (가변저항) 초기화
    AdcaRegs.ADCCTL2.bit.PRESCALE = 6;		// ADCCLK = SYSCLK/4 (50MHz)
    AdcSetMode(ADC_ADCA, ADC_RESOLUTION_12BIT, ADC_SIGNALMODE_SINGLE);	// OTP trim 로드 포함

    AdcaRegs.ADCCTL1.bit.ADCPWDNZ = 1;		// ADC 아날로그 회로 파워-업
    DELAY_US(1000);							// 파워업 딜레이

    // SOC0 설정: 소프트웨어 트리거, ADCIN A0
    AdcaRegs.ADCSOC0CTL.bit.CHSEL = 0;
    AdcaRegs.ADCSOC0CTL.bit.ACQPS = 15;
    AdcaRegs.ADCSOC0CTL.bit.TRIGSEL = 0;	// 0 = 소프트웨어 강제 트리거 전용(SW Only)

    EDIS;
}

static void initEPwm1(void) {
    EALLOW;

    // ePWM1A 초기화 (Duty 제어용)
    EPwm1Regs.TBCTL.bit.PRDLD = 1;			// TBPRD 쉐도우 로드
    EPwm1Regs.TBPRD = 2000;				// 100kHz @ 200MHz TBCLK
    EPwm1Regs.TBCTR = 0;
    EPwm1Regs.TBCTL.bit.CTRMODE = 0;		// Up-count 모드
    EPwm1Regs.TBCTL.bit.CLKDIV = 0;		// TBCLK = EPWMCLK / 1
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0;

    // Action Qualifier: 0에서 SET, CMPA에서 CLEAR
    EPwm1Regs.AQCTLA.bit.ZRO = 2;
    EPwm1Regs.AQCTLA.bit.CAU = 1;

    EPwm1Regs.CMPA.bit.CMPA = 1000;	// 50%

    EDIS;
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
    AdcaRegs.ADCSOCFRC1.bit.SOC0 = 1;
    while (AdcaRegs.ADCCTL1.bit.ADCBSY);
    g_telemetry.adcA0Raw = AdcaResultRegs.ADCRESULT0;
    g_telemetry.adcA0Voltage = ((float)g_telemetry.adcA0Raw / 4095.0f) * 3.3f;

    // 2. Tactile Switches
    for (i = 0; i < NUM_TACT_SW; i++) {
        if (((GpioDataRegs.GPCDAT.all >> (s_tactGpios[i] - 64U)) & 1UL) == 1UL) {
            tact |= (1 << i);
        }
    }
    g_telemetry.tactSwitches = tact;

    // 3. Toggle Switches
    for (i = 0; i < NUM_TOGGLE_SW; i++) {
        if (((GpioDataRegs.GPADAT.all >> s_toggleGpios[i]) & 1UL) == 1UL) {
            toggle |= (1 << i);
        }
    }
    g_telemetry.toggleSwitches = toggle;

    // 4. PWM Duty 반영
    cmpVal = (uint16_t)((2000UL * g_sysConfig.pwmDutyPct) / 100UL);
    EPwm1Regs.CMPA.bit.CMPA = cmpVal;
}

static void updateLedOutputs(uint16_t mask) {
    Uint16 i;
    g_telemetry.currentLedState = mask;
    for (i = 0; i < NUM_LEDS; i++) {
        if (mask & (1U << i)) {
            GpioDataRegs.GPASET.all = (1UL << s_ledGpios[i]);
        } else {
            GpioDataRegs.GPACLEAR.all = (1UL << s_ledGpios[i]);
        }
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
// __error__ - usblib.lib(그리고 usblib이 내부적으로 의존하는 DriverLib usb.c/sysctl.c
// 저수준 모듈)이 참조하는 ASSERT 실패 핸들러입니다. TI 헤더 주석에 "application's
// responsibility to define the __error__ function"이라고 명시되어 있어 직접 정의합니다.
// 실제 하드웨어에서는 정상 경로에서 호출되지 않아야 하며, 호출되면 무한루프로 멈춰
// 디버거로 원인을 추적할 수 있게 합니다.
//
void __error__(const char *pcFilename, uint32_t ui32Line)
{
    (void)pcFilename;
    (void)ui32Line;
    for(;;)
    {
    }
}

// -----------------------------------------------------------------------------
// USBGPIOEnable - USB0DM(GPIO42)/USB0DP(GPIO43)를 아날로그 모드로, VBUS 감지(GPIO46)/
// ID(GPIO47)를 입력으로, VBUS 전원 스위치(GPIO120/121)를 설정합니다.
// -----------------------------------------------------------------------------
void USBGPIOEnable(void)
{
    EALLOW;

    GpioCtrlRegs.GPBAMSEL.bit.GPIO42 = 1;	// USB0DM 아날로그 모드
    GpioCtrlRegs.GPBAMSEL.bit.GPIO43 = 1;	// USB0DP 아날로그 모드

    GpioCtrlRegs.GPBDIR.bit.GPIO46 = 0;		// VBUS 감지 - 입력
    GpioCtrlRegs.GPBDIR.bit.GPIO47 = 0;		// ID - 입력

    GpioCtrlRegs.GPDDIR.bit.GPIO120 = 0;	// 입력
    GpioCtrlRegs.GPDDIR.all |= (1UL << 25);	// GPIO121 출력 (bit 25 = 121 - 96)
    GpioDataRegs.GPDSET.all = (1UL << 25);	// GPIO121 High (VBUS 전원 활성화)

    EDIS;
}
