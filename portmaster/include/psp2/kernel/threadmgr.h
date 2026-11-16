#ifndef _PSP2_KERNEL_THREADMGR_H_
#define _PSP2_KERNEL_THREADMGR_H_

#include <psp2/types.h>
#include <pthread.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);

typedef struct SceKernelThreadOptParam {
    SceSize size;
} SceKernelThreadOptParam;

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, int stackSize, unsigned int attr, int cpuAffinityMask, const SceKernelThreadOptParam *option);
int sceKernelStartThread(SceUID thid, SceSize args, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *stat, void *timeout);
int sceKernelDeleteThread(SceUID thid);

static inline int sceKernelDelayThread(unsigned int delay_us)
{
    return usleep(delay_us);
}

typedef struct SceKernelMutexOptParam {
    SceSize size;
} SceKernelMutexOptParam;

SceUID sceKernelCreateMutex(const char *name, unsigned int attr, int initCount, const SceKernelMutexOptParam *option);
int sceKernelLockMutex(SceUID mutexid, int count, unsigned int *timeout);
int sceKernelUnlockMutex(SceUID mutexid, int count);
int sceKernelDeleteMutex(SceUID mutexid);

#define SCE_KERNEL_CPU_MASK_USER_0 0x01
#define SCE_KERNEL_CPU_MASK_USER_1 0x02
#define SCE_KERNEL_CPU_MASK_USER_2 0x04

typedef struct SceKernelSemaOptParam {
    SceSize size;
} SceKernelSemaOptParam;

SceUID sceKernelCreateSema(const char *name, unsigned int attr, int initVal, int maxVal, const SceKernelSemaOptParam *opt);
int sceKernelWaitSema(SceUID semaid, int signal, unsigned int *timeout);
int sceKernelSignalSema(SceUID semaid, int signal);
int sceKernelDeleteSema(SceUID semaid);

#ifdef __cplusplus
}
#endif

#endif // _PSP2_KERNEL_THREADMGR_H_
