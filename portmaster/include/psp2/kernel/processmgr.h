#ifndef _PSP2_KERNEL_PROCESSMGR_H_
#define _PSP2_KERNEL_PROCESSMGR_H_

#include <psp2/types.h>
#include <time.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND 0
#define SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF 1
static inline int sceKernelPowerTick(int type) { return 0; }

static inline SceUInt64 sceKernelGetProcessTimeWide(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (SceUInt64)ts.tv_sec * 1000000ULL + (SceUInt64)ts.tv_nsec / 1000ULL;
}

static inline void sceKernelExitProcess(int status)
{
    exit(status);
}

#ifdef __cplusplus
}
#endif

#endif // _PSP2_KERNEL_PROCESSMGR_H_
