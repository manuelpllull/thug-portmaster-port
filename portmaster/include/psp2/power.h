#ifndef _PSP2_POWER_H_
#define _PSP2_POWER_H_

#ifdef __cplusplus
extern "C" {
#endif

static inline int scePowerSetArmClockFrequency(int freq) { return 0; }
static inline int scePowerSetBusClockFrequency(int freq) { return 0; }
static inline int scePowerSetGpuClockFrequency(int freq) { return 0; }
static inline int scePowerSetGpuXbarClockFrequency(int freq) { return 0; }

static inline int scePowerGetArmClockFrequency(void) { return 1300; }
static inline int scePowerGetBusClockFrequency(void) { return 667; }
static inline int scePowerGetGpuClockFrequency(void) { return 520; }
static inline int scePowerGetGpuXbarClockFrequency(void) { return 520; }

#ifdef __cplusplus
}
#endif

#endif // _PSP2_POWER_H_
