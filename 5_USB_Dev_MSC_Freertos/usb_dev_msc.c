// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — USB Device Mass Storage Class Source
// File: usb_dev_msc.c
// ==============================================================================

#include "device.h"
#include "driverlib.h"
#include "usblib.h"
#include "device/usbdevice.h"
#include "usb_dev_msc.h"
#include "virtual_disk.h"
#include <string.h>

extern void USBGPIOEnable(void);

// USB Endpoint assignments
#define MSC_BULK_IN_EP          USB_EP_1
#define MSC_BULK_OUT_EP         USB_EP_1
#define MSC_EP_MAX_PACKET_SIZE  64

// BOT State Machine
typedef enum {
    BOT_STATE_IDLE_WAIT_CBW,
    BOT_STATE_DATA_IN,
    BOT_STATE_DATA_OUT,
    BOT_STATE_SEND_CSW
} BotState_t;

static BotState_t s_botState = BOT_STATE_IDLE_WAIT_CBW;
static bool s_isConfigured = false;
static uint8_t s_cswStatus = CSW_STATUS_PASSED;
static uint32_t s_cswResidue = 0;

// EP0 Control Transfer State Machine
typedef enum {
    EP0_STATE_IDLE,
    EP0_STATE_TX,
    EP0_STATE_STATUS,
    EP0_STATE_STALL
} EP0State_t;

static volatile EP0State_t s_ep0State    = EP0_STATE_IDLE;
static volatile uint8_t    s_pendingAddr = 0;
static volatile bool       s_addrPending = false;

// Command Block Wrapper buffer (31 bytes)
// Byte 0..3:   'USBC'
// Byte 4..7:   dCBWTag
// Byte 8..11:  dCBWDataTransferLength
// Byte 12:     bmCBWFlags
// Byte 13:     bCBWLUN
// Byte 14:     bCBWCBLength
// Byte 15..30: CBWCB (SCSI command block, 16 bytes)
static uint8_t s_cbwBuf[31];

// Transfer tracking
static uint32_t s_curLba = 0;
static uint16_t s_sectorsRemaining = 0;
static uint16_t s_sectorOffset = 0;
static uint8_t  s_sectorBuf[SECTOR_SIZE];

// -----------------------------------------------------------------------------
// USB Descriptors
// -----------------------------------------------------------------------------
static const uint8_t s_deviceDescriptor[] = {
    18,                 // bLength
    0x01,               // bDescriptorType (Device)
    0x00, 0x02,         // bcdUSB (2.00)
    0x00,               // bDeviceClass
    0x00,               // bDeviceSubClass
    0x00,               // bDeviceProtocol
    64,                 // bMaxPacketSize0
    0xBE, 0x1C,         // idVendor (0x1CBE = Luminary / TI)
    0x05, 0x00,         // idProduct (0x0005)
    0x00, 0x01,         // bcdDevice (1.00)
    1,                  // iManufacturer (String 1)
    2,                  // iProduct (String 2)
    3,                  // iSerialNumber (String 3)
    1                   // bNumConfigurations
};

static const uint8_t s_configDescriptor[] = {
    // Configuration Descriptor
    9,                  // bLength
    0x02,               // bDescriptorType (Configuration)
    32, 0,              // wTotalLength (32 bytes)
    1,                  // bNumInterfaces
    1,                  // bConfigurationValue
    0,                  // iConfiguration
    0xC0,               // bmAttributes (Self-powered)
    50,                 // bMaxPower (100mA)

    // Interface Descriptor (Mass Storage Class)
    9,                  // bLength
    0x04,               // bDescriptorType (Interface)
    0,                  // bInterfaceNumber
    0,                  // bAlternateSetting
    2,                  // bNumEndpoints (EP1 IN, EP1 OUT)
    0x08,               // bInterfaceClass (Mass Storage)
    0x06,               // bInterfaceSubClass (SCSI Transparent)
    0x50,               // bInterfaceProtocol (Bulk-Only Transport)
    0,                  // iInterface

    // Endpoint 1 IN (Bulk IN)
    7,                  // bLength
    0x05,               // bDescriptorType (Endpoint)
    0x81,               // bEndpointAddress (EP1 IN)
    0x02,               // bmAttributes (Bulk)
    64, 0,              // wMaxPacketSize (64 bytes)
    0,                  // bInterval

    // Endpoint 1 OUT (Bulk OUT)
    7,                  // bLength
    0x05,               // bDescriptorType (Endpoint)
    0x01,               // bEndpointAddress (EP1 OUT)
    0x02,               // bmAttributes (Bulk)
    64, 0,              // wMaxPacketSize (64 bytes)
    0                   // bInterval
};

// String Descriptors
static const uint8_t s_langDescriptor[] = { 4, 0x03, 0x09, 0x04 }; // 0x0409: US English
static const uint8_t s_strManufacturer[] = { 20, 0x03, 'S',0,'y',0,'n',0,'c',0,'W',0,'o',0,'r',0,'k',0,'s',0 };
static const uint8_t s_strProduct[] = { 44, 0x03, 'F',0,'2',0,'8',0,'P',0,'6',0,'5',0,'9',0,' ',0,'V',0,'i',0,'r',0,'t',0,'u',0,'a',0,'l',0,' ',0,'D',0,'i',0,'s',0,'k',0 };
static const uint8_t s_strSerial[] = { 30, 0x03, 'S',0,'W',0,'-',0,'P',0,'6',0,'5',0,'9',0,'-',0,'0',0,'0',0,'1',0 };

// SCSI Inquiry Data (36 bytes)
static const uint8_t s_scsiInquiryData[36] = {
    0x00,               // Direct-access block device
    0x80,               // Removable media (RMB=1)
    0x02,               // ANSI SCSI-2
    0x02,               // Response data format
    31,                 // Additional length
    0x00, 0x00, 0x00,
    ' ',' ',' ',' ',' ',' ',' ',' ',                                  // Vendor ID (8 spaces)
    'S','y','n','c','W','o','r','k','s',' ','D','i','s','k',' ',' ', // Product ID (16 chars)
    '1','.','0','0'                                                   // Product Revision (4 chars)
};

// -----------------------------------------------------------------------------
// SCSI Command Handlers
// -----------------------------------------------------------------------------
static void sendCSW(uint8_t status, uint32_t residue) {
    uint8_t csw[13];

    csw[0]  = 'U';
    csw[1]  = 'S';
    csw[2]  = 'B';
    csw[3]  = 'S';
    csw[4]  = s_cbwBuf[4]; // Echo Tag
    csw[5]  = s_cbwBuf[5];
    csw[6]  = s_cbwBuf[6];
    csw[7]  = s_cbwBuf[7];
    csw[8]  = (uint8_t)(residue & 0xFF);
    csw[9]  = (uint8_t)((residue >> 8) & 0xFF);
    csw[10] = (uint8_t)((residue >> 16) & 0xFF);
    csw[11] = (uint8_t)((residue >> 24) & 0xFF);
    csw[12] = status;      // 0: Passed, 1: Failed, 2: Phase Error

    USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, csw, 13);
    USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);

    s_botState = BOT_STATE_IDLE_WAIT_CBW;
}

static void handleSCSICommand(void) {
    uint8_t opCode;
    uint32_t expLen;

    opCode = s_cbwBuf[15];
    expLen = (uint32_t)s_cbwBuf[8] |
             ((uint32_t)s_cbwBuf[9] << 8) |
             ((uint32_t)s_cbwBuf[10] << 16) |
             ((uint32_t)s_cbwBuf[11] << 24);

    switch (opCode) {
        case SCSI_TEST_UNIT_READY: {
            sendCSW(CSW_STATUS_PASSED, 0);
            break;
        }

        case SCSI_INQUIRY: {
            uint16_t sendLen;
            sendLen = (uint16_t)sizeof(s_scsiInquiryData);
            if (sendLen > expLen) {
                sendLen = (uint16_t)expLen;
            }

            USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, (uint8_t*)s_scsiInquiryData, sendLen);
            USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
            s_cswStatus = CSW_STATUS_PASSED;
            s_cswResidue = expLen - sendLen;
            s_botState = BOT_STATE_SEND_CSW;
            break;
        }

        case SCSI_READ_CAPACITY_10: {
            uint8_t capData[8];
            uint32_t lastLba;
            uint32_t blkSize;
            uint16_t sendLen;

            lastLba = TOTAL_SECTORS - 1;
            blkSize = SECTOR_SIZE;
            sendLen = 8;
            if (sendLen > expLen) {
                sendLen = (uint16_t)expLen;
            }

            // Big-endian formatting
            capData[0] = (uint8_t)((lastLba >> 24) & 0xFF);
            capData[1] = (uint8_t)((lastLba >> 16) & 0xFF);
            capData[2] = (uint8_t)((lastLba >> 8) & 0xFF);
            capData[3] = (uint8_t)(lastLba & 0xFF);

            capData[4] = (uint8_t)((blkSize >> 24) & 0xFF);
            capData[5] = (uint8_t)((blkSize >> 16) & 0xFF);
            capData[6] = (uint8_t)((blkSize >> 8) & 0xFF);
            capData[7] = (uint8_t)(blkSize & 0xFF);

            USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, capData, sendLen);
            USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
            s_cswStatus = CSW_STATUS_PASSED;
            s_cswResidue = expLen - sendLen;
            s_botState = BOT_STATE_SEND_CSW;
            break;
        }

        case SCSI_READ_FORMAT_CAPACITIES: {
            uint8_t formCap[12];
            uint32_t numBlocks;
            uint16_t sendLen;

            numBlocks = TOTAL_SECTORS;
            sendLen = 12;
            if (sendLen > expLen) {
                sendLen = (uint16_t)expLen;
            }

            // Capacity List Header (4 bytes)
            formCap[0] = 0;
            formCap[1] = 0;
            formCap[2] = 0;
            formCap[3] = 8; // Descriptor length (8 bytes)

            // Current Capacity Descriptor (8 bytes)
            formCap[4] = (uint8_t)((numBlocks >> 24) & 0xFF);
            formCap[5] = (uint8_t)((numBlocks >> 16) & 0xFF);
            formCap[6] = (uint8_t)((numBlocks >> 8) & 0xFF);
            formCap[7] = (uint8_t)(numBlocks & 0xFF);
            formCap[8] = 0x02; // Descriptor Code = 0x02 (Formatted Media)
            formCap[9] = 0;
            formCap[10] = (uint8_t)((SECTOR_SIZE >> 8) & 0xFF); // 0x02
            formCap[11] = (uint8_t)(SECTOR_SIZE & 0xFF);        // 0x00

            USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, formCap, sendLen);
            USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
            s_cswStatus = CSW_STATUS_PASSED;
            s_cswResidue = expLen - sendLen;
            s_botState = BOT_STATE_SEND_CSW;
            break;
        }

        case SCSI_REQUEST_SENSE: {
            uint8_t sense[18];
            uint16_t sendLen;

            sendLen = 18;
            if (sendLen > expLen) {
                sendLen = (uint16_t)expLen;
            }

            memset(sense, 0, sizeof(sense));
            sense[0] = 0x70; // Current error
            sense[2] = 0x00; // No sense
            sense[7] = 10;   // Additional length

            USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, sense, sendLen);
            USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
            s_cswStatus = CSW_STATUS_PASSED;
            s_cswResidue = expLen - sendLen;
            s_botState = BOT_STATE_SEND_CSW;
            break;
        }

        case SCSI_MODE_SENSE_6: {
            uint8_t mode[4];
            uint16_t sendLen;

            sendLen = 4;
            if (sendLen > expLen) {
                sendLen = (uint16_t)expLen;
            }

            mode[0] = 0x03; // Mode data length (3 bytes follow)
            mode[1] = 0x00; // Medium type (0x00 = Default)
            mode[2] = 0x00; // Device-specific param (0x00 = Write enabled)
            mode[3] = 0x00; // Block descriptor length (0)

            USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, mode, sendLen);
            USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
            s_cswStatus = CSW_STATUS_PASSED;
            s_cswResidue = expLen - sendLen;
            s_botState = BOT_STATE_SEND_CSW;
            break;
        }

        case SCSI_PREVENT_REMOVAL:
        case SCSI_START_STOP_UNIT: {
            sendCSW(CSW_STATUS_PASSED, 0);
            break;
        }

        case SCSI_READ_10: {
            s_curLba = ((uint32_t)s_cbwBuf[17] << 24) |
                       ((uint32_t)s_cbwBuf[18] << 16) |
                       ((uint32_t)s_cbwBuf[19] << 8)  |
                       ((uint32_t)s_cbwBuf[20]);

            s_sectorsRemaining = ((uint16_t)s_cbwBuf[22] << 8) |
                                 ((uint16_t)s_cbwBuf[23]);

            if (s_sectorsRemaining > 0) {
                VirtualDisk_readSector(s_curLba, s_sectorBuf);
                s_sectorOffset = 0;
                s_botState = BOT_STATE_DATA_IN;

                // Send first 64-byte chunk
                USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, &s_sectorBuf[0], MSC_EP_MAX_PACKET_SIZE);
                USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
                s_sectorOffset += MSC_EP_MAX_PACKET_SIZE;
            } else {
                sendCSW(CSW_STATUS_PASSED, 0);
            }
            break;
        }

        case SCSI_WRITE_10: {
            s_curLba = ((uint32_t)s_cbwBuf[17] << 24) |
                       ((uint32_t)s_cbwBuf[18] << 16) |
                       ((uint32_t)s_cbwBuf[19] << 8)  |
                       ((uint32_t)s_cbwBuf[20]);

            s_sectorsRemaining = ((uint16_t)s_cbwBuf[22] << 8) |
                                 ((uint16_t)s_cbwBuf[23]);

            s_sectorOffset = 0;
            s_botState = BOT_STATE_DATA_OUT;
            break;
        }

        default: {
            if (expLen > 0) {
                if (s_cbwBuf[12] & 0x80) {
                    USBDevEndpointStall(USBA_BASE, MSC_BULK_IN_EP, USB_EP_DEV_IN);
                } else {
                    USBDevEndpointStall(USBA_BASE, MSC_BULK_OUT_EP, USB_EP_DEV_OUT);
                }
            }
            sendCSW(CSW_STATUS_FAILED, expLen);
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Endpoint 0 (Control) Request Handler
// -----------------------------------------------------------------------------
static void handleEP0Setup(void) {
    uint8_t req[8];
    uint32_t reqLen;
    int32_t ret;
    uint8_t  bmReqType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;

    reqLen = sizeof(req);
    ret = USBEndpointDataGet(USBA_BASE, USB_EP_0, req, &reqLen);
    if (ret != 0 || reqLen < 8) return;

    bmReqType = req[0];
    bRequest  = req[1];
    wValue    = (uint16_t)req[2] | ((uint16_t)req[3] << 8);
    wIndex    = (uint16_t)req[4] | ((uint16_t)req[5] << 8);
    wLength   = (uint16_t)req[6] | ((uint16_t)req[7] << 8);
    (void)wIndex;

    // MSC Class-Specific Requests
    if ((bmReqType & 0x60) == 0x20) {
        if (bRequest == 0xFE) {
            // GET_MAX_LUN (return 1 byte: LUN=0)
            uint8_t maxLun = 0;
            USBDevEndpointDataAck(USBA_BASE, USB_EP_0, false);
            USBEndpointDataPut(USBA_BASE, USB_EP_0, &maxLun, 1);
            USBEndpointDataSend(USBA_BASE, USB_EP_0, USB_TRANS_IN_LAST);
            s_ep0State = EP0_STATE_STATUS;
            return;
        } else if (bRequest == 0xFF) {
            // BULK_ONLY_RESET
            s_botState = BOT_STATE_IDLE_WAIT_CBW;
            USBDevEndpointStallClear(USBA_BASE, MSC_BULK_IN_EP, USB_EP_DEV_IN);
            USBDevEndpointStallClear(USBA_BASE, MSC_BULK_OUT_EP, USB_EP_DEV_OUT);
            USBDevEndpointDataAck(USBA_BASE, USB_EP_0, true);
            s_ep0State = EP0_STATE_STATUS;
            return;
        } else {
            USBDevEndpointStall(USBA_BASE, USB_EP_0, USB_EP_DEV_OUT);
            s_ep0State = EP0_STATE_STALL;
            return;
        }
    }

    // Standard USB Requests
    switch (bRequest) {
        case 0x06: { // GET_DESCRIPTOR
            uint8_t descType = (uint8_t)(wValue >> 8);
            uint8_t descIndex = (uint8_t)(wValue & 0xFF);
            const uint8_t *descPtr = NULL;
            uint16_t descLen = 0;

            if (descType == 0x01) { // DEVICE
                descPtr = s_deviceDescriptor;
                descLen = sizeof(s_deviceDescriptor);
            } else if (descType == 0x02) { // CONFIGURATION
                descPtr = s_configDescriptor;
                descLen = sizeof(s_configDescriptor);
            } else if (descType == 0x03) { // STRING
                if (descIndex == 0) {
                    descPtr = s_langDescriptor;
                    descLen = sizeof(s_langDescriptor);
                } else if (descIndex == 1) {
                    descPtr = s_strManufacturer;
                    descLen = sizeof(s_strManufacturer);
                } else if (descIndex == 2) {
                    descPtr = s_strProduct;
                    descLen = sizeof(s_strProduct);
                } else if (descIndex == 3) {
                    descPtr = s_strSerial;
                    descLen = sizeof(s_strSerial);
                }
            }

            if (descPtr != NULL) {
                uint16_t sendLen = (wLength < descLen) ? wLength : descLen;
                USBDevEndpointDataAck(USBA_BASE, USB_EP_0, false);
                USBEndpointDataPut(USBA_BASE, USB_EP_0, (uint8_t*)descPtr, sendLen);
                USBEndpointDataSend(USBA_BASE, USB_EP_0, USB_TRANS_IN_LAST);
                s_ep0State = EP0_STATE_STATUS;
            } else {
                // Unsupported descriptor (BOS 0x0F, Qualifier 0x06, String 0xEE, etc.) -> STALL
                USBDevEndpointStall(USBA_BASE, USB_EP_0, USB_EP_DEV_OUT);
                s_ep0State = EP0_STATE_STALL;
            }
            break;
        }

        case 0x05: { // SET_ADDRESS
            s_pendingAddr = (uint8_t)(wValue & 0x7F);
            s_addrPending = true;
            s_ep0State = EP0_STATE_STATUS;
            USBDevEndpointDataAck(USBA_BASE, USB_EP_0, true);
            break;
        }

        case 0x09: { // SET_CONFIGURATION
            s_isConfigured = (wValue > 0);
            if (s_isConfigured) {
                // Re-initialize endpoints and configure hardware FIFOs
                USBDevEndpointConfigSet(USBA_BASE, MSC_BULK_IN_EP, MSC_EP_MAX_PACKET_SIZE, USB_EP_DEV_IN | USB_EP_MODE_BULK);
                USBDevEndpointConfigSet(USBA_BASE, MSC_BULK_OUT_EP, MSC_EP_MAX_PACKET_SIZE, USB_EP_DEV_OUT | USB_EP_MODE_BULK);
                USBFIFOConfigSet(USBA_BASE, MSC_BULK_IN_EP, 64, USB_FIFO_SZ_64, USB_EP_DEV_IN);
                USBFIFOConfigSet(USBA_BASE, MSC_BULK_OUT_EP, 128, USB_FIFO_SZ_64, USB_EP_DEV_OUT);
                s_botState = BOT_STATE_IDLE_WAIT_CBW;
            }
            s_ep0State = EP0_STATE_STATUS;
            USBDevEndpointDataAck(USBA_BASE, USB_EP_0, true);
            break;
        }

        case 0x00: { // GET_STATUS
            uint8_t statusBuf[2];
            statusBuf[0] = 0x01; // Self powered (bit 0 = 1)
            statusBuf[1] = 0x00;
            USBDevEndpointDataAck(USBA_BASE, USB_EP_0, false);
            USBEndpointDataPut(USBA_BASE, USB_EP_0, statusBuf, 2);
            USBEndpointDataSend(USBA_BASE, USB_EP_0, USB_TRANS_IN_LAST);
            s_ep0State = EP0_STATE_STATUS;
            break;
        }

        default: {
            USBDevEndpointStall(USBA_BASE, USB_EP_0, USB_EP_DEV_OUT);
            s_ep0State = EP0_STATE_STALL;
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Public Functions
// -----------------------------------------------------------------------------
void USBDevMSC_init(void) {
    VirtualDisk_init();
    s_botState = BOT_STATE_IDLE_WAIT_CBW;
    s_isConfigured = false;
    s_ep0State = EP0_STATE_IDLE;
    s_pendingAddr = 0;
    s_addrPending = false;

    // Reset USB Controller
    SysCtl_resetPeripheral(SYSCTL_PERIPH_RES_USBA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_USBA);

    // Setup Pins
    USBGPIOEnable();

    // Set Hardware USB Controller to Device Mode
    USBDevMode(USBA_BASE);

    // Explicitly set address to 0
    USBDevAddrSet(USBA_BASE, 0);

    // Initialize Device Mode Stack
    USBStackModeSet(0, eUSBModeDevice, NULL);

    // Configure Endpoints: EP1 IN (Bulk), EP1 OUT (Bulk)
    USBDevEndpointConfigSet(USBA_BASE, MSC_BULK_IN_EP, MSC_EP_MAX_PACKET_SIZE, USB_EP_DEV_IN | USB_EP_MODE_BULK);
    USBDevEndpointConfigSet(USBA_BASE, MSC_BULK_OUT_EP, MSC_EP_MAX_PACKET_SIZE, USB_EP_DEV_OUT | USB_EP_MODE_BULK);

    // Allocate Endpoint FIFOs in USB Controller Shared SRAM:
    // EP0: 0..63 (64 bytes, hardware fixed)
    // EP1 IN: 64..127 (64 bytes)
    // EP1 OUT: 128..191 (64 bytes)
    USBFIFOConfigSet(USBA_BASE, MSC_BULK_IN_EP, 64, USB_FIFO_SZ_64, USB_EP_DEV_IN);
    USBFIFOConfigSet(USBA_BASE, MSC_BULK_OUT_EP, 128, USB_FIFO_SZ_64, USB_EP_DEV_OUT);

    // Clear any pending interrupts
    USBIntStatusControl(USBA_BASE);
    USBIntStatusEndpoint(USBA_BASE);

    // Enable USB Controller Interrupts
    USBIntEnableControl(USBA_BASE, USB_INTCTRL_RESET |
                                   USB_INTCTRL_DISCONNECT |
                                   USB_INTCTRL_RESUME |
                                   USB_INTCTRL_SUSPEND);
    USBIntEnableEndpoint(USBA_BASE, USB_INTEP_0 | USB_INTEP_DEV_OUT_1 | USB_INTEP_DEV_IN_1);

    // Enable Global USB Interrupt (wrapper level)
    USBEnableGlobalInterrupt(USBA_BASE);

    // Connect to USB Bus (soft connect pull-up on D+)
    USBDevConnect(USBA_BASE);
}

bool USBDevMSC_isConfigured(void) {
    return s_isConfigured;
}

void USBDevMSC_process(void) {
    // 1. Process Data IN (Sector continuation fallback if polled)
    if (s_botState == BOT_STATE_DATA_IN) {
        if ((USBEndpointStatus(USBA_BASE, MSC_BULK_IN_EP) & USB_DEV_TX_TXPKTRDY) == 0) {
            if (s_sectorOffset < SECTOR_SIZE) {
                uint16_t chunk = SECTOR_SIZE - s_sectorOffset;
                if (chunk > MSC_EP_MAX_PACKET_SIZE) chunk = MSC_EP_MAX_PACKET_SIZE;

                USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, &s_sectorBuf[s_sectorOffset], chunk);
                USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
                s_sectorOffset += chunk;
            } else {
                s_sectorsRemaining--;
                s_curLba++;
                if (s_sectorsRemaining > 0) {
                    VirtualDisk_readSector(s_curLba, s_sectorBuf);
                    s_sectorOffset = 0;
                    USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, &s_sectorBuf[0], MSC_EP_MAX_PACKET_SIZE);
                    USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
                    s_sectorOffset += MSC_EP_MAX_PACKET_SIZE;
                } else {
                    s_cswStatus = CSW_STATUS_PASSED;
                    s_cswResidue = 0;
                    s_botState = BOT_STATE_SEND_CSW;
                }
            }
        }
    }
    // 2. Process CSW transmission fallback
    else if (s_botState == BOT_STATE_SEND_CSW) {
        if ((USBEndpointStatus(USBA_BASE, MSC_BULK_IN_EP) & USB_DEV_TX_TXPKTRDY) == 0) {
            sendCSW(s_cswStatus, s_cswResidue);
        }
    }
}

__interrupt void USBDevMSC_intHandler(void) {
    uint32_t statusCtrl;
    uint32_t statusEp;
    uint32_t ep0Status;
    uint32_t cbwLen;
    int32_t res;
    uint16_t chunk;
    uint32_t chunkLen;

    statusCtrl = USBIntStatusControl(USBA_BASE);
    statusEp   = USBIntStatusEndpoint(USBA_BASE);

    // 1. Bus Reset Interrupt
    if (statusCtrl & USB_INTCTRL_RESET) {
        USBDevAddrSet(USBA_BASE, 0);
        s_pendingAddr = 0;
        s_addrPending = false;
        s_ep0State = EP0_STATE_IDLE;
        s_botState = BOT_STATE_IDLE_WAIT_CBW;
        s_isConfigured = false;
    }

    // 2. Control Endpoint 0 Interrupt
    if (statusEp & USB_INTEP_0) {
        ep0Status = USBEndpointStatus(USBA_BASE, USB_EP_0);

        // Clear Sent Stall condition
        if (ep0Status & USB_DEV_EP0_SENT_STALL) {
            USBDevEndpointStatusClear(USBA_BASE, USB_EP_0, USB_DEV_EP0_SENT_STALL);
            s_ep0State = EP0_STATE_IDLE;
        }

        // Clear Setup End condition
        if (ep0Status & USB_DEV_EP0_SETUP_END) {
            USBDevEndpointStatusClear(USBA_BASE, USB_EP_0, USB_DEV_EP0_SETUP_END);
            s_ep0State = EP0_STATE_IDLE;
        }

        // Status phase transition
        if (s_ep0State == EP0_STATE_STATUS) {
            s_ep0State = EP0_STATE_IDLE;
            if (s_addrPending) {
                s_addrPending = false;
                USBDevAddrSet(USBA_BASE, s_pendingAddr);
            }
        }

        // New setup packet arrived
        if (ep0Status & USB_DEV_EP0_OUT_PKTRDY) {
            handleEP0Setup();
        }
    }

    // 3. Bulk OUT (EP1) Interrupt
    if (statusEp & USB_INTEP_DEV_OUT_1) {
        if (s_botState == BOT_STATE_IDLE_WAIT_CBW) {
            cbwLen = 31;
            res = USBEndpointDataGet(USBA_BASE, MSC_BULK_OUT_EP, s_cbwBuf, &cbwLen);
            if (res == 0 && cbwLen == 31 &&
                s_cbwBuf[0] == 'U' && s_cbwBuf[1] == 'S' &&
                s_cbwBuf[2] == 'B' && s_cbwBuf[3] == 'C') {
                USBDevEndpointDataAck(USBA_BASE, MSC_BULK_OUT_EP, true);
                handleSCSICommand();
            }
        } else if (s_botState == BOT_STATE_DATA_OUT) {
            chunk = SECTOR_SIZE - s_sectorOffset;
            if (chunk > MSC_EP_MAX_PACKET_SIZE) chunk = MSC_EP_MAX_PACKET_SIZE;
            chunkLen = chunk;
            res = USBEndpointDataGet(USBA_BASE, MSC_BULK_OUT_EP, &s_sectorBuf[s_sectorOffset], &chunkLen);
            if (res == 0) {
                USBDevEndpointDataAck(USBA_BASE, MSC_BULK_OUT_EP, true);
                s_sectorOffset += (uint16_t)chunkLen;

                if (s_sectorOffset >= SECTOR_SIZE) {
                    VirtualDisk_writeSector(s_curLba, s_sectorBuf);
                    s_sectorsRemaining--;
                    s_curLba++;
                    s_sectorOffset = 0;

                    if (s_sectorsRemaining == 0) {
                        sendCSW(CSW_STATUS_PASSED, 0);
                    }
                }
            }
        }
    }

    // 4. Bulk IN (EP1) Interrupt
    if (statusEp & USB_INTEP_DEV_IN_1) {
        if (s_botState == BOT_STATE_SEND_CSW) {
            sendCSW(s_cswStatus, s_cswResidue);
        } else if (s_botState == BOT_STATE_DATA_IN) {
            if (s_sectorOffset < SECTOR_SIZE) {
                chunk = SECTOR_SIZE - s_sectorOffset;
                if (chunk > MSC_EP_MAX_PACKET_SIZE) chunk = MSC_EP_MAX_PACKET_SIZE;

                USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, &s_sectorBuf[s_sectorOffset], chunk);
                USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
                s_sectorOffset += chunk;
            } else {
                s_sectorsRemaining--;
                s_curLba++;
                if (s_sectorsRemaining > 0) {
                    VirtualDisk_readSector(s_curLba, s_sectorBuf);
                    s_sectorOffset = 0;
                    USBEndpointDataPut(USBA_BASE, MSC_BULK_IN_EP, &s_sectorBuf[0], MSC_EP_MAX_PACKET_SIZE);
                    USBEndpointDataSend(USBA_BASE, MSC_BULK_IN_EP, USB_TRANS_IN);
                    s_sectorOffset += MSC_EP_MAX_PACKET_SIZE;
                } else {
                    s_cswStatus = CSW_STATUS_PASSED;
                    s_cswResidue = 0;
                    sendCSW(s_cswStatus, s_cswResidue);
                }
            }
        }
    }

    // Clear Global Interrupt Flag (peripheral wrapper level)
    USBClearGlobalInterruptFlag(USBA_BASE);

    // Acknowledge PIE Group 9
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}
