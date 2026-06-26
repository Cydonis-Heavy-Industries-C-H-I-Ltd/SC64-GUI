/* Validates the vendored FatFs (config + write + list) against a RAM disk.
   Proves writing a 2nd file preserves the 1st — the core "safe write" claim.
   This uses the SAME ff.c/ffconf.h the app links, just a different diskio. */
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SECTORS 131072            /* 64 MiB */
#define SS 512
static unsigned char disk[SECTORS * SS];

DSTATUS disk_status(BYTE pdrv){ (void)pdrv; return 0; }
DSTATUS disk_initialize(BYTE pdrv){ (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t s, UINT c){
    (void)pdrv; memcpy(buff, disk + (size_t)s*SS, (size_t)c*SS); return RES_OK; }
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t s, UINT c){
    (void)pdrv; memcpy(disk + (size_t)s*SS, buff, (size_t)c*SS); return RES_OK; }
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff){
    (void)pdrv;
    switch(cmd){
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_SIZE: *(WORD*)buff = SS; return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t*)buff = SECTORS; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD*)buff = 1; return RES_OK;
    }
    return RES_OK;
}
DWORD get_fattime(void){ return ((DWORD)(2025-1980)<<25)|(6<<21)|(26<<16); }

static int write_file(const char* path, const char* content){
    FIL f; UINT bw;
    if (f_open(&f, path, FA_CREATE_ALWAYS|FA_WRITE) != FR_OK) return -1;
    f_write(&f, content, (UINT)strlen(content), &bw);
    f_close(&f);
    return bw == strlen(content) ? 0 : -1;
}
static int read_file(const char* path, char* out, UINT max){
    FIL f; UINT br;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    f_read(&f, out, max-1, &br); out[br]=0; f_close(&f);
    return 0;
}

int main(void){
    FATFS fs; BYTE work[4096];
    MKFS_PARM opt = { FM_ANY, 0, 0, 0, 0 };

    if (f_mkfs("", &opt, work, sizeof work) != FR_OK){ printf("FAIL mkfs\n"); return 1; }
    if (f_mount(&fs, "", 1) != FR_OK){ printf("FAIL mount\n"); return 1; }

    /* write first file, then a second — second must not clobber first */
    if (write_file("/existing_save.eep", "ORIGINAL-DATA-1234")){ printf("FAIL write1\n"); return 1; }
    if (write_file("/My Long ROM Name.z64", "NEWFILE-PAYLOAD")){ printf("FAIL write2\n"); return 1; }

    /* remount to be sure it's all flushed to the "card" */
    f_mount(0, "", 0);
    if (f_mount(&fs, "", 1) != FR_OK){ printf("FAIL remount\n"); return 1; }

    char buf[64];
    read_file("/existing_save.eep", buf, sizeof buf);
    if (strcmp(buf, "ORIGINAL-DATA-1234")){ printf("FAIL: first file corrupted -> '%s'\n", buf); return 1; }
    read_file("/My Long ROM Name.z64", buf, sizeof buf);
    if (strcmp(buf, "NEWFILE-PAYLOAD")){ printf("FAIL: second file wrong -> '%s'\n", buf); return 1; }

    printf("PASS: both files intact after second write (LFN preserved)\n");
    printf("Directory listing:\n");
    DIR dir; FILINFO fno; f_opendir(&dir, "/");
    while (f_readdir(&dir,&fno)==FR_OK && fno.fname[0]) printf("  %-26s %lu B\n", fno.fname, (unsigned long)fno.fsize);
    f_closedir(&dir);
    return 0;
}
