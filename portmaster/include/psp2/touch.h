#ifndef _PSP2_TOUCH_H_
#define _PSP2_TOUCH_H_

#include <psp2/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceTouchReport {
    uint8_t id;
    uint8_t force;
    int16_t x;
    int16_t y;
    uint8_t reserved[8];
    uint16_t info;
} SceTouchReport;

typedef struct SceTouchData {
    uint64_t timeStamp;
    uint32_t status;
    uint32_t reportNum;
    SceTouchReport report[8];
} SceTouchData;

typedef struct SceTouchPanelInfo {
    int16_t minAaX;
    int16_t minAaY;
    int16_t maxAaX;
    int16_t maxAaY;
    int16_t minDispX;
    int16_t minDispY;
    int16_t maxDispX;
    int16_t maxDispY;
    uint8_t minForce;
    uint8_t maxForce;
    uint8_t reserved[30];
} SceTouchPanelInfo;

#define SCE_TOUCH_PORT_FRONT 0
#define SCE_TOUCH_PORT_BACK 1
#define SCE_TOUCH_SAMPLING_STATE_STOP 0
#define SCE_TOUCH_SAMPLING_STATE_START 1

static inline int sceTouchSetSamplingState(int port, int state) { return 0; }
static inline int sceTouchGetPanelInfo(int port, SceTouchPanelInfo *pPanelInfo) { return -1; }
static inline int sceTouchPeek(int port, SceTouchData *pData, uint32_t nBufs) { return 0; }

#ifdef __cplusplus
}
#endif

#endif // _PSP2_TOUCH_H_
