// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — USB MSC Virtual FAT12 Disk Source
// File: virtual_disk.c
// ==============================================================================

#include "virtual_disk.h"
#include "config_parser.h"
#include "status_generator.h"
#include <string.h>

static char s_configBuffer[SECTOR_SIZE];

static const char s_readmeText[] =
    "================================================\r\n"
    " SyncWorks TMS320F28P659 EVM V2 Virtual Disk\r\n"
    "================================================\r\n"
    "Running in RAM of TI TMS320F28P659 DSP (200MHz)!\r\n\r\n"
    "Files on this disk:\r\n"
    "1. README.TXT : Board overview and instructions.\r\n"
    "2. STATUS.TXT : Live telemetry (Reopen to refresh).\r\n"
    "3. CONFIG.INI : Hardware parameter tuning file.\r\n"
    "                (Edit values and Ctrl+S to save)\r\n\r\n"
    "Website: https://www.tms320f28x.co.kr\r\n";

void VirtualDisk_init(void) {
    ConfigParser_init();
    memset(s_configBuffer, 0, sizeof(s_configBuffer));
    ConfigParser_generateDefaultText(s_configBuffer, sizeof(s_configBuffer));
}

char* VirtualDisk_getConfigBuffer(void) {
    return s_configBuffer;
}

void VirtualDisk_readSector(uint32_t lba, uint8_t *b) {
    uint8_t *entry;
    uint32_t readmeLen;
    uint32_t cfgLen;
    uint16_t statusLen;
    uint32_t sec;
    uint16_t fatTime;
    uint16_t fatDate;
    static char s_tempBuf[SECTOR_SIZE];

    memset(b, 0, SECTOR_SIZE);

    if (lba == 0) {
        // -------------------------------------------------------------
        // Sector 0: Boot Sector / BPB (BIOS Parameter Block)
        // -------------------------------------------------------------
        b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;               // Jump instruction
        memcpy(&b[3], "MSDOS5.0", 8);                         // OEM Name
        b[11] = 0x00; b[12] = 0x02;                           // Bytes per sector = 512
        b[13] = 0x01;                                         // Sectors per cluster = 1
        b[14] = (uint8_t)(RESERVED_SECTORS & 0xFF);           // Reserved sectors = 1
        b[15] = 0x00;
        b[16] = NUM_FATS;                                     // Number of FATs = 2
        b[17] = 0x20; b[18] = 0x00;                           // Root entries = 32
        b[19] = (uint8_t)(TOTAL_SECTORS & 0xFF);              // Total sectors (128)
        b[20] = 0x00;
        b[21] = 0xF8;                                         // Media descriptor (Fixed disk)
        b[22] = (uint8_t)(SECTORS_PER_FAT & 0xFF);            // Sectors per FAT = 1
        b[23] = 0x00;
        b[24] = 0x01; b[25] = 0x00;                           // Sectors per track = 1
        b[26] = 0x01; b[27] = 0x00;                           // Number of heads = 1
        b[36] = 0x80;                                         // Drive number
        b[38] = 0x29;                                         // Extended boot signature
        b[39] = 0x12; b[40] = 0x34; b[41] = 0x56; b[42] = 0x78;// Volume ID
        memcpy(&b[43], "F28P659DISK", 11);                    // Volume label
        memcpy(&b[54], "FAT12   ", 8);                        // File system type
        b[510] = 0x55; b[511] = 0xAA;                         // Boot signature
    }
    else if (lba == 1 || lba == 2) {
        // -------------------------------------------------------------
        // Sector 1 (FAT1) & Sector 2 (FAT2)
        // FAT12 Entry: 12-bit per cluster
        // Clust 0: 0xFF8 (Media descriptor)
        // Clust 1: 0xFFF (EOF)
        // Clust 2: 0xFFF (README.TXT EOF)
        // Clust 3: 0xFFF (STATUS.TXT EOF)
        // Clust 4: 0xFFF (CONFIG.INI EOF)
        // Clust 5: 0x000 (Free)
        // -------------------------------------------------------------
        b[0] = 0xF8; b[1] = 0xFF; b[2] = 0xFF; // Clust 0, 1: 0xFF8, 0xFFF
        b[3] = 0xFF; b[4] = 0xFF; b[5] = 0xFF; // Clust 2, 3: 0xFFF, 0xFFF
        b[6] = 0xFF; b[7] = 0x0F; b[8] = 0x00; // Clust 4, 5: 0xFFF, 0x000
    }
    else if (lba == 3) {
        // -------------------------------------------------------------
        // Sector 3: Root Directory (Part 1: Entries 0 ~ 15)
        // Each entry: 32 bytes
        // -------------------------------------------------------------

        // Entry 0: Volume Label
        entry = &b[0];
        memcpy(&entry[0], "F28P659     ", 11);
        entry[11] = 0x08; // Volume Label attribute

        // Entry 1: README.TXT (Cluster 2)
        entry = &b[32];
        memcpy(&entry[0], "README  TXT", 11);
        entry[11] = 0x20; // Archive
        entry[26] = (uint8_t)(CLUSTER_README & 0xFF);
        entry[27] = 0x00; // Start cluster = 2
        readmeLen = (uint32_t)sizeof(s_readmeText) - 1;
        entry[28] = (uint8_t)(readmeLen & 0xFF);
        entry[29] = (uint8_t)((readmeLen >> 8) & 0xFF);

        // Entry 2: STATUS.TXT (Cluster 3)
        entry = &b[64];
        memcpy(&entry[0], "STATUS  TXT", 11);
        entry[11] = 0x20; // Archive
        sec = g_telemetry.uptimeSec;
        fatTime = (uint16_t)(((sec % 60) / 2) | (((sec / 60) % 60) << 5) | (((sec / 3600) % 24) << 11));
        fatDate = (uint16_t)(1 | (1 << 5) | (46 << 9)); // 2026-01-01
        entry[22] = (uint8_t)(fatTime & 0xFF);
        entry[23] = (uint8_t)((fatTime >> 8) & 0xFF);
        entry[24] = (uint8_t)(fatDate & 0xFF);
        entry[25] = (uint8_t)((fatDate >> 8) & 0xFF);
        entry[26] = (uint8_t)(CLUSTER_STATUS & 0xFF);
        entry[27] = 0x00; // Start cluster = 3
        statusLen = StatusGenerator_generate(s_tempBuf, sizeof(s_tempBuf));
        entry[28] = (uint8_t)(statusLen & 0xFF);
        entry[29] = (uint8_t)((statusLen >> 8) & 0xFF);

        // Entry 3: CONFIG.INI (Cluster 4)
        entry = &b[96];
        memcpy(&entry[0], "CONFIG  INI", 11);
        entry[11] = 0x20; // Archive
        entry[26] = (uint8_t)(CLUSTER_CONFIG & 0xFF);
        entry[27] = 0x00; // Start cluster = 4
        cfgLen = (uint32_t)strlen(s_configBuffer);
        entry[28] = (uint8_t)(cfgLen & 0xFF);
        entry[29] = (uint8_t)((cfgLen >> 8) & 0xFF);
    }
    else if (lba == 4) {
        // Sector 4: Root Directory (Part 2: Entries 16 ~ 31, all empty)
        // Already zeroed
    }
    else if (lba == 5) {
        // -------------------------------------------------------------
        // Sector 5 (Cluster 2): README.TXT Content
        // -------------------------------------------------------------
        memcpy(b, s_readmeText, sizeof(s_readmeText) - 1);
    }
    else if (lba == 6) {
        // -------------------------------------------------------------
        // Sector 6 (Cluster 3): STATUS.TXT Content (Dynamically Generated)
        // -------------------------------------------------------------
        StatusGenerator_generate((char*)b, SECTOR_SIZE);
    }
    else if (lba == 7) {
        // -------------------------------------------------------------
        // Sector 7 (Cluster 4): CONFIG.INI Content (RAM buffer)
        // -------------------------------------------------------------
        memcpy(b, s_configBuffer, SECTOR_SIZE);
    }
}

void VirtualDisk_writeSector(uint32_t lba, const uint8_t *b) {
    if (lba == 7) {
        // -------------------------------------------------------------
        // Sector 7 (Cluster 4): User saved CONFIG.INI!
        // -------------------------------------------------------------
        memcpy(s_configBuffer, b, SECTOR_SIZE);
        s_configBuffer[SECTOR_SIZE - 1] = '\0'; // ensure null termination

        // Parse modified parameters and update system behavior
        ConfigParser_parse(s_configBuffer);
    }
}
