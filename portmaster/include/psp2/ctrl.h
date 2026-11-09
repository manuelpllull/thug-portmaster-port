#ifndef _PSP2_CTRL_H_
#define _PSP2_CTRL_H_

#include <psp2/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum SceCtrlButtons {
    SCE_CTRL_SELECT      = 0x00000001,
    SCE_CTRL_L3          = 0x00000002,
    SCE_CTRL_R3          = 0x00000004,
    SCE_CTRL_START       = 0x00000008,
    SCE_CTRL_UP          = 0x00000010,
    SCE_CTRL_RIGHT       = 0x00000020,
    SCE_CTRL_DOWN        = 0x00000040,
    SCE_CTRL_LEFT        = 0x00000080,
    SCE_CTRL_LTRIGGER    = 0x00000100, // L2
    SCE_CTRL_RTRIGGER    = 0x00000200, // R2
    SCE_CTRL_L1          = 0x00000400,
    SCE_CTRL_R1          = 0x00000800,
    SCE_CTRL_TRIANGLE    = 0x00001000,
    SCE_CTRL_CIRCLE      = 0x00002000,
    SCE_CTRL_CROSS       = 0x00004000,
    SCE_CTRL_SQUARE      = 0x00008000
};

#define SCE_CTRL_L2 SCE_CTRL_LTRIGGER
#define SCE_CTRL_R2 SCE_CTRL_RTRIGGER

#define SCE_CTRL_MODE_DIGITAL      0
#define SCE_CTRL_MODE_ANALOG       1
#define SCE_CTRL_MODE_ANALOG_WIDE  2

typedef struct SceCtrlData {
    uint64_t timeStamp;
    unsigned int buttons;
    unsigned char lx;
    unsigned char ly;
    unsigned char rx;
    unsigned char ry;
    unsigned char up;
    unsigned char right;
    unsigned char down;
    unsigned char left;
    unsigned char lt;
    unsigned char rt;
    unsigned char l1;
    unsigned char r1;
    unsigned char triangle;
    unsigned char circle;
    unsigned char cross;
    unsigned char square;
    unsigned char reserved[16];
} SceCtrlData;

int sceCtrlSetSamplingMode(int mode);
int sceCtrlSetSamplingModeExt(int mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count);
int sceCtrlPeekBufferPositiveExt2(int port, SceCtrlData *pad_data, int count);

#ifdef __cplusplus
}
#endif

#endif // _PSP2_CTRL_H_
