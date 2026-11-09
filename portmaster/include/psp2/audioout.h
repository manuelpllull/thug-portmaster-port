#ifndef _PSP2_AUDIOOUT_H_
#define _PSP2_AUDIOOUT_H_

#include <psp2/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_AUDIO_OUT_PORT_TYPE_MAIN   0
#define SCE_AUDIO_OUT_PORT_TYPE_BGM    1
#define SCE_AUDIO_OUT_PORT_TYPE_VOICE  2

#define SCE_AUDIO_OUT_MODE_MONO        0
#define SCE_AUDIO_OUT_MODE_STEREO      1

#define SCE_AUDIO_VOLUME_0DB           0x8000
#define SCE_AUDIO_VOLUME_FLAG_L_CH     1
#define SCE_AUDIO_VOLUME_FLAG_R_CH     2

typedef int SceAudioOutChannelFlag;

int sceAudioOutOpenPort(int type, int len, int freq, int mode);
int sceAudioOutClosePort(int port);
int sceAudioOutOutput(int port, const void *buf);
int sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol);
int sceAudioOutGetRestSample(int port);

#ifdef __cplusplus
}
#endif

#endif // _PSP2_AUDIOOUT_H_
