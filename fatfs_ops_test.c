/* Exercises the new FatFs operations (mkdir / rename / recursive delete)
   against a RAM disk, mirroring what Sc64FileSystem does. */
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>

#define SECTORS 131072
#define SS 512
static unsigned char disk[SECTORS*SS];
DSTATUS disk_status(BYTE p){(void)p;return 0;}
DSTATUS disk_initialize(BYTE p){(void)p;return 0;}
DRESULT disk_read(BYTE p,BYTE*b,LBA_t s,UINT c){(void)p;memcpy(b,disk+(size_t)s*SS,(size_t)c*SS);return RES_OK;}
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t s,UINT c){(void)p;memcpy(disk+(size_t)s*SS,b,(size_t)c*SS);return RES_OK;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void*b){(void)p;switch(cmd){case GET_SECTOR_SIZE:*(WORD*)b=SS;return RES_OK;case GET_SECTOR_COUNT:*(LBA_t*)b=SECTORS;return RES_OK;case GET_BLOCK_SIZE:*(DWORD*)b=1;return RES_OK;}return RES_OK;}
DWORD get_fattime(void){return ((DWORD)(2026-1980)<<25)|(6<<21)|(26<<16);}

static FRESULT rm_r(const char* path){          /* same logic as removeRecursive() */
    FILINFO fno; FRESULT fr=f_stat(path,&fno); if(fr!=FR_OK) return fr;
    if(fno.fattrib & AM_DIR){
        DIR d; fr=f_opendir(&d,path); if(fr!=FR_OK) return fr;
        for(;;){ FILINFO e; fr=f_readdir(&d,&e); if(fr!=FR_OK){f_closedir(&d);return fr;} if(!e.fname[0])break;
            char child[300]; snprintf(child,sizeof child,"%s/%s",path,e.fname);
            fr=rm_r(child); if(fr!=FR_OK){f_closedir(&d);return fr;} }
        f_closedir(&d);
    }
    return f_unlink(path);
}
static int mkfile(const char*p,const char*c){FIL f;UINT bw;if(f_open(&f,p,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return -1;f_write(&f,c,strlen(c),&bw);f_close(&f);return 0;}
static int exists(const char*p){FILINFO fno;return f_stat(p,&fno)==FR_OK;}

int main(void){
    FATFS fs; BYTE work[4096]; MKFS_PARM opt={FM_ANY,0,0,0,0};
    if(f_mkfs("",&opt,work,sizeof work)||f_mount(&fs,"",1)){printf("FAIL setup\n");return 1;}

    /* mkdir + nested mkdir + files inside */
    if(f_mkdir("/roms")){printf("FAIL mkdir /roms\n");return 1;}
    if(f_mkdir("/roms/sub")){printf("FAIL mkdir nested\n");return 1;}
    mkfile("/roms/a.z64","AAAA");
    mkfile("/roms/sub/b.z64","BBBB");
    mkfile("/keep.txt","KEEP");

    /* rename a file, then rename a folder */
    if(f_rename("/roms/a.z64","/roms/renamed.z64")){printf("FAIL rename file\n");return 1;}
    if(f_rename("/roms","/games")){printf("FAIL rename dir\n");return 1;}
    if(!exists("/games/renamed.z64")||!exists("/games/sub/b.z64")){printf("FAIL rename moved contents\n");return 1;}

    /* recursive delete of a non-empty folder; unrelated file must survive */
    if(rm_r("/games")){printf("FAIL recursive delete\n");return 1;}
    if(exists("/games")){printf("FAIL folder still present\n");return 1;}
    if(!exists("/keep.txt")){printf("FAIL unrelated file lost\n");return 1;}

    printf("PASS: mkdir, nested mkdir, rename file+folder, recursive delete; /keep.txt preserved\n");
    return 0;
}
