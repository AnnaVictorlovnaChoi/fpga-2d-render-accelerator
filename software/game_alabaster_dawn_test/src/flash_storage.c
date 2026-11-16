#include "flash_storage.h"
#include "bsp.h"
#include "io.h"
#include "spi.h"
#include <stdint.h>
#define APPFIX_FLASH_SPI 0xf8014000u
#define APPFIX_FLASH_CS 0u
#define W25Q256_TIMEOUT_US 100000u
static uint32_t last_id,error_code,reads; static int sealed,initialized;
static unsigned long long load_end;
static unsigned long long now_ticks(void){return clint_getTime(SYSTEM_CLINT_CTRL);}
static unsigned long long deadline(void){unsigned long long e=now_ticks()+(unsigned long long)(SYSTEM_CLINT_HZ/1000000u)*W25Q256_TIMEOUT_US;return load_end && load_end<e?load_end:e;}
static int wait_cmd(unsigned long long end){while(!(read_u32(APPFIX_FLASH_SPI+SPI_BUFFER)&0xffffu))if(now_ticks()>=end)return -10;return 0;}
static int drain(void){unsigned long long end=deadline();bsp_uDelay(2);while((read_u32(APPFIX_FLASH_SPI+SPI_BUFFER)&0xffffu)!=SYSTEM_SPI_0_IO_PARAMETER_CMD_FIFO_DEPTH)if(now_ticks()>=end)return -12;return 0;}
static int tx(uint8_t value,uint8_t *reply,int readback)
{
    unsigned long long end=deadline();if(load_end && now_ticks()>=load_end)return -23;if(wait_cmd(end))return -10;
    write_u32(value|SPI_CMD_WRITE|(readback?SPI_CMD_READ:0),APPFIX_FLASH_SPI+SPI_DATA);
    if(readback){while(!(read_u32(APPFIX_FLASH_SPI+SPI_BUFFER)>>16))if(now_ticks()>=end)return -11;
        uint8_t b=(uint8_t)read_u32(APPFIX_FLASH_SPI+SPI_DATA);if(reply)*reply=b;}
    return 0;
}
static int select_flash(void){if(wait_cmd(deadline()))return -20;write_u32(APPFIX_FLASH_CS|0x80u|SPI_CMD_SS,APPFIX_FLASH_SPI+SPI_DATA);return 0;}
static int command_end(int rc){int e=wait_cmd(deadline());if(!e){write_u32(APPFIX_FLASH_CS|SPI_CMD_SS,APPFIX_FLASH_SPI+SPI_DATA);e=drain();}if(rc)error_code=(uint32_t)-rc;else if(e)error_code=(uint32_t)-e;return rc?rc:e;}
static int board_init(void)
{
    unsigned n=0;error_code=last_id=0;load_end=0;
    write_u32(0,APPFIX_FLASH_SPI+SPI_CONFIG);write_u32(9,APPFIX_FLASH_SPI+SPI_CLK_DIVIDER);
    write_u32(5,APPFIX_FLASH_SPI+SPI_SS_SETUP);write_u32(5,APPFIX_FLASH_SPI+SPI_SS_HOLD);write_u32(5,APPFIX_FLASH_SPI+SPI_SS_DISABLE);
    while(read_u32(APPFIX_FLASH_SPI+SPI_BUFFER)>>16){(void)read_u32(APPFIX_FLASH_SPI+SPI_DATA);if(++n>256u)return -13;}
    return command_end(0);
}
static int read_jedec(uint32_t *id)
{
    uint8_t a=0,b=0,c=0;int rc=select_flash();if(!rc)rc=tx(0x9f,0,0);if(!rc)rc=tx(0,&a,1);if(!rc)rc=tx(0,&b,1);if(!rc)rc=tx(0,&c,1);rc=command_end(rc);
    if(rc)return rc;last_id=((uint32_t)a<<16)|((uint32_t)b<<8)|c;if(id)*id=last_id;return 0;
}
static int read4(uint32_t address,uint8_t*dest,unsigned length)
{
    unsigned i;int rc;
    if(address>=0x02000000u||length>0x02000000u-address)return -22;
    rc=select_flash();if(!rc)rc=tx(0x13,0,0);if(!rc)rc=tx((uint8_t)(address>>24),0,0);if(!rc)rc=tx((uint8_t)(address>>16),0,0);if(!rc)rc=tx((uint8_t)(address>>8),0,0);if(!rc)rc=tx((uint8_t)address,0,0);
    for(i=0;!rc&&i<length;++i)rc=tx(0,&dest[i],1);return command_end(rc);
}
int flash_storage_init(void){int rc;if(sealed)return -40;if(initialized)return 0;rc=board_init();if(!rc)rc=read_jedec(0);if(!rc && last_id!=0xef4019u && last_id!=0xef6019u)rc=-41;if(!rc){initialized=1;load_end=now_ticks()+(unsigned long long)SYSTEM_CLINT_HZ*120u;}return rc;}
int flash_storage_read(uint32_t address,void *dest,uint32_t bytes){uint8_t*d=(uint8_t*)dest;int rc;if(sealed||!initialized)return -40;if(!d||address>FLASH_CAPACITY||bytes>FLASH_CAPACITY-address)return -22;while(bytes){unsigned n=bytes>4096u?4096u:bytes;rc=read4(address,d,n);++reads;if(rc)return rc;address+=n;d+=n;bytes-=n;}return 0;}
void flash_storage_seal(void){sealed=1;}
uint32_t flash_storage_jedec(void){return last_id;}
uint32_t flash_storage_reads(void){return reads;}
