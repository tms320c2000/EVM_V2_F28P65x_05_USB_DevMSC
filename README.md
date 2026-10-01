# TMS320F28X 개발보드 V2 — F28P65x USB 디바이스(MSC) 가상 이동식 디스크 예제

[TMS320F28X 범용 개발보드 V2](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903127)의
회로블록 **(5) USB 통신 회로**를 F28P65x 모듈의 On-Chip USB(USB0) **디바이스(Device) 모드의
대용량 저장장치(MSC)**로 구동하는 예제입니다. 외장 SD카드나 플래시 없이 칩 내부 RAM에 64KB
가상 FAT12 파일시스템을 만들어 두고, PC에 USB 케이블을 꽂으면 `F28P659` 이동식 디스크가
나타납니다. 같은 동작을 세 가지 방식으로 각각 구현해서 코드량·메모리 사용량을 비교할 수
있게 만들었습니다.

가상 드라이브에는 파일 3개가 있습니다.
- `README.TXT`: 보드 개요와 사용법
- `STATUS.TXT`: 가변저항 전압, 택트/토글 스위치, Uptime, LED 상태를 읽을 때마다 새로 생성
- `CONFIG.INI`: 메모장으로 `LED_PATTERN`/`LED_SPEED_MS`/`PWM_DUTY_PCT`를 고쳐 저장하면 보드에 즉시 반영

## 포함된 프로젝트

| 프로젝트 | 설명 |
|---|---|
| [5_USB_Dev_MSC_Bitfield](5_USB_Dev_MSC_Bitfield/) | 레지스터 비트필드 직접 제어(USB 스택 `usblib` API 자체는 동일) |
| [5_USB_Dev_MSC_Driverlib](5_USB_Dev_MSC_Driverlib/) | TI DriverLib 사용 |
| [5_USB_Dev_MSC_Freertos](5_USB_Dev_MSC_Freertos/) | FreeRTOS 태스크로 구현(정적 할당, 힙 미사용) |

각 폴더는 자기완결형(self-contained) CCS 프로젝트입니다 — 폴더 하나만 받아도 C2000Ware/
DriverLib/USB Library(usblib) 등 필요한 파일이 전부 로컬에 포함되어 있어 Import → Build가
됩니다.

## 배선

USB 신호(D-/D+/VBUS/ID)와 LED16 블록을 A Side 핀-헤더로 점퍼 연결합니다. 스위치와
가변저항 블록은 선택 사항입니다.

| 신호 | 연결 |
|---|---|
| USB D- | A Side 핀-헤더 15번(GPIO42, USB0DM) → (5) 블록 "D-" |
| USB D+ | A Side 핀-헤더 17번(GPIO43, USB0DP) → (5) 블록 "D+" |
| USB VBUS | A Side 핀-헤더 19번(GPIO46) → (5) 블록 "VBUS" |
| USB ID | A Side 핀-헤더 21번(GPIO47) → (5) 블록 "ID" |
| LED | A Side 핀-헤더 65~95번(홀수, GPIO0~15에 순서대로 1:1 대응) → (8) 범용 LED 16개 블록 |
| 택트 스위치 1~4 (선택) | A Side 핀-헤더 29, 27, 25, 23번(GPIO70~67) → (6) 블록 |
| 토글 스위치 1~8 (선택) | A Side 핀-헤더 115~101번(홀수, GPIO25~18) → (7) 블록 |
| 가변저항 (선택) | B Side 핀-헤더 111번(ADCINA0) → (9) 가변전압 블록 출력 |

PC 케이블은 개발보드 BOTTOM면의 Mini-B 5핀 USB 커넥터에 연결하고, BOTTOM면의 USB ID 풀업
스위치는 High로 설정합니다.

![TMS320F28X EVM V2 — USB 디바이스(MSC) 예제 결선](5_USB_Dev_MSC_Driverlib/f28xevm_v2_usb_dev_msc.png)

## 메모리 사용량 비교 (CPU1_RAM 빌드 실측)

| | Bitfield | DriverLib | FreeRTOS |
|---|---|---|---|
| 합계 | 18,701 B (18.3 KB) | 26,169 B (25.6 KB) | 31,759 B (31.0 KB) |

링커 맵의 온칩 RAM 사용량 합산이며 스택 2KB가 포함됩니다.

## 프로세서 모듈

- [TMS320F28P650DK9 모듈(산업용)](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903200)
- [TMS320F28P659DK8-Q1 모듈(차량 전장용)](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903201)

## 개발 환경

CCS 21.x(Theia 기반) / TI CGT 22.6.3.LTS / C2000Ware 26.00.00.00

## Import 방법 (zip 직접 import도 지원)

압축을 미리 풀어서 "Select search-directory"로 지정하거나, GitHub에서 받은 zip 파일을
그대로 CCS의 "Select archive file"로 지정해도 됩니다 — 둘 다 정상 동작합니다.
