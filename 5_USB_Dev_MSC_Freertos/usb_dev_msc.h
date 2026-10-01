// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — USB Device Mass Storage Class Header
// File: usb_dev_msc.h
// ==============================================================================

#ifndef USB_DEV_MSC_H
#define USB_DEV_MSC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB BOT Signatures
#define USB_MSC_CBW_SIGNATURE   0x43425355  // 'USBC'
#define USB_MSC_CSW_SIGNATURE   0x53425355  // 'USBS'

// SCSI Standard Commands
#define SCSI_TEST_UNIT_READY        0x00
#define SCSI_REQUEST_SENSE          0x03
#define SCSI_INQUIRY                0x12
#define SCSI_MODE_SENSE_6           0x1A
#define SCSI_START_STOP_UNIT        0x1B
#define SCSI_PREVENT_REMOVAL        0x1E
#define SCSI_READ_FORMAT_CAPACITIES 0x23
#define SCSI_READ_CAPACITY_10       0x25
#define SCSI_READ_10                0x28
#define SCSI_WRITE_10               0x2A

// SCSI Status Codes in CSW
#define CSW_STATUS_PASSED       0x00
#define CSW_STATUS_FAILED       0x01
#define CSW_STATUS_PHASE_ERROR  0x02

/**
 * USB MSC 디바이스 스택 초기화
 */
void USBDevMSC_init(void);

/**
 * USB MSC 메인 폴링 및 상태 머신 처리 (인터럽트 또는 배경 루프에서 호출)
 */
void USBDevMSC_process(void);

/**
 * USB 인터럽트 서비스 루틴 핸들러
 */
__interrupt void USBDevMSC_intHandler(void);

/**
 * USB 연결 및 설정(Configured) 상태 여부 확인
 */
bool USBDevMSC_isConfigured(void);

#ifdef __cplusplus
}
#endif

#endif // USB_DEV_MSC_H
