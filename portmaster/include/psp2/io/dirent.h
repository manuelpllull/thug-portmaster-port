#ifndef _PSP2_IO_DIRENT_H_
#define _PSP2_IO_DIRENT_H_

#include <psp2/types.h>
#include <psp2/io/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceIoDirent {
    SceIoStat d_stat;
    char d_name[256];
    void *d_private;
} SceIoDirent;

SceUID sceIoDopen(const char *dirname);
int sceIoDread(SceUID fd, SceIoDirent *dir);
int sceIoDclose(SceUID fd);

#ifdef __cplusplus
}
#endif

#endif // _PSP2_IO_DIRENT_H_
