// Render thread for the R36S (THUG_RENDER_THREAD=0 turns it off).
//
// Device profile (warmup-run1): the game thread spent ~50 % of its samples in
// the Mali driver and ~6 % more in libc called by it; the game and gl4es
// themselves ~32 %. Every real GLES call of the game thread (gl4es' and the
// direct GLES path's, all obtained through thug_glrec_getproc) is recorded
// into command buffers and replayed by a render thread that owns the GL
// context, so the driver's CPU time runs on another core while the game
// thread prepares the next frame. Nothing about what is drawn changes.
//
//  - void calls with scalar arguments, and the uploads/uniform arrays whose
//    data size is known, are recorded (data copied) and return at once;
//  - calls that return something or read/write through pointers wait for the
//    render thread and run there (load time, or rare);
//  - glGetError answers GL_NO_ERROR without waiting;
//  - vertex data in client memory (gl4es' immediate-mode/2D paths) is copied
//    at the draw, for the index range the draw uses;
//  - the end of a frame is a recorded "present"; the game thread runs at most
//    one frame ahead of the render thread.
// C++98, pthreads. Pointers are 32 bits (armhf, and the -m32 x86 harness).

#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "portmaster_glrec.h"

void thug_present_submit_rt(float cpu_ms, int mirror);    // portmaster_present.cpp

#define GL_APIENTRY
typedef void GLvoid;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef int GLfixed;
typedef char GLchar;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef unsigned short GLhalf;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;
typedef void (*GLDEBUGPROC)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void *);

typedef char glrec_ptr_is_32bit[sizeof(void *) == 4 ? 1 : -1];

namespace {

// ---- command buffers ---------------------------------------------------------
const int kBufs = 4;
const uint32_t kWords = 2u * 1024u * 1024u;     // 8 MB per buffer
const uint32_t kMaxCmd = 1u << 20;               // header holds 20 bits of length

struct Buf { uint32_t *w; uint32_t used; };
Buf s_buf[kBufs];
int s_ready[kBufs];
int s_nready = 0;
int s_free[kBufs];
int s_nfree = 0;
int s_cur = -1;
uint32_t s_pending = 0;

pthread_t s_thread;
pthread_mutex_t s_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_cv_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t s_cv_free = PTHREAD_COND_INITIALIZER;
pthread_cond_t s_cv_done = PTHREAD_COND_INITIALIZER;
volatile bool s_deferred = false;
bool s_stop = false;
uint32_t s_sync_seq = 0, s_sync_done = 0;
uint32_t s_frames_rec = 0, s_frames_done = 0;

// statistics (game thread: waits; render thread: busy)
uint64_t s_wait_buf_us = 0, s_wait_sync_us = 0, s_wait_frame_us = 0, s_busy_us = 0;
uint32_t s_syncs = 0, s_sync_draws = 0, s_bytes = 0;

uint64_t now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

inline bool glrec_deferred() { return s_deferred; }

void submit_cur_locked()
{
    if (s_cur >= 0 && s_buf[s_cur].used) {
        s_ready[s_nready++] = s_cur;
        s_cur = -1;
        pthread_cond_signal(&s_cv_ready);
    }
}

void take_free_locked()
{
    if (s_cur >= 0)
        return;
    const uint64_t t0 = now_us();
    while (s_nfree == 0)
        pthread_cond_wait(&s_cv_free, &s_m);
    s_wait_buf_us += now_us() - t0;
    s_cur = s_free[--s_nfree];
    s_buf[s_cur].used = 0;
}

uint32_t *glrec_alloc(uint32_t words, uint32_t payload_bytes)
{
    const uint32_t total = words + (payload_bytes + 3) / 4;
    if (total >= kMaxCmd)
        return NULL;    // caller runs it synchronously instead
    if (s_cur < 0 || s_buf[s_cur].used + total > kWords) {
        pthread_mutex_lock(&s_m);
        if (s_cur >= 0)
            submit_cur_locked();
        take_free_locked();
        pthread_mutex_unlock(&s_m);
    }
    s_pending = total;
    s_bytes += total * 4;
    return s_buf[s_cur].w + s_buf[s_cur].used;
}

void glrec_commit(uint32_t *c, uint32_t op)
{
    c[0] = op | (s_pending << 12);
    s_buf[s_cur].used += s_pending;
}

void glrec_flush()
{
    pthread_mutex_lock(&s_m);
    submit_cur_locked();
    pthread_mutex_unlock(&s_m);
}

void glrec_bad_op(uint32_t op)
{
    fprintf(stderr, "[GLREC] bad op %u\n", op);
    abort();
}

enum {
    OP_SYNC = 4000, OP_PRESENT, OP_VAP, OP_CLIENT_ATTRIB, OP_DRAW_ELEM_OFF, OP_DRAW_ELEM_CLIENT,
    OP_DRAW_ARRAYS
};

// which functions wait, for the periodic report
const char *s_sync_names[16];
uint32_t s_sync_counts[16];

void count_sync(const char *name)
{
    for (int i = 0; i < 16; ++i) {
        if (s_sync_names[i] == name) { ++s_sync_counts[i]; return; }
        if (!s_sync_names[i]) { s_sync_names[i] = name; s_sync_counts[i] = 1; return; }
    }
}

void glrec_sync(void (*fn)(void *), void *arg, const char *name = "draw")
{
    count_sync(name);
    uint32_t *c = glrec_alloc(4, 0);
    c[1] = (uint32_t)(uintptr_t)fn;
    c[2] = (uint32_t)(uintptr_t)arg;
    const uint32_t seq = ++s_sync_seq;
    c[3] = seq;
    glrec_commit(c, OP_SYNC);
    ++s_syncs;
    const uint64_t t0 = now_us();
    pthread_mutex_lock(&s_m);
    submit_cur_locked();
    while (s_sync_done < seq)
        pthread_cond_wait(&s_cv_done, &s_m);
    pthread_mutex_unlock(&s_m);
    s_wait_sync_us += now_us() - t0;
}

// ---- unpack state for texture data sizes ---------------------------------------
int s_unpack_align = 4, s_unpack_row = 0, s_unpack_skip = 0, s_unpack_height = 0;

uint32_t type_comps_bytes(GLenum format, GLenum type)
{
    int comps = 4;
    switch (format) {
    case 0x1906: case 0x1909: case 0x1903: case 0x8D94: case 0x1902: comps = 1; break;   // A, L, R, R_INT, DEPTH
    case 0x190A: case 0x8227: case 0x8228: case 0x84F9: comps = 2; break;                 // LA, RG, RG_INT, DEPTH_STENCIL
    case 0x1907: case 0x8D98: comps = 3; break;                                           // RGB, RGB_INT
    default: comps = 4; break;
    }
    switch (type) {
    case 0x1400: case 0x1401: return comps;                  // (U)BYTE
    case 0x1402: case 0x1403: case 0x140B: case 0x8D61: return comps * 2;   // (U)SHORT, HALF
    case 0x1404: case 0x1405: case 0x1406: return comps * 4; // (U)INT, FLOAT
    case 0x8363: case 0x8033: case 0x8034: return 2;         // 565, 4444, 5551
    case 0x84FA: case 0x8368: case 0x8C3B: case 0x8C3E: return 4;
    case 0x8DAD: return 8;
    default: return comps * 4;
    }
}

}   // namespace

static uint32_t glrec_tex_bytes(GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type)
{
    if (w <= 0 || h <= 0 || d <= 0)
        return 0;
    if (s_unpack_skip)
        return kMaxCmd * 4;     // unusual unpack state: run synchronously
    const uint32_t bpp = type_comps_bytes(format, type);
    const uint32_t rowpix = s_unpack_row ? (uint32_t)s_unpack_row : (uint32_t)w;
    uint32_t row = rowpix * bpp;
    const uint32_t a = (uint32_t)s_unpack_align;
    row = (row + a - 1) / a * a;
    const uint32_t img_rows = s_unpack_height ? (uint32_t)s_unpack_height : (uint32_t)h;
    return row * img_rows * (uint32_t)(d - 1) + row * (uint32_t)(h - 1) + (uint32_t)w * bpp;
}

// custom stubs (defined below, referenced by the generated table)
static void GL_APIENTRY glrec_glBindBuffer(GLenum target, GLuint buffer);
static void GL_APIENTRY glrec_glBindVertexArray(GLuint array);
static void GL_APIENTRY glrec_glPixelStorei(GLenum pname, GLint param);
static void GL_APIENTRY glrec_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
static void GL_APIENTRY glrec_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
static void GL_APIENTRY glrec_glEnableVertexAttribArray(GLuint index);
static void GL_APIENTRY glrec_glDisableVertexAttribArray(GLuint index);
static void GL_APIENTRY glrec_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
static void GL_APIENTRY glrec_glDrawArrays(GLenum mode, GLint first, GLsizei count);
static GLenum GL_APIENTRY glrec_glGetError(void);
static void GL_APIENTRY glrec_glDeleteBuffers(GLsizei n, const GLuint *buffers);
static void GL_APIENTRY glrec_glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
static void GL_APIENTRY glrec_glGenBuffers(GLsizei n, GLuint *buffers);
static void GL_APIENTRY glrec_glGenTextures(GLsizei n, GLuint *textures);
static void GL_APIENTRY glrec_glGenVertexArrays(GLsizei n, GLuint *arrays);
static void GL_APIENTRY glrec_glEnable(GLenum cap);
static void GL_APIENTRY glrec_glDisable(GLenum cap);
static GLboolean GL_APIENTRY glrec_glIsEnabled(GLenum cap);

#include "portmaster_glrec_gen.inc"

// ---- shadow binding state (game thread) -------------------------------------
namespace {
struct Attr {
    bool enabled, integer;
    GLint size;
    GLenum type;
    GLboolean norm;
    GLsizei stride;
    const void *ptr;
    GLuint buf;
};
const int kAttrs = 16;
Attr s_attr[kAttrs];          // default VAO only (client arrays exist only there)
GLuint s_array_buf = 0, s_vao = 0, s_elem0 = 0;
std::vector<GLuint> s_vao_elem;

GLuint &elem_binding()
{
    if (s_vao == 0)
        return s_elem0;
    if (s_vao >= s_vao_elem.size())
        s_vao_elem.resize(s_vao + 64, 0);
    return s_vao_elem[s_vao];
}

uint32_t type_size(GLenum type)
{
    switch (type) {
    case 0x1400: case 0x1401: return 1;
    case 0x1402: case 0x1403: case 0x140B: case 0x8D61: return 2;
    default: return 4;
    }
}

bool client_attribs()
{
    if (s_vao != 0)
        return false;
    for (int i = 0; i < kAttrs; ++i)
        if (s_attr[i].enabled && s_attr[i].buf == 0 && s_attr[i].ptr)
            return true;
    return false;
}

// Copy the client vertex data of [lo, hi] for every enabled client attribute.
bool emit_client_attribs(uint32_t lo, uint32_t hi)
{
    for (int i = 0; i < kAttrs; ++i) {
        const Attr &a = s_attr[i];
        if (!a.enabled || a.buf != 0 || !a.ptr)
            continue;
        const uint32_t elem = (uint32_t)a.size * type_size(a.type);
        const uint32_t stride = a.stride ? (uint32_t)a.stride : elem;
        const uint32_t bytes = (hi - lo) * stride + elem;
        uint32_t *c = glrec_alloc(9, bytes);
        if (!c)
            return false;
        c[1] = (uint32_t)i;
        c[2] = (uint32_t)a.size;
        c[3] = a.type;
        c[4] = (uint32_t)a.norm | (a.integer ? 2u : 0u);
        c[5] = (uint32_t)a.stride;
        c[6] = lo * stride;    // replay pointer = data - lo * stride
        c[7] = bytes;
        c[8] = s_array_buf;    // array buffer to restore after pointing at client data
        memcpy(c + 9, (const char *)a.ptr + lo * stride, bytes);
        glrec_commit(c, OP_CLIENT_ATTRIB);
    }
    return true;
}

// A draw that cannot be recorded (client vertex data with buffered indices):
// run it on the render thread with the original pointers, caller waiting.
struct DrawSync { GLenum mode; GLsizei count; GLenum type; const void *indices; GLint first; bool elements; };
void exec_draw_sync(void *p)
{
    const DrawSync *d = static_cast<const DrawSync *>(p);
    for (int i = 0; i < kAttrs; ++i) {
        const Attr &a = s_attr[i];
        if (!a.enabled || a.buf != 0 || !a.ptr)
            continue;
        glrec_real_glBindBuffer(0x8892, 0);
        if (a.integer)
            glrec_real_glVertexAttribIPointer(i, a.size, a.type, a.stride, a.ptr);
        else
            glrec_real_glVertexAttribPointer(i, a.size, a.type, a.norm, a.stride, a.ptr);
    }
    glrec_real_glBindBuffer(0x8892, s_array_buf);
    if (d->elements)
        glrec_real_glDrawElements(d->mode, d->count, d->type, d->indices);
    else
        glrec_real_glDrawArrays(d->mode, d->first, d->count);
}
}   // namespace

static void GL_APIENTRY glrec_glBindBuffer(GLenum target, GLuint buffer)
{
    if (target == 0x8892)
        s_array_buf = buffer;
    else if (target == 0x8893)
        elem_binding() = buffer;
    if (!glrec_deferred()) { glrec_real_glBindBuffer(target, buffer); return; }
    glrec_async_glBindBuffer(target, buffer);
}

static void GL_APIENTRY glrec_glBindVertexArray(GLuint array)
{
    s_vao = array;
    if (!glrec_deferred()) { glrec_real_glBindVertexArray(array); return; }
    glrec_async_glBindVertexArray(array);
}

static void GL_APIENTRY glrec_glPixelStorei(GLenum pname, GLint param)
{
    if (pname == 0x0CF5) s_unpack_align = param;
    else if (pname == 0x0CF2) s_unpack_row = param;
    else if (pname == 0x806E) s_unpack_height = param;
    else if (pname == 0x0CF3 || pname == 0x0CF4 || pname == 0x806D) s_unpack_skip = param;
    if (!glrec_deferred()) { glrec_real_glPixelStorei(pname, param); return; }
    glrec_async_glPixelStorei(pname, param);
}

static void set_attr(GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride,
                     const void *pointer, bool integer)
{
    if (s_vao == 0 && index < (GLuint)kAttrs) {
        Attr &a = s_attr[index];
        a.size = size; a.type = type; a.norm = norm; a.stride = stride;
        a.ptr = pointer; a.buf = s_array_buf; a.integer = integer;
    }
}

static void GL_APIENTRY glrec_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    set_attr(index, size, type, normalized, stride, pointer, false);
    if (!glrec_deferred()) { glrec_real_glVertexAttribPointer(index, size, type, normalized, stride, pointer); return; }
    if (s_array_buf == 0 && s_vao == 0)
        return;     // client data: copied at the draw
    uint32_t *c = glrec_alloc(7, 0);
    c[1] = index; c[2] = (uint32_t)size; c[3] = type; c[4] = normalized;
    c[5] = (uint32_t)stride; c[6] = (uint32_t)(uintptr_t)pointer;
    glrec_commit(c, OP_VAP);
}

static void GL_APIENTRY glrec_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    set_attr(index, size, type, 0, stride, pointer, true);
    if (!glrec_deferred()) { glrec_real_glVertexAttribIPointer(index, size, type, stride, pointer); return; }
    if (s_array_buf == 0 && s_vao == 0)
        return;
    uint32_t *c = glrec_alloc(7, 0);
    c[1] = index | 0x80000000u; c[2] = (uint32_t)size; c[3] = type; c[4] = 0;
    c[5] = (uint32_t)stride; c[6] = (uint32_t)(uintptr_t)pointer;
    glrec_commit(c, OP_VAP);
}

static void GL_APIENTRY glrec_glEnableVertexAttribArray(GLuint index)
{
    if (s_vao == 0 && index < (GLuint)kAttrs)
        s_attr[index].enabled = true;
    if (!glrec_deferred()) { glrec_real_glEnableVertexAttribArray(index); return; }
    glrec_async_glEnableVertexAttribArray(index);
}

static void GL_APIENTRY glrec_glDisableVertexAttribArray(GLuint index)
{
    if (s_vao == 0 && index < (GLuint)kAttrs)
        s_attr[index].enabled = false;
    if (!glrec_deferred()) { glrec_real_glDisableVertexAttribArray(index); return; }
    glrec_async_glDisableVertexAttribArray(index);
}

static void GL_APIENTRY glrec_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (!glrec_deferred()) { glrec_real_glDrawArrays(mode, first, count); return; }
    if (count <= 0)
        return;
    if (client_attribs() && !emit_client_attribs((uint32_t)first, (uint32_t)(first + count - 1))) {
        DrawSync d = { mode, count, 0, NULL, first, false };
        ++s_sync_draws;
        glrec_sync(exec_draw_sync, &d);
        return;
    }
    uint32_t *c = glrec_alloc(4, 0);
    c[1] = mode; c[2] = (uint32_t)first; c[3] = (uint32_t)count;
    glrec_commit(c, OP_DRAW_ARRAYS);
}

static void GL_APIENTRY glrec_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if (!glrec_deferred()) { glrec_real_glDrawElements(mode, count, type, indices); return; }
    if (count <= 0)
        return;
    const bool client = client_attribs();
    if (elem_binding() != 0) {
        if (client) {    // buffered indices: index range unknown here
            DrawSync d = { mode, count, type, indices, 0, true };
            ++s_sync_draws;
            glrec_sync(exec_draw_sync, &d);
            return;
        }
        uint32_t *c = glrec_alloc(5, 0);
        c[1] = mode; c[2] = (uint32_t)count; c[3] = type; c[4] = (uint32_t)(uintptr_t)indices;
        glrec_commit(c, OP_DRAW_ELEM_OFF);
        return;
    }
    // client indices
    const uint32_t isz = type == 0x1401 ? 1 : type == 0x1403 ? 2 : 4;
    uint32_t lo = 0xffffffffu, hi = 0;
    for (GLsizei i = 0; i < count; ++i) {
        const uint32_t v = isz == 1 ? ((const uint8_t *)indices)[i]
                         : isz == 2 ? ((const uint16_t *)indices)[i] : ((const uint32_t *)indices)[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (client && !emit_client_attribs(lo, hi)) {
        DrawSync d = { mode, count, type, indices, 0, true };
        ++s_sync_draws;
        glrec_sync(exec_draw_sync, &d);
        return;
    }
    const uint32_t bytes = (uint32_t)count * isz;
    uint32_t *c = glrec_alloc(5, bytes);
    if (!c) {
        DrawSync d = { mode, count, type, indices, 0, true };
        glrec_sync(exec_draw_sync, &d);
        return;
    }
    c[1] = mode; c[2] = (uint32_t)count; c[3] = type; c[4] = bytes;
    memcpy(c + 5, indices, bytes);
    glrec_commit(c, OP_DRAW_ELEM_CLIENT);
}

static GLenum GL_APIENTRY glrec_glGetError(void)
{
    if (!glrec_deferred())
        return glrec_real_glGetError();
    return 0;   // errors are not reported back while recording
}

static void GL_APIENTRY glrec_glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    for (GLsizei i = 0; i < n; ++i) {
        if (buffers[i] == s_array_buf) s_array_buf = 0;
        if (buffers[i] == s_elem0) s_elem0 = 0;
        for (int k = 0; k < kAttrs; ++k)
            if (s_attr[k].buf == buffers[i]) { s_attr[k].buf = 0; s_attr[k].ptr = NULL; }
    }
    if (!glrec_deferred()) { glrec_real_glDeleteBuffers(n, buffers); return; }
    glrec_async_glDeleteBuffers(n, buffers);
}

static void GL_APIENTRY glrec_glDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    for (GLsizei i = 0; i < n; ++i) {
        if (arrays[i] == s_vao) s_vao = 0;
        if (arrays[i] < s_vao_elem.size()) s_vao_elem[arrays[i]] = 0;
    }
    if (!glrec_deferred()) { glrec_real_glDeleteVertexArrays(n, arrays); return; }
    glrec_async_glDeleteVertexArrays(n, arrays);
}

// Object names: generated 256 at a time on the render thread, handed out here
// without waiting (loading created ~30,000 buffers, one wait each).
namespace {
struct NamePool { GLuint names[256]; int n; };
NamePool s_pool_buf, s_pool_tex, s_pool_vao;
void take_names(NamePool &pool, GLsizei n, GLuint *out, void (*gen)(GLsizei, GLuint *))
{
    for (GLsizei i = 0; i < n; ++i) {
        if (pool.n == 0) {
            gen(256, pool.names);
            pool.n = 256;
        }
        out[i] = pool.names[--pool.n];
    }
}
void gen_buf(GLsizei n, GLuint *o) { glrec_syncdirect_glGenBuffers(n, o); }
void gen_tex(GLsizei n, GLuint *o) { glrec_syncdirect_glGenTextures(n, o); }
void gen_vao(GLsizei n, GLuint *o) { glrec_syncdirect_glGenVertexArrays(n, o); }
}   // namespace

static void GL_APIENTRY glrec_glGenBuffers(GLsizei n, GLuint *buffers)
{
    if (!glrec_deferred()) { glrec_real_glGenBuffers(n, buffers); return; }
    take_names(s_pool_buf, n, buffers, gen_buf);
}

static void GL_APIENTRY glrec_glGenTextures(GLsizei n, GLuint *textures)
{
    if (!glrec_deferred()) { glrec_real_glGenTextures(n, textures); return; }
    take_names(s_pool_tex, n, textures, gen_tex);
}

static void GL_APIENTRY glrec_glGenVertexArrays(GLsizei n, GLuint *arrays)
{
    if (!glrec_deferred()) { glrec_real_glGenVertexArrays(n, arrays); return; }
    take_names(s_pool_vao, n, arrays, gen_vao);
}

// Enable state: answered here (3 glIsEnabled per frame each drained the queue).
// A capability's first query asks the driver once; glEnable/glDisable keep it.
namespace {
struct CapState { GLenum cap; signed char on; };    // on: -1 unknown
CapState s_caps[48];
int s_ncaps = 0;
CapState *cap_slot(GLenum cap)
{
    for (int i = 0; i < s_ncaps; ++i)
        if (s_caps[i].cap == cap)
            return &s_caps[i];
    if (s_ncaps == (int)(sizeof(s_caps) / sizeof(s_caps[0])))
        return NULL;
    s_caps[s_ncaps].cap = cap;
    s_caps[s_ncaps].on = -1;
    return &s_caps[s_ncaps++];
}
}   // namespace

static void GL_APIENTRY glrec_glEnable(GLenum cap)
{
    if (CapState *c = cap_slot(cap)) c->on = 1;
    if (!glrec_deferred()) { glrec_real_glEnable(cap); return; }
    glrec_async_glEnable(cap);
}

static void GL_APIENTRY glrec_glDisable(GLenum cap)
{
    if (CapState *c = cap_slot(cap)) c->on = 0;
    if (!glrec_deferred()) { glrec_real_glDisable(cap); return; }
    glrec_async_glDisable(cap);
}

static GLboolean GL_APIENTRY glrec_glIsEnabled(GLenum cap)
{
    CapState *c = cap_slot(cap);
    if (c && c->on >= 0)
        return (GLboolean)c->on;
    const GLboolean v = glrec_deferred() ? glrec_syncdirect_glIsEnabled(cap) : glrec_real_glIsEnabled(cap);
    if (c) c->on = v ? 1 : 0;
    return v;
}

// ---- render thread -------------------------------------------------------------
namespace {

void replay(const Buf &b)
{
    const uint32_t *p = b.w, *end = b.w + b.used;
    while (p < end) {
        const uint32_t op = p[0] & 0xfffu, n = p[0] >> 12;
        switch (op) {
        case OP_SYNC: {
            void (*fn)(void *) = (void (*)(void *))(uintptr_t)p[1];
            fn((void *)(uintptr_t)p[2]);
            pthread_mutex_lock(&s_m);
            s_sync_done = p[3];
            pthread_cond_broadcast(&s_cv_done);
            pthread_mutex_unlock(&s_m);
            break;
        }
        case OP_PRESENT: {
            float ms;
            memcpy(&ms, p + 1, 4);
            thug_present_submit_rt(ms, (int)p[2]);
            pthread_mutex_lock(&s_m);
            ++s_frames_done;
            pthread_cond_broadcast(&s_cv_done);
            pthread_mutex_unlock(&s_m);
            break;
        }
        case OP_VAP:
            if (p[1] & 0x80000000u)
                glrec_real_glVertexAttribIPointer(p[1] & 0xffffu, (GLint)p[2], p[3], (GLsizei)p[5],
                                                  (const void *)(uintptr_t)p[6]);
            else
                glrec_real_glVertexAttribPointer(p[1], (GLint)p[2], p[3], (GLboolean)p[4], (GLsizei)p[5],
                                                 (const void *)(uintptr_t)p[6]);
            break;
        case OP_CLIENT_ATTRIB: {
            // client data: array buffer 0 while pointing, then the game's binding
            if (p[8])
                glrec_real_glBindBuffer(0x8892, 0);
            const char *data = (const char *)(p + 9) - p[6];
            if (p[4] & 2u)
                glrec_real_glVertexAttribIPointer(p[1], (GLint)p[2], p[3], (GLsizei)p[5], data);
            else
                glrec_real_glVertexAttribPointer(p[1], (GLint)p[2], p[3], (GLboolean)(p[4] & 1u),
                                                 (GLsizei)p[5], data);
            if (p[8])
                glrec_real_glBindBuffer(0x8892, p[8]);
            break;
        }
        case OP_DRAW_ARRAYS:
            glrec_real_glDrawArrays(p[1], (GLint)p[2], (GLsizei)p[3]);
            break;
        case OP_DRAW_ELEM_OFF:
            glrec_real_glDrawElements(p[1], (GLsizei)p[2], p[3], (const void *)(uintptr_t)p[4]);
            break;
        case OP_DRAW_ELEM_CLIENT:    // element binding is 0 here (recorded state)
            glrec_real_glDrawElements(p[1], (GLsizei)p[2], p[3], p + 5);
            break;
        default:
            glrec_replay_generated(p, op);
            break;
        }
        p += n;
    }
}

typedef void *(*PfnCur)(void);
typedef unsigned (*PfnMakeCurrent)(void *, void *, void *, void *);
void *s_dpy = NULL, *s_ctx = NULL;
PfnMakeCurrent s_make_current = NULL;
bool s_rt_ready = false, s_rt_failed = false;

void *thread_main(void *)
{
    pthread_setname_np(pthread_self(), "thug-render");    // named in [CRASH] reports
    const bool ok = s_make_current(s_dpy, NULL, NULL, s_ctx) == 1;
    pthread_mutex_lock(&s_m);
    if (ok) s_rt_ready = true; else s_rt_failed = true;
    pthread_cond_broadcast(&s_cv_done);
    pthread_mutex_unlock(&s_m);
    if (!ok)
        return NULL;
    for (;;) {
        pthread_mutex_lock(&s_m);
        while (!s_stop && s_nready == 0)
            pthread_cond_wait(&s_cv_ready, &s_m);
        if (s_nready == 0) {
            pthread_mutex_unlock(&s_m);
            break;
        }
        const int b = s_ready[0];
        for (int i = 1; i < s_nready; ++i)
            s_ready[i - 1] = s_ready[i];
        --s_nready;
        pthread_mutex_unlock(&s_m);
        const uint64_t t0 = now_us();
        replay(s_buf[b]);
        const uint64_t t1 = now_us();
        pthread_mutex_lock(&s_m);
        s_busy_us += t1 - t0;
        s_free[s_nfree++] = b;
        pthread_cond_broadcast(&s_cv_free);
        pthread_mutex_unlock(&s_m);
    }
    s_make_current(s_dpy, NULL, NULL, NULL);
    return NULL;
}

void *look(const char *name)
{
    if (void *p = SDL_GL_GetProcAddress(name))
        return p;
    // The full paths cover multiarch systems where the bare names are the other arch's (Knulli).
    const char *libs[] = { getenv("SDL_VIDEO_GL_DRIVER"), getenv("SDL_VIDEO_EGL_DRIVER"),
                           "libGLESv2.so.2", "libGLESv2.so", "libEGL.so.1", "libEGL.so" };
    for (const char *l : libs)
        if (l)
            if (void *h = dlopen(l, RTLD_LAZY | RTLD_NOLOAD))
                if (void *p = dlsym(h, name))
                    return p;
    return NULL;
}

void trap_unwrapped()
{
    fprintf(stderr, "[GLREC] FATAL: an unwrapped GL function was called with the render thread on\n");
    abort();
}

void stop_at_exit()
{
    if (!s_deferred)
        return;
    // drain: everything recorded so far is executed before the process ends
    glrec_syncdirect_glFlush();
}

}   // namespace

extern "C" void *thug_glrec_getproc(const char *name)
{
    void *real = look(name);
    for (int i = 0; glrec_table[i].name; ++i)
        if (strcmp(glrec_table[i].name, name) == 0) {
            if (!*glrec_table[i].real)
                *glrec_table[i].real = real;
            return real ? glrec_table[i].stub : NULL;
        }
    // OES/EXT aliases of core entry points
    char core[96];
    const size_t l = strlen(name);
    if (l > 3 && l < sizeof(core)) {
        const char *suffixes[] = { "OES", "EXT", "KHR", NULL };
        for (int s = 0; suffixes[s]; ++s) {
            const size_t sl = strlen(suffixes[s]);
            if (l > sl && strcmp(name + l - sl, suffixes[s]) == 0) {
                memcpy(core, name, l - sl);
                core[l - sl] = 0;
                for (int i = 0; glrec_table[i].name; ++i)
                    if (strcmp(glrec_table[i].name, core) == 0) {
                        if (!*glrec_table[i].real)
                            *glrec_table[i].real = real ? real : look(core);
                        return *glrec_table[i].real ? glrec_table[i].stub : NULL;
                    }
            }
        }
    }
    if (real)
        printf("[GLREC] unwrapped GL function %s%s\n", name,
               s_deferred ? " (render thread on: calls abort)" : "");
    return s_deferred && real ? (void *)trap_unwrapped : real;
}

// Quitting (game thread): the render thread runs every finished recording,
// leaves the context and ends. A recording still in progress is dropped.
void thug_glrec_stop()
{
    if (!s_deferred)
        return;
    pthread_mutex_lock(&s_m);
    s_stop = true;
    pthread_cond_broadcast(&s_cv_ready);
    pthread_mutex_unlock(&s_m);
    pthread_join(s_thread, NULL);
    s_deferred = false;    // stop_at_exit has nothing left to drain
}

bool thug_glrec_start()
{
    const char *e = getenv("THUG_RENDER_THREAD");
    if (e && strcmp(e, "0") == 0) {
        printf("[GLREC] render thread off (THUG_RENDER_THREAD=0)\n");
        return false;
    }
    PfnCur get_dpy = reinterpret_cast<PfnCur>(look("eglGetCurrentDisplay"));
    PfnCur get_ctx = reinterpret_cast<PfnCur>(look("eglGetCurrentContext"));
    s_make_current = reinterpret_cast<PfnMakeCurrent>(look("eglMakeCurrent"));
    // gl4es looks functions up lazily; replay paths may call ones it has not
    // asked for yet (client data, sync draws): resolve every real one now.
    for (int i = 0; glrec_table[i].name; ++i)
        if (!*glrec_table[i].real)
            *glrec_table[i].real = look(glrec_table[i].name);
    if (!get_dpy || !get_ctx || !s_make_current || !glrec_real_glFlush) {
        printf("[GLREC] render thread not used: EGL entry points missing\n");
        return false;
    }
    s_dpy = get_dpy();
    s_ctx = get_ctx();
    if (!s_dpy || !s_ctx) {
        printf("[GLREC] render thread not used: no current EGL context\n");
        return false;
    }
    for (int i = 0; i < kBufs; ++i) {
        s_buf[i].w = (uint32_t *)malloc(kWords * 4);
        if (!s_buf[i].w) {
            printf("[GLREC] render thread not used: out of memory\n");
            return false;
        }
        s_buf[i].used = 0;
        s_free[s_nfree++] = i;
    }
    glrec_real_glFlush();
    if (s_make_current(s_dpy, NULL, NULL, NULL) != 1) {
        printf("[GLREC] render thread not used: cannot release the context\n");
        return false;
    }
    if (pthread_create(&s_thread, NULL, thread_main, NULL) != 0) {
        s_make_current(s_dpy, NULL, NULL, s_ctx);
        printf("[GLREC] render thread not used: pthread_create failed\n");
        return false;
    }
    pthread_mutex_lock(&s_m);
    while (!s_rt_ready && !s_rt_failed)
        pthread_cond_wait(&s_cv_done, &s_m);
    pthread_mutex_unlock(&s_m);
    if (!s_rt_ready) {
        pthread_join(s_thread, NULL);
        s_make_current(s_dpy, NULL, NULL, s_ctx);
        printf("[GLREC] render thread not used: it could not take the context\n");
        return false;
    }
    s_deferred = true;
    atexit(stop_at_exit);
    printf("[GLREC] render thread on: GL calls recorded on the game thread, run on the render thread "
           "(THUG_RENDER_THREAD=0 turns it off)\n");
    return true;
}

bool thug_glrec_active()
{
    return s_deferred;
}

void thug_glrec_present(float cpu_ms, int mirror)
{
    uint32_t *c = glrec_alloc(3, 0);
    memcpy(c + 1, &cpu_ms, 4);
    c[2] = (uint32_t)mirror;
    glrec_commit(c, OP_PRESENT);
    const uint64_t t0 = now_us();
    pthread_mutex_lock(&s_m);
    submit_cur_locked();
    ++s_frames_rec;
    while (s_frames_rec - s_frames_done > 1)
        pthread_cond_wait(&s_cv_done, &s_m);
    const uint64_t busy = s_busy_us;
    pthread_mutex_unlock(&s_m);
    s_wait_frame_us += now_us() - t0;
    if ((s_frames_rec % 300) == 0) {
        static uint64_t last_busy = 0;
        printf("[GLREC] 300 frames, ms/frame: render thread busy %.1f | game thread waited: frame %.1f, "
               "sync %.1f (%u calls, %u draws), buffer %.1f | %.0f KB recorded/frame\n",
               (busy - last_busy) / 300000.0, s_wait_frame_us / 300000.0, s_wait_sync_us / 300000.0,
               s_syncs, s_sync_draws, s_wait_buf_us / 300000.0, s_bytes / 300.0 / 1024.0);
        last_busy = busy;
        for (int i = 0; i < 16 && s_sync_names[i]; ++i)
            if (s_sync_counts[i])
                printf("[GLREC]   waited in %s: %u calls\n", s_sync_names[i], s_sync_counts[i]);
        memset(s_sync_counts, 0, sizeof(s_sync_counts));
        memset(s_sync_names, 0, sizeof(s_sync_names));
        s_wait_frame_us = s_wait_sync_us = s_wait_buf_us = 0;
        s_syncs = s_sync_draws = s_bytes = 0;
    }
}
