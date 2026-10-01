// ==============================================================================
// SyncWorks TMS320F28P659 EVM V2 — USB MSC Virtual FAT12 Disk Header
// File: virtual_disk.h
// ==============================================================================

#ifndef VIRTUAL_DISK_H
#define VIRTUAL_DISK_H

#include <stdint.h>
#include <stdbool.h>
#include "c28x_uint8_compat.h"	// C28x는 stdint.h가 uint8_t를 정의하지 않아 직접 보강

#ifdef __cplusplus
extern "C" {
#endif

#define SECTOR_SIZE             512
#define TOTAL_SECTORS           128     // 128 * 512 = 64KB Virtual Disk
#define RESERVED_SECTORS        1       // Sector 0: Boot Sector (BPB)
#define SECTORS_PER_FAT         1       // Sector 1: FAT1, Sector 2: FAT2
#define NUM_FATS                2
#define ROOT_DIR_SECTORS        2       // Sectors 3 & 4 (32 entries * 32 bytes = 1024 bytes)
#define FIRST_DATA_SECTOR       5       // Sector 5 = Cluster 2

// Cluster assignments
#define CLUSTER_README          2       // Sector 5: README.TXT
#define CLUSTER_STATUS          3       // Sector 6: STATUS.TXT
#define CLUSTER_CONFIG          4       // Sector 7: CONFIG.INI

/**
 * 가상 디스크 파일시스템 초기화
 */
void VirtualDisk_init(void);

/**
 * 512바이트 섹터 읽기 (SCSI READ_10 콜백)
 * @param lba 논리 블록 주소 (0 ~ TOTAL_SECTORS - 1)
 * @param buffer512 읽은 512바이트를 채울 버퍼
 */
void VirtualDisk_readSector(uint32_t lba, uint8_t *buffer512);

/**
 * 512바이트 섹터 쓰기 (SCSI WRITE_10 콜백)
 * @param lba 논리 블록 주소
 * @param buffer512 기록할 512바이트 데이터
 */
void VirtualDisk_writeSector(uint32_t lba, const uint8_t *buffer512);

/**
 * CONFIG.INI 내용 버퍼 포인터 반환
 */
char* VirtualDisk_getConfigBuffer(void);

#ifdef __cplusplus
}
#endif

#endif // VIRTUAL_DISK_H
