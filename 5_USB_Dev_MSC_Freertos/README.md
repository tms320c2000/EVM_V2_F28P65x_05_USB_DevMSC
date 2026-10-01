---
title: "F28P65x USB MSC 가상 이동식 디스크 (Virtual Disk) — FreeRTOS 버전 — TMS320F28P659"
metadata:
  purpose: "5_USB_Dev_MSC_Driverlib와 동일한 가상 USB 드라이브 데모를 FreeRTOS 정적 태스크 위에서 구현"
---

# F28P65x USB MSC 가상 이동식 디스크 (Virtual Disk) — FreeRTOS 버전

[5_USB_Dev_MSC_Driverlib](../5_USB_Dev_MSC_Driverlib/README.md)(DriverLib 버전)와 동일한
동작을 FreeRTOS 커널 위에서 구현했습니다. USB 스택 위의 3개 파일(`README.TXT`/
`STATUS.TXT`/`CONFIG.INI`)과 그 동작 방식은 DriverLib 버전과 100% 동일하므로, 기능
설명은 [Driverlib 버전 README의 "가상 드라이브의 파일 3개"](../5_USB_Dev_MSC_Driverlib/README.md)를
참고하세요.

## 이 버전의 핵심
- 하나의 정적(Static, 힙 미사용) 태스크 **`UsbDevMscTask`(우선순위 1)**만 사용합니다:
  `USBDevMSC_process()`를 계속 호출해 USB 대용량 저장장치 상태 머신을 폴링하고,
  `vTaskDelay(pdMS_TO_TICKS(1))`로 스케줄러에 양보하면서 베어메탈 버전과 동일한 1ms
  타임베이스로 10ms 센서/스위치 갱신, 설정된 속도의 LED 패턴 갱신, 1초 Uptime 카운트를
  처리합니다.
- 이 프로젝트에는 다른 태스크가 없으므로 DriverLib 버전의 `while(1)` 루프와 사실상
  동일하게 동작합니다 — 차이는 `DEVICE_DELAY_US(1000)` busy-wait 대신
  `vTaskDelay(1)`로 CPU를 FreeRTOS Idle 태스크에 양보한다는 점뿐입니다
  (`configTICK_RATE_HZ=1000`이라 1 tick = 1ms로 정확히 대응합니다).
- 하드웨어 초기화(`initPeripherals`/`initAdcA`/`initEPwm1` 등)와 USB 핀/클럭 설정
  (`USBGPIOEnable()`, `SysCtl_setAuxClock(DEVICE_AUXSETCLOCK_CONFIG_USB)`)은 DriverLib
  API를 그대로 사용합니다(`5_USB_HostMouse_Freertos.c`와 동일한 패턴).
- FreeRTOS 커널은 정적 할당만 사용합니다(`configSUPPORT_DYNAMIC_ALLOCATION=0`) —
  태스크 스택, Idle 태스크 메모리 모두 `.freertosStaticStack` 섹션에 배치된 전역
  배열입니다.
- `usblib.lib`/`driverlib.lib`이 참조하는 `__error__()` ASSERT 핸들러는 DriverLib
  버전과 마찬가지로 이 프로젝트가 복사해 온 `device/device.c`가 이미 정의하고
  있어 별도로 정의하지 않습니다.

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
`9_VariableVoltage2Ch_Freertos`/`5_USB_HostMouse_Freertos`와 동일한 FreeRTOS 커널/포트
(`FreeRTOS/`, `device/device.c`) 구성을 재사용했습니다. USB Device Library 헤더 전체
(`usb/include/`)와 `usblib.lib` + `driverlib.lib` + `device/driverlib/usb.c`(usblib이
내부적으로 호출하는 저수준 USB HAL, 사전 빌드된 `driverlib.lib`에는 빠져 있어 소스를
직접 컴파일)를 포함해 자기완결형으로 만들었습니다. 컴파일러 정의 `--define=ccs_c2k`
(usblib.h가 TI CCS/C2000 컴파일러임을 식별) + `--c99`(가변 선언용 for문 문법) 필수 —
`.projectspec`에 이미 반영되어 있습니다.

> **링커 메모리 배치**: 기본 FreeRTOS 템플릿(`.stack`은 RAMM1 1KB, `.const`/`.data`는
> RAMLS5 2KB)이 USB 디스크립터·가변 문자열 데이터량에 비해 작아서, DriverLib/비트필드
> 버전과 동일하게 `.stack`은 RAMLS7로, `.data`는 RAMGS0으로, `.const`는(RAM 빌드에서만)
> RAMLS6/RAMGS3/RAMGS4로 분산 배치했습니다. 추가로 이 예제는 코드 용량이 커서 `.text`가
> 기본 템플릿의 `RAMD0|RAMD1`(FreeRTOS 정적 스택 전용 영역)까지 침범해 `.freertosStaticStack`
> 배치가 실패했으므로, RAM 빌드에서 `.text`를 `RAMLS0~3 | RAMLS8 | RAMLS9`로만 배치하도록
> 수정해 `RAMD0`/`RAMD1`을 FreeRTOS 정적 스택/힙 전용으로 비워뒀습니다(FLASH 빌드는
> `.text`가 원래 FLASH_BANK0/1로 가므로 이 문제가 없습니다).

## Import → Build → Flash → Run
1. CCS에서 `CCS/5_USB_Dev_MSC_Freertos.projectspec`를 Import
2. Build (CPU1_RAM 또는 CPU1_FLASH)
3. Debug 연결 후 Flash/Run — `.ccxml`은 XDS2xx USB 디버그 프로브 설정을 사용합니다
4. 보드 USB 포트를 PC에 연결하면 Windows 탐색기에 `F28P659 (D:)` 드라이브가 자동
   생성됩니다 — `README.TXT`, `STATUS.TXT`, `CONFIG.INI` 파일 열람 및 수정 실습 진행!

## 메모리 사용량 (CPU1_RAM 빌드 실측, 2026-10-01)

`C:/Users/vosam/workspace_ccstheia/5_USB_Dev_MSC_*/CPU1_RAM/*.map`을 실제로 빌드해 링커
맵의 온칩 RAM 사용량을 합산한 값입니다(플래시가 아닌 RAM 로드 구성 기준, 링커 cmd가
매핑하는 레지스터 윈도우는 제외). 링커 옵션의 스택 2KB(`--stack_size=0x800`)가 포함됩니다.

| | 비트필드 | DriverLib | **FreeRTOS(이 폴더)** |
|---|---|---|---|
| 합계 | 18,701 B (18.3 KB) | 26,169 B (25.6 KB) | **31,759 B (31.0 KB)** |

비트필드 버전이 가장 작고, FreeRTOS 버전은 커널 코드/데이터가 더해져 가장 큽니다.

## 세 가지 버전 비교
같은 USB MSC 가상 이동식 디스크 통합 데모를 세 가지 방식으로 구현해 비교합니다.

| 버전 | 폴더 | 핵심 차이 |
|---|---|---|
| DriverLib | [5_USB_Dev_MSC_Driverlib](../5_USB_Dev_MSC_Driverlib/) | TI 표준 HAL 함수 호출 |
| 비트필드 | [5_USB_Dev_MSC_Bitfield](../5_USB_Dev_MSC_Bitfield/) | 애플리케이션 코드는 레지스터 구조체 직접 조작(usblib.lib + 공유 USB 엔진의 숨은 driverlib 의존성만 링크) |
| **FreeRTOS(이 폴더)** | 5_USB_Dev_MSC_Freertos | DriverLib + 태스크 스케줄링, 정적 할당 |

## 관련 링크
- 상품 페이지: https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903127
- 게시판 글: https://tms320f28x.co.kr/board/view.php?bdId=tms320f28xevmv2&sno=111
- 유튜브 영상: (게시 후 URL 추가 예정)
