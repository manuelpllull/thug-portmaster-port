#ifndef _VITAGL_H_
#define _VITAGL_H_

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>

#include <psp2/gxm.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VGL_MEM_VRAM = 0,
    VGL_MEM_RAM = 0,
    VGL_MEM_SLOW = 0,
    VGL_MEM_PHYCONT = 0
};

typedef int vglMemType;

static inline void vglSetVertexBufferSize(unsigned int s) {}
static inline void vglSetFragmentBufferSize(unsigned int s) {}
static inline void vglSetVDMBufferSize(unsigned int s) {}
static inline void vglSetParamBufferSize(unsigned int s) {}

int vglInit(int width, int height);
int vglInitExtended(int flags, int width, int height, int ram_sz, int msaa);
void vglSwapBuffers(GLboolean wait);
void vglEnd(void);
static inline int eglSwapInterval(void *dpy, int interval) { return 0; }

static inline void* vgl_memalign(size_t align, size_t size) { return memalign(align, size); }
static inline void vgl_free(void *ptr) { free(ptr); }
static inline size_t vglMemFree(int type) { return 128 * 1024 * 1024; }
static inline void vglLazyFree(void *ptr) { free(ptr); }
static inline void vglUseCachedMem(int b) {}

static inline SceGxmTexture* vglGetGxmTexture(GLenum target) { return NULL; }
static inline void* vglGetTexDataPointer(GLenum target) { return NULL; }
static inline void vglGetShaderBinary(GLuint sh, GLsizei max_size, GLsizei *length, void *buffer)
{
    if (length) *length = 0;
}
uint8_t *vgl_reserve_data_pool(uint32_t size);
static inline void vgl_fast_memcpy(void *dst, const void *src, size_t n) { memcpy(dst, src, n); }

extern unsigned int vgl_framecount;

#ifndef GL_CG_VERTEX_SHADER_EXT
#define GL_CG_VERTEX_SHADER_EXT   0x80000001
#endif
#ifndef GL_CG_FRAGMENT_SHADER_EXT
#define GL_CG_FRAGMENT_SHADER_EXT 0x80000002
#endif

#ifdef __cplusplus
}
#endif

#endif // _VITAGL_H_
