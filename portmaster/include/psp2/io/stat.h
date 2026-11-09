#ifndef _PSP2_IO_STAT_H_
#define _PSP2_IO_STAT_H_

#include <psp2/types.h>
#include <psp2/rtc.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef st_atime
#undef st_atime
#endif
#ifdef st_ctime
#undef st_ctime
#endif
#ifdef st_mtime
#undef st_mtime
#endif

#define SCE_S_IFMT   0xF000
#define SCE_S_IFREG  0x1000
#define SCE_S_IFDIR  0x2000

#define SCE_S_ISREG(m) (((m) & SCE_S_IFMT) == SCE_S_IFREG)
#define SCE_S_ISDIR(m) (((m) & SCE_S_IFMT) == SCE_S_IFDIR)

typedef struct SceIoStat {
    SceMode st_mode;
    unsigned int st_attr;
    SceOff st_size;
    SceDateTime st_ctime;
    SceDateTime st_atime;
    SceDateTime st_mtime;
    unsigned int st_private[6];
} SceIoStat;

int sceIoGetstat(const char *file, SceIoStat *stat);

#ifdef __cplusplus
}
#endif

#endif // _PSP2_IO_STAT_H_
