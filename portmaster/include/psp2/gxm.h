#ifndef _PSP2_GXM_H_
#define _PSP2_GXM_H_

#include <psp2/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void SceGxmContext;
typedef void SceGxmShaderPatcher;
typedef void SceGxmVertexProgram;
typedef void SceGxmFragmentProgram;
typedef void SceGxmProgram;
typedef void SceGxmProgramParameter;
typedef void SceGxmVertexAttribute;
typedef void SceGxmVertexStream;
typedef struct SceGxmTexture SceGxmTexture;
typedef uint32_t SceGxmShaderPatcherId;
typedef int SceGxmMultisampleMode;
typedef int SceGxmAttributeFormat;
typedef int SceGxmPrimitiveType;
typedef int SceGxmIndexFormat;

#define SCE_GXM_MULTISAMPLE_NONE 0
#define SCE_GXM_REGION_CLIP_OUTSIDE 0
#define SCE_GXM_PRIMITIVE_TRIANGLE_STRIP 0
#define SCE_GXM_INDEX_FORMAT_U16 0
#define SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE 4096

typedef struct SceGxmCommandList { void *dummy; } SceGxmCommandList;
typedef struct SceGxmDeferredContextParams {
    void *hostMem;
    void *callbackData;
} SceGxmDeferredContextParams;

static inline int sceGxmProgramCheck(const SceGxmProgram *program) { return -1; }
static inline int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *patcher, const SceGxmProgram *program, SceGxmShaderPatcherId *programId) { return -1; }
static inline void sceGxmSetRegionClip(SceGxmContext *context, int mode, unsigned int xMin, unsigned int yMin, unsigned int xMax, unsigned int yMax) {}
static inline unsigned int sceGxmTextureGetWidth(const SceGxmTexture *t) { return 0; }
static inline unsigned int sceGxmTextureGetHeight(const SceGxmTexture *t) { return 0; }
static inline unsigned int sceGxmTextureGetFormat(const SceGxmTexture *t) { return 0; }

#ifdef __cplusplus
}
#endif

#endif // _PSP2_GXM_H_
