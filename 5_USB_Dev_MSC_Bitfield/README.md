---
title: "F28P65x USB MSC 가상 이동식 디스크 (Virtual Disk) — 비트필드(레지스터 직접 제어) 버전 — TMS320F28P659"
metadata:
  purpose: "5_USB_Dev_MSC_Driverlib와 동일한 가상 USB 드라이브 데모를 애플리케이션 코드는 DriverLib을 호출하지 않고 클래식 레지스터 구조체만으로 구현"
---

# F28P65x USB MSC 가상 이동식 디스크 (Virtual Disk) — 비트필드 버전

[5_USB_Dev_MSC_Driverlib](../5_USB_Dev_MSC_Driverlib/README.md)(DriverLib 버전)와 동일한
동작을 애플리케이션 코드는 **DriverLib을 전혀 호출하지 않고** TI의 클래식 레지스터
비트필드 구조체(`GpioCtrlRegs`, `DevCfgRegs`, `CpuSysRegs`, `ClkCfgRegs`, `AdcaRegs`,
`EPwm1Regs`)만으로 구현했습니다. USB 스택 위의 3개 파일(`README.TXT`/`STATUS.TXT`/
`CONFIG.INI`)과 그 동작 방식은 DriverLib 버전과 100% 동일하므로, 기능 설명은
[Driverlib 버전 README의 "가상 드라이브의 파일 3개"](../5_USB_Dev_MSC_Driverlib/README.md)를
참고하세요.

## 이 버전의 핵심 — 그리고 "완전히 DriverLib-free"는 아닌 이유
- 시스템 초기화: `InitSysCtrl()`/`InitGpio()`/`InitPieCtrl()`/`InitPieVectTable()`
  (DriverLib의 `Device_init()` 등에 대응하는 클래식 헬퍼)
- USB 핀 활성화: `USBGPIOEnable()`이 `GpioCtrlRegs.GPBAMSEL.bit.GPIO42/43`(아날로그 모드),
  `GPBDIR.bit.GPIO46/47`(입력)을 직접 설정 — DriverLib 버전의 `USBGPIOEnable()`(usb_hal.c)과
  동일한 레지스터 값을 만듭니다.
- USB 주변장치 리셋/클럭 인에이블: `DevCfgRegs.SOFTPRES11.bit.USB_A`를 1→0으로 펄스
  (DriverLib의 `SysCtl_resetPeripheral(SYSCTL_PERIPH_RES_USBA)`에 대응),
  `CpuSysRegs.PCLKCR11.bit.USB_A = 1`(이미 `InitSysCtrl()`에서 켜져 있어 중복이지만 명시).
- USB 60MHz 클럭: `initUsbAuxClock()`이 `ClkCfgRegs.AUXPLLCTL1/AUXPLLMULT/AUXPLLSTS`를
  직접 조작해 DriverLib의 `SysCtl_setAuxClock(DEVICE_AUXSETCLOCK_CONFIG_USB)`와 동일한
  120MHz(→60MHz) AUXPLLCLK를 만듭니다(DCC 주파수 검증/재시도 로직은 생략한 축약판) —
  `5_USB_HostMouse_Bitfield.c`와 동일한 함수입니다.
- 인터럽트 등록: `PieVectTable.USBA_INT = &USBDevMSC_intHandler`
  (DriverLib의 `Interrupt_register(INT_USBA, ...)`에 대응).
- LED/스위치/ADC/PWM: `GpioDataRegs.GPxSET/GPxCLEAR/GPxDAT`, `AdcaRegs`, `EPwm1Regs`를
  직접 조작. `initAdcA()`에서 `GpioCtrlRegs.GPHMUX1/GPHAMSEL.bit.GPIO227`,
  `AnalogSubsysRegs.AGPIOCTRLH.bit.GPIO227`로 A0(가변저항) 아날로그 모드를 활성화하는
  부분은 DriverLib 버전에서 발견된 "빠지면 ADC가 고정값만 읽는" 버그의 수정을 그대로
  반영했습니다.
- **다만 USB 프로토콜 스택 자체(`usblib.lib`)는 TI가 미리 빌드해 배포하는 라이브러리라
  손을 댈 수 없고, 이 라이브러리가 내부적으로 DriverLib의 인터럽트/시스템클럭/USB
  레지스터 저수준 함수(`Interrupt_enable`, `SysCtl_delay`, `USBHostMode` 등)를 호출하도록
  이미 빌드되어 있습니다.** 게다가 `usb_dev_msc.c`(모든 버전이 공유하는 USB BOT/SCSI
  엔진 파일)는 `USBEndpointDataGet/Put`, `USB_EP_1`, `USB_TRANS_IN`, `USBA_BASE` 같은
  DriverLib 저수준 USB HAL(`device/driverlib/usb.c`/`usb.h`) 함수를 직접 호출합니다 —
  이 파일은 사전 빌드된 `driverlib.lib`에도 포함되어 있지 않아 이 프로젝트에도 소스를
  통째로 복사해 함께 컴파일합니다. 그래서 이 프로젝트도 `driverlib.lib`을 링크에
  포함하고 `device/driverlib/usb.c`를 별도 컴파일하지만, **우리가 새로 작성한
  애플리케이션 코드(`5_USB_Dev_MSC_Bitfield.c`)는 driverlib 함수를 단 한 줄도 직접
  호출하지 않습니다** — usblib.lib 자체와 공유 USB 엔진 파일의 숨은 의존성을 채워주는
  용도일 뿐입니다.
- `usblib.lib`/`driverlib.lib`이 참조하는 ASSERT 실패 핸들러 `__error__()`는 DriverLib
  버전에서는 `device/device.c`가 대신 정의해 주지만, 이 비트필드 버전은 `device.c`를
  쓰지 않으므로 `5_USB_Dev_MSC_Bitfield.c`에 직접 정의했습니다(무한루프로 멈춰
  디버거로 원인 추적).

## 요구 하드웨어
- SyncWorks TMS320F28X 개발보드 V2
- TMS320F28P650DK9 또는 TMS320F28P659DK8-Q1 모듈
- USB 케이블(PC ↔ 보드), 점퍼 케이블(LED16/스위치 블록용, 선택)

## 배선

(5) USB 통신 회로는 F2837x/F2838x용 ControlCARD 구성이라, F28P65x 모듈에서는 A Side
핀-헤더에서 (5)번 블록의 USB 인터페이스 핀-헤더까지 점퍼 케이블로 직접 연결해야 합니다.

| 신호 | 연결 |
|---|---|
| USB D- | **A Side 핀-헤더 15번**(GPIO42, USB0DM) → (5) 블록 "D-" 핀 |
| USB D+ | **A Side 핀-헤더 17번**(GPIO43, USB0DP) → (5) 블록 "D+" 핀 |
| USB VBUS | **A Side 핀-헤더 19번**(GPIO46) → (5) 블록 "VBUS" 핀 (PC USB 5V 전원 감지) |
| USB ID | **A Side 핀-헤더 21번**(GPIO47) → (5) 블록 "ID" 핀 |
| LED | 개발보드 **A Side 핀-헤더 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91, 93, 95번**(홀수, GPIO0~GPIO15에 순서대로 1:1 대응)을 **(8) 범용 LED 16개** 블록 입력 16핀에 순서대로 연결 (권장) |
| 택트 스위치 1~4 | **A Side 핀-헤더 29, 27, 25, 23번**(GPIO70, 69, 68, 67) → (6) 블록 SW1~SW4 (선택) |
| 토글 스위치 1~8 | **A Side 핀-헤더 115, 113, 111, 109, 107, 105, 103, 101번**(GPIO25~GPIO18) → (7) 블록 1~8번 (선택) |
| 가변저항 | **B Side 핀-헤더 111번**(ADCINA0) → (9) 가변전압 블록 출력 (선택) |

- PC 케이블은 반드시 개발보드 **BOTTOM면의 Mini-B 5핀 USB 커넥터**에 연결하세요. TOP면의
  Type-A 커넥터는 마우스/메모리를 꽂는 호스트 전용입니다.
- BOTTOM면의 **USB ID 풀업 스위치**는 디바이스 모드이므로 High 쪽으로 설정하세요.
- 디바이스 모드에서는 PC가 VBUS 전원을 공급하므로 USB 호스트 예제의 EPEN/PFLT 배선은
  필요하지 않습니다.

![TMS320F28X EVM V2 — USB 디바이스(MSC) 예제 결선](f28xevm_v2_usb_dev_msc.png)

위 사진은 USB 신호 4개와 LED16 블록의 결선, 그리고 BOTTOM면의 USB ID 풀업 스위치(High)와
Mini-B 연결 위치를 보여줍니다. 스위치/가변저항 배선은 선택 사항이라 사진에 없습니다.

## 소프트웨어 버전
CCS 21.x / **SysConfig 미사용** (products="C2000WARE"만 사용) / CGT 22.6.3.LTS.
`device/common_include`, `device/headers_include`에 필요한 클래식 헤더를 전부 복사했고,
USB Host Library 헤더 전체(`usb/include/`)와 `usblib.lib`, 그리고 usblib과 공유 USB
엔진의 숨은 의존성을 위한 `driverlib.lib` + `device/driverlib/usb.c`를 함께 포함해
자기완결형으로 만들었습니다. 컴파일러 정의 `--define=ccs_c2k`(usblib.h가 TI CCS/C2000
컴파일러임을 식별) + `--c99`(가변 선언용 for문 문법) 필수 — `.projectspec`에 이미
반영되어 있습니다.

> **C28x uint8_t 주의사항**: C28x는 하드웨어에 8비트 타입이 없어 TI `stdint.h`가 이
> 아키텍처에서는 `uint8_t`/`int8_t`를 정의하지 않습니다. `usb_dev_msc.c`/`virtual_disk.h`/
> `status_generator.h`가 `uint8_t`를 직접 사용하므로, 여러 독립 컴파일 단위가 공유하는
> `c28x_uint8_compat.h`(`typedef uint16_t uint8_t;`) shim을 만들어 포함했습니다.

> **링커 메모리 배치**: USB 디스크립터·가변 문자열 데이터량이 기본 템플릿(`.stack`은
> RAMM1 1KB, `.const`/`.data`는 RAMLS5 2KB)보다 커서, `28p65x_generic_ram_lnk_cpu1.cmd`/
> `_flash_lnk_cpu1.cmd`에서 `.stack`은 RAMLS7로, `.data`는 RAMGS0으로, `.const`는
> RAMLS6/RAMGS3/RAMGS4로 분산 배치하도록 수정했습니다(DriverLib 버전과 동일한 수정).

## Import → Build → Flash → Run
1. CCS에서 `CCS/5_USB_Dev_MSC_Bitfield.projectspec`를 Import
2. Build (CPU1_RAM 또는 CPU1_FLASH)
3. Debug 연결 후 Flash/Run — `.ccxml`은 XDS2xx USB 디버그 프로브 설정을 사용합니다
4. 보드 USB 포트를 PC에 연결하면 Windows 탐색기에 `F28P659 (D:)` 드라이브가 자동
   생성됩니다 — `README.TXT`, `STATUS.TXT`, `CONFIG.INI` 파일 열람 및 수정 실습 진행!

## 빌드 검증 메모 (2026-09-20)
DriverLib 버전에서 이미 찾아 고친 실제 버그(매크로 오탈자, `USB0_BASE`→`USBA_BASE`,
`USBEndpointDataGet()` 포인터/정수 혼동, GCC 전용 `__attribute__((packed))`, 링커 메모리
배치)는 공유 파일(`usb_dev_msc.c`, `virtual_disk.c` 등)을 통해 이 버전에도 동일하게
반영되어 있습니다(자세한 내용은 [Driverlib 버전 README의 "5. 빌드 검증 메모"](../5_USB_Dev_MSC_Driverlib/README.md#5-빌드-검증-메모-2026-09-20) 참고).

이 버전을 만들며 추가로 발견/처리한 사항:
- `SOFTPRES11` 레지스터는 `CpuSysRegs`가 아니라 `DevCfgRegs` 구조체에 속함(첫 시도에서
  `error: struct "CPU1_SYS_REGS" has no field "SOFTPRES11"`로 확인).
- `usb_dev_msc.c`가 쓰는 `USBA_BASE`, `USB_EP_x`, `USB_TRANS_x`, `USB_INTEP_x` 등은
  `usblib.h`(프로토콜 계층)가 아니라 DriverLib 저수준 `usb.h`/`inc/hw_memmap.h`에서
  옵니다 — `usb.h` 자체는 `#include` 문이 하나도 없어 포함하는 쪽이 미리
  `inc/hw_memmap.h`를 포함해 둬야 `USBA_BASE`가 풀립니다.
- 수동 `cl2000 -z` 링크 시 `--define=__TI_EABI__`를 **cmd 파일보다 앞쪽 인자로** 전달해야
  `.cmd` 파일의 `#if defined(__TI_EABI__)` 분기가 올바르게 적용됩니다(cmd 파일 뒤에
  넣으면 이미 전처리가 끝난 뒤라 무시되어, 엉뚱하게도 `.const`가 좁은 영역 하나로
  몰려 링크 실패로 이어졌습니다).

## 메모리 사용량 (CPU1_RAM 빌드 실측, 2026-10-01)

`C:/Users/vosam/workspace_ccstheia/5_USB_Dev_MSC_*/CPU1_RAM/*.map`을 실제로 빌드해 링커
맵의 온칩 RAM 사용량을 합산한 값입니다(플래시가 아닌 RAM 로드 구성 기준, 링커 cmd가
매핑하는 레지스터 윈도우는 제외). 링커 옵션의 스택 2KB(`--stack_size=0x800`)가 포함됩니다.

| | **비트필드(이 폴더)** | DriverLib | FreeRTOS |
|---|---|---|---|
| 합계 | **18,701 B (18.3 KB)** | 26,169 B (25.6 KB) | 31,759 B (31.0 KB) |

비트필드 버전이 가장 작고, FreeRTOS 버전은 커널 코드/데이터가 더해져 가장 큽니다.

## 세 가지 버전 비교
같은 USB MSC 가상 이동식 디스크 통합 데모를 세 가지 방식으로 구현해 비교합니다.

| 버전 | 폴더 | 핵심 차이 |
|---|---|---|
| DriverLib | [5_USB_Dev_MSC_Driverlib](../5_USB_Dev_MSC_Driverlib/) | TI 표준 HAL 함수 호출 |
| **비트필드(이 폴더)** | 5_USB_Dev_MSC_Bitfield | 애플리케이션 코드는 레지스터 구조체 직접 조작(usblib.lib + 공유 USB 엔진의 숨은 driverlib 의존성만 링크) |
| FreeRTOS | [5_USB_Dev_MSC_Freertos](../5_USB_Dev_MSC_Freertos/) | DriverLib + 태스크 스케줄링, 정적 할당 |

## 관련 링크
- 상품 페이지: https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903127
- 게시판 글: https://tms320f28x.co.kr/board/view.php?bdId=tms320f28xevmv2&sno=111
- 유튜브 영상: (게시 후 URL 추가 예정)
