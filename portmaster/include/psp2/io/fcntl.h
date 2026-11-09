#ifndef _PSP2_IO_FCNTL_H_
#define _PSP2_IO_FCNTL_H_

#include <psp2/types.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_O_RDONLY   0x0001
#define SCE_O_WRONLY   0x0002
#define SCE_O_RDWR     0x0003
#define SCE_O_APPEND   0x0100
#define SCE_O_CREAT    0x0200
#define SCE_O_TRUNC    0x0400

#define SCE_SEEK_SET   0
#define SCE_SEEK_CUR   1
#define SCE_SEEK_END   2

SceUID sceIoOpen(const char *file, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoRemove(const char *file);
int sceIoMkdir(const char *dir, SceMode mode);
int sceIoRmdir(const char *dir);

static inline int sceIoSyncByFd(SceUID fd, int flag)
{
    return fsync(fd);
}

static inline int sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset)
{
    return pread(fd, data, size, offset);
}

#ifdef __cplusplus
}
#endif

#endif // _PSP2_IO_FCNTL_H_
