#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H
#include <stdint.h>
#define FLASH_CAPACITY 0x02000000u
#define FLASH_ASSET_BASE 0x00200000u
int flash_storage_init(void);
int flash_storage_read(uint32_t address,void *dst,uint32_t bytes);
void flash_storage_seal(void);
uint32_t flash_storage_jedec(void);
uint32_t flash_storage_reads(void);
#endif
