// Presenter thread for the R36S (THUG_PRESENTER=0 turns it off).
//
// Without it the game thread ends every frame in SDL_GL_SwapWindow, which on
// the Mali waits for the GPU to finish the frame and then for the next
// refresh: ~10-11 ms of idle CPU per frame in the device logs. A frame then
// costs CPU time + GPU tail + vblank alignment instead of the larger of the
// CPU and GPU times. Same design as the NFSU2/NFSMW R36S ports:
//   - the game context has no window surface (EGL_KHR_surfaceless_context)
//     and draws into one of two off-screen buffers (gl4es redirects its
//     "framebuffer 0" there, gl4es_thug_set_mainfbo);
//   - at the end of a frame the game thread puts a fence behind the frame,
//     hands the buffer to this thread and starts the next frame in the other
//     buffer;
//   - this thread owns the window with a shared context: it waits for the
//     fence, copies the buffer to the window, draws the FPS meter if enabled
//     (L3 + Select) and swaps with swap interval 1.
// Any missing piece leaves the old path in place.
//
// C++98 (the engine's dialect): pthreads, no std::thread.

#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "portmaster_present.h"
#include "portmaster_glrec.h"

extern "C" {
void gl4es_thug_set_mainfbo(unsigned int fbo, unsigned int tex, int w, int h) __attribute__((weak));
void gl4es_thug_restore(int units) __attribute__((weak));
void glFlush(void);    // gl4es: pushes anything gl4es still holds to the driver
extern unsigned g_thug_vblank_us;    // portmaster_platform.cpp: the game's time step
}

namespace {

typedef unsigned int GLuintT;
typedef int GLintT;
typedef unsigned int GLenumT;

enum {
    K_TEXTURE_2D = 0x0DE1, K_RGBA = 0x1908, K_UNSIGNED_BYTE = 0x1401, K_NEAREST = 0x2600, K_LINEAR = 0x2601,
    K_TEXTURE_MIN_FILTER = 0x2801, K_TEXTURE_MAG_FILTER = 0x2800, K_TEXTURE_WRAP_S = 0x2802,
    K_TEXTURE_WRAP_T = 0x2803, K_CLAMP_TO_EDGE = 0x812F, K_FRAMEBUFFER = 0x8D40,
    K_RENDERBUFFER = 0x8D41, K_COLOR_ATTACHMENT0 = 0x8CE0, K_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    K_DEPTH24_STENCIL8 = 0x88F0, K_FRAMEBUFFER_COMPLETE = 0x8CD5, K_RENDERBUFFER_BINDING = 0x8CA7,
    K_FRAMEBUFFER_BINDING = 0x8CA6, K_COLOR_BUFFER_BIT = 0x4000, K_DEPTH_BUFFER_BIT = 0x0100,
    K_STENCIL_BUFFER_BIT = 0x0400, K_VERTEX_SHADER = 0x8B31, K_FRAGMENT_SHADER = 0x8B30,
    K_COMPILE_STATUS = 0x8B81, K_LINK_STATUS = 0x8B82, K_FLOAT = 0x1406, K_TRIANGLE_STRIP = 0x0005,
    K_TRIANGLES = 0x0004, K_BLEND = 0x0BE2, K_SRC_ALPHA = 0x0302, K_ONE_MINUS_SRC_ALPHA = 0x0303,
    K_DEPTH_TEST = 0x0B71, K_CULL_FACE = 0x0B44, K_SCISSOR_TEST = 0x0C11, K_TEXTURE0 = 0x84C0,
    K_SYNC_GPU_COMMANDS_COMPLETE = 0x9117, K_SYNC_FLUSH_COMMANDS_BIT = 0x1, K_ARRAY_BUFFER = 0x8892
};

struct Fn {
    void (*GenTextures)(GLintT, GLuintT *);
    void (*BindTexture)(GLenumT, GLuintT);
    void (*TexImage2D)(GLenumT, GLintT, GLintT, GLintT, GLintT, GLintT, GLenumT, GLenumT, const void *);
    void (*TexParameteri)(GLenumT, GLenumT, GLintT);
    void (*GenRenderbuffers)(GLintT, GLuintT *);
    void (*BindRenderbuffer)(GLenumT, GLuintT);
    void (*RenderbufferStorage)(GLenumT, GLenumT, GLintT, GLintT);
    void (*GenFramebuffers)(GLintT, GLuintT *);
    void (*BindFramebuffer)(GLenumT, GLuintT);
    void (*FramebufferTexture2D)(GLenumT, GLenumT, GLenumT, GLuintT, GLintT);
    void (*FramebufferRenderbuffer)(GLenumT, GLenumT, GLenumT, GLuintT);
    GLenumT (*CheckFramebufferStatus)(GLenumT);
    void (*GetIntegerv)(GLenumT, GLintT *);
    void (*Clear)(GLuintT);
    void (*ClearColor)(float, float, float, float);
    void (*Viewport)(GLintT, GLintT, GLintT, GLintT);
    void (*Flush)(void);
    void *(*FenceSync)(GLenumT, GLuintT);
    GLenumT (*ClientWaitSync)(void *, GLuintT, uint64_t);
    void (*DeleteSync)(void *);
    GLuintT (*CreateShader)(GLenumT);
    void (*ShaderSource)(GLuintT, GLintT, const char *const *, const GLintT *);
    void (*CompileShader)(GLuintT);
    void (*GetShaderiv)(GLuintT, GLenumT, GLintT *);
    GLuintT (*CreateProgram)(void);
    void (*AttachShader)(GLuintT, GLuintT);
    void (*BindAttribLocation)(GLuintT, GLuintT, const char *);
    void (*LinkProgram)(GLuintT);
    void (*GetProgramiv)(GLuintT, GLenumT, GLintT *);
    GLintT (*GetUniformLocation)(GLuintT, const char *);
    void (*UseProgram)(GLuintT);
    void (*Uniform1i)(GLintT, GLintT);
    void (*Uniform4f)(GLintT, float, float, float, float);
    void (*EnableVertexAttribArray)(GLuintT);
    void (*VertexAttribPointer)(GLuintT, GLintT, GLenumT, unsigned char, GLintT, const void *);
    void (*DrawArrays)(GLenumT, GLintT, GLintT);
    void (*Enable)(GLenumT);
    void (*Disable)(GLenumT);
    void (*BlendFunc)(GLenumT, GLenumT);
    void (*ActiveTexture)(GLenumT);
    void (*BindBuffer)(GLenumT, GLuintT);
};
Fn F;

typedef void *(*PfnCur)(void);
typedef unsigned (*PfnMakeCurrent)(void *, void *, void *, void *);
typedef const char *(*PfnQueryString)(void *, int);

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

template <typename T> bool load(T &f, const char *name)
{
    f = reinterpret_cast<T>(look(name));
    return f != NULL;
}

struct Slot { GLuintT tex, rb, fbo; bool busy; };
struct Item { int slot; void *fence; int mirror; };

const int kMaxSlots = 3;
struct State {
    bool tried, on;
    int w, h, cur, cur_game;
    int dw, dh;    // screen (drawable) size; w x h has the same shape
    int nslots;    // 2; 3 with THUG_PACING_MODE=steady (one more frame of slack)
    // THUG_PACING_MODE=steady: every frame is shown for the same number of
    // refreshes (hold), instead of "at the first refresh after it is ready".
    bool steady, steady_floor;
    int refresh_hz, hold_base, hold;
    unsigned late, steady_frames;    // frames not ready when their predecessor's hold ended
    Slot slots[kMaxSlots];
    SDL_Window *window;
    SDL_GLContext ctx1, ctx2;
    pthread_t th;
    pthread_mutex_t m;
    pthread_cond_t cv_item, cv_free;
    Item items[kMaxSlots];
    int n_items;
    bool stop, ready, failed;
    // statistics (game thread writes cpu_ms; the presenter reads it)
    volatile float cpu_ms;
    volatile bool overlay;
    uint64_t wait_slot_us, wait_gpu_us, blit_swap_us, presented;
} S;

uint64_t now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

// ---- drawing on the presenter context -------------------------------------
GLuintT compile(GLenumT type, const char *src)
{
    GLuintT sh = F.CreateShader(type);
    F.ShaderSource(sh, 1, &src, NULL);
    F.CompileShader(sh);
    GLintT ok = 0;
    F.GetShaderiv(sh, K_COMPILE_STATUS, &ok);
    return ok ? sh : 0;
}

GLuintT program(const char *vs, const char *fs)
{
    const GLuintT v = compile(K_VERTEX_SHADER, vs), f = compile(K_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return 0;
    const GLuintT p = F.CreateProgram();
    F.AttachShader(p, v);
    F.AttachShader(p, f);
    F.BindAttribLocation(p, 0, "aP");
    F.LinkProgram(p);
    GLintT ok = 0;
    F.GetProgramiv(p, K_LINK_STATUS, &ok);
    return ok ? p : 0;
}

GLuintT s_blit = 0, s_solid = 0;
GLintT s_blit_tex = -1, s_blit_k = -1, s_blit_d = -1, s_solid_color = -1;

// 5x7 glyphs, one byte per row, bit 4 = leftmost column.
struct Glyph { char c; unsigned char rows[7]; };
const Glyph kFont[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { ' ', { 0, 0, 0, 0, 0, 0, 0 } },
};

float s_verts[6 * 2 * 2048];
int s_nv = 0;

void quad(float x0, float y0, float x1, float y1)
{
    if (s_nv + 6 > (int)(sizeof(s_verts) / sizeof(s_verts[0]) / 2))
        return;
    // pixels (origin top-left) -> clip space
    const float ax = x0 * 2.0f / S.dw - 1.0f, bx = x1 * 2.0f / S.dw - 1.0f;
    const float ay = 1.0f - y0 * 2.0f / S.dh, by = 1.0f - y1 * 2.0f / S.dh;
    const float v[12] = { ax, ay, bx, ay, ax, by, bx, ay, bx, by, ax, by };
    memcpy(s_verts + s_nv * 2, v, sizeof(v));
    s_nv += 6;
}

void text(const char *t, float x, float y, float px)
{
    for (; *t; ++t, x += 6 * px) {
        const Glyph *g = NULL;
        for (size_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); ++i)
            if (kFont[i].c == *t)
                g = &kFont[i];
        if (!g)
            continue;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if (g->rows[r] & (0x10 >> c))
                    quad(x + c * px, y + r * px, x + (c + 1) * px, y + (r + 1) * px);
    }
}

void draw_solid(float r, float g, float b, float a)
{
    if (!s_nv)
        return;
    F.UseProgram(s_solid);
    F.Uniform4f(s_solid_color, r, g, b, a);
    F.VertexAttribPointer(0, 2, K_FLOAT, 0, 0, s_verts);
    F.DrawArrays(K_TRIANGLES, 0, s_nv);
    s_nv = 0;
}

void draw_overlay(float fps)
{
    char l1[32], l2[32];
    snprintf(l1, sizeof(l1), "%.1f FPS", fps);
    snprintf(l2, sizeof(l2), "CPU %.1f MS", S.cpu_ms);
    // top-right corner: the game's score HUD sits top-left
    const float px = 2.0f, x = S.dw - 12 * 6 * px - 8.0f, y = 8.0f;
    F.Enable(K_BLEND);
    F.BlendFunc(K_SRC_ALPHA, K_ONE_MINUS_SRC_ALPHA);
    quad(x - 4, y - 4, x + 12 * 6 * px + 4, y + 2 * 9 * px + 2);
    draw_solid(0.0f, 0.0f, 0.0f, 0.55f);
    F.Disable(K_BLEND);
    text(l1, x, y, px);
    text(l2, x, y + 9 * px, px);
    draw_solid(1.0f, 1.0f, 0.2f, 1.0f);
}

const float kStrip[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };

// Draws one off-screen buffer onto the window's back buffer: the blit, the
// mirrored room around a 4:3 menu picture, the FPS meter.
void draw_frame(const Item &it, float fps)
{
    F.Viewport(0, 0, S.dw, S.dh);
    F.UseProgram(s_blit);
    F.BindTexture(K_TEXTURE_2D, S.slots[it.slot].tex);
    const float a = (float)S.w / (float)S.h, a43 = 4.0f / 3.0f;
    if (it.mirror && (a > a43 * 1.001f || a < a43 / 1.001f)) {
        // The 4:3 picture in the middle; the room on each side shows the
        // picture's own edge, mirrored (menus, loading screens).
        const bool wide = a > a43;
        const float e0 = wide ? 0.5f - 0.5f * a43 / a : 0.5f - 0.5f * a / a43, e1 = 1.0f - e0;
        const float part[3][2] = { { 0.0f, e0 }, { e0, e1 }, { e1, 1.0f } };
        const float mirror_at[3] = { e0, 0.0f, e1 };
        for (int k = 0; k < 3; ++k) {
            const float p0 = part[k][0] * 2.0f - 1.0f, p1 = part[k][1] * 2.0f - 1.0f;
            const float q[8] = { wide ? p0 : -1.0f, wide ? -1.0f : p0, wide ? p1 : 1.0f, wide ? -1.0f : p0,
                                 wide ? p0 : -1.0f, wide ? 1.0f : p1, wide ? p1 : 1.0f, wide ? 1.0f : p1 };
            const float sc = k == 1 ? 1.0f : -1.0f, of = k == 1 ? 0.0f : 2.0f * mirror_at[k];
            if (wide)
                F.Uniform4f(s_blit_k, sc, of, 1.0f, 0.0f);
            else
                F.Uniform4f(s_blit_k, 1.0f, 0.0f, sc, of);
            // the mirrored room at half brightness: it reads as background
            const float dim = k == 1 ? 1.0f : 0.5f;
            F.Uniform4f(s_blit_d, dim, dim, dim, 1.0f);
            F.VertexAttribPointer(0, 2, K_FLOAT, 0, 0, q);
            F.DrawArrays(K_TRIANGLE_STRIP, 0, 4);
        }
        F.Uniform4f(s_blit_k, 1.0f, 0.0f, 1.0f, 0.0f);
        F.Uniform4f(s_blit_d, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        F.VertexAttribPointer(0, 2, K_FLOAT, 0, 0, kStrip);
        F.DrawArrays(K_TRIANGLE_STRIP, 0, 4);
    }
    if (S.overlay)
        draw_overlay(fps);
}

void *thread_main(void *)
{
    pthread_setname_np(pthread_self(), "thug-present");    // named in [CRASH] reports
    if (SDL_GL_MakeCurrent(S.window, S.ctx2) != 0) {
        pthread_mutex_lock(&S.m);
        S.failed = true;
        pthread_cond_broadcast(&S.cv_free);
        pthread_mutex_unlock(&S.m);
        return NULL;
    }
    SDL_GL_SetSwapInterval(1);
    // uK maps the screen position to the picture: (1, 0, 1, 0) is a plain
    // copy; a negative scale mirrors (the room around a 4:3 menu picture).
    s_blit = program("attribute vec2 aP; varying vec2 vT; uniform vec4 uK;\n"
                     "void main() { vec2 t = aP * 0.5 + 0.5;\n"
                     "  vT = vec2(uK.x * t.x + uK.y, uK.z * t.y + uK.w);\n"
                     "  gl_Position = vec4(aP, 0.0, 1.0); }\n",
                     "precision mediump float; uniform sampler2D uT; uniform vec4 uD; varying vec2 vT;\n"
                     "void main() { gl_FragColor = texture2D(uT, vT) * uD; }\n");
    s_solid = program("attribute vec2 aP; void main() { gl_Position = vec4(aP, 0.0, 1.0); }\n",
                      "precision mediump float; uniform vec4 uC;\n"
                      "void main() { gl_FragColor = uC; }\n");
    const bool ok = s_blit && s_solid;
    if (ok) {
        s_blit_tex = F.GetUniformLocation(s_blit, "uT");
        s_blit_k = F.GetUniformLocation(s_blit, "uK");
        s_blit_d = F.GetUniformLocation(s_blit, "uD");
        s_solid_color = F.GetUniformLocation(s_solid, "uC");
        F.UseProgram(s_blit);
        F.Uniform1i(s_blit_tex, 0);
        F.Uniform4f(s_blit_k, 1.0f, 0.0f, 1.0f, 0.0f);
        F.Uniform4f(s_blit_d, 1.0f, 1.0f, 1.0f, 1.0f);
        F.Disable(K_DEPTH_TEST);
        F.Disable(K_CULL_FACE);
        F.Disable(K_SCISSOR_TEST);
        F.BindBuffer(K_ARRAY_BUFFER, 0);
        F.EnableVertexAttribArray(0);
        F.ActiveTexture(K_TEXTURE0);
    }
    pthread_mutex_lock(&S.m);
    if (ok)
        S.ready = true;
    else
        S.failed = true;
    pthread_cond_broadcast(&S.cv_free);
    pthread_mutex_unlock(&S.m);
    if (!ok)
        return NULL;

    const bool adapt = !(getenv("THUG_STEADY_ADAPT") && strcmp(getenv("THUG_STEADY_ADAPT"), "0") == 0);
    int probing = 0;
    unsigned calm = 0, probe_wait = 600;
    uint64_t last_report_t = 0;
    uint64_t fps_t0 = now_us(), fps_n = 0, last_report = 0;
    float fps = 0.0f;
    for (;;) {
        Item it;
        pthread_mutex_lock(&S.m);
        while (!S.stop && S.n_items == 0)
            pthread_cond_wait(&S.cv_item, &S.m);
        if (S.n_items == 0) {
            pthread_mutex_unlock(&S.m);
            break;
        }
        it = S.items[0];
        for (int i = 1; i < S.n_items; ++i)
            S.items[i - 1] = S.items[i];
        --S.n_items;
        pthread_mutex_unlock(&S.m);

        const uint64_t t0 = now_us();
        if (it.fence) {
            F.ClientWaitSync(it.fence, K_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
            F.DeleteSync(it.fence);
        }
        const uint64_t t1 = now_us();
        draw_frame(it, fps);
        SDL_GL_SwapWindow(S.window);
        if (S.steady) {
            // Show this picture for `hold` refreshes in all: swap it again
            // (the blit costs ~0.4 ms). The slot stays busy until then.
            const int hold = S.hold;
            for (int h = 1; h < hold; ++h) {
                draw_frame(it, fps);
                SDL_GL_SwapWindow(S.window);
            }
            const uint64_t period = 1000000ull * (uint64_t)hold / (uint64_t)S.refresh_hz;
            if (S.steady_floor) {
                // The driver's swap does not wait for the refresh: keep time by the clock.
                const uint64_t due = t1 + period - 1000;
                const uint64_t now = now_us();
                if (due > now)
                    usleep((useconds_t)(due - now));
            }
            // Frame-rate check over the first 90 frames: a swap that waits
            // for the refresh makes a frame take about `hold` refreshes.
            static uint64_t prev_end = 0;
            static unsigned checked = 0;
            const uint64_t now2 = now_us();
            if (!S.steady_floor && checked < 90 && prev_end && S.n_items > 0) {
                ++checked;    // only frames that were ready in time say how long a swap takes
                static uint64_t sum = 0;
                sum += now2 - prev_end;
                if (checked == 90) {
                    if (sum / 90 < period * 3 / 4) {
                        S.steady_floor = true;
                        printf("[PRESENT] steady: the swap does not wait for the refresh (%.1f ms/frame, "
                               "wanted %.1f); pacing by the clock\n", sum / 90 / 1000.0, period / 1000.0);
                    }
                }
            }
            prev_end = now2;
            pthread_mutex_lock(&S.m);
            ++S.steady_frames;
            if (S.n_items == 0)
                ++S.late;    // not ready when its predecessor's hold ended: the frame took longer
            const unsigned late = S.late, frames = S.steady_frames;
            pthread_mutex_unlock(&S.m);
            if (frames >= 120) {
                // Every 120 frames: 10% late -> hold one refresh longer (up to
                // base + 2); once calm for a while, try the shorter hold again
                // (the wait doubles after a failed try). THUG_STEADY_ADAPT=0 = fixed.
                const bool bad = late * 10 >= frames;
                const int was = S.hold;
                if (adapt) {
                    if (bad) {
                        if (probing) {
                            ++S.hold;
                            probe_wait = probe_wait * 2 > 5400 ? 5400 : probe_wait * 2;
                            probing = 0;
                        } else if (S.hold < S.hold_base + 2 && S.hold < 6) {
                            ++S.hold;
                        }
                        calm = 0;
                    } else if (probing) {
                        probing = 0;    // the shorter hold works: keep it
                        probe_wait = 600;
                    } else if (S.hold > S.hold_base) {
                        calm += frames;
                        if (calm >= probe_wait) {
                            --S.hold;
                            probing = 1;
                            calm = 0;
                        }
                    }
                }
                if (S.hold != was) {
                    g_thug_vblank_us = thug_present_step_us();
                    printf("[PRESENT] steady: %d refreshes per frame (%.1f FPS) after %u/%u late frames%s\n",
                           S.hold, (double)S.refresh_hz / S.hold, late, frames, probing ? ", trying shorter" : "");
                }
                pthread_mutex_lock(&S.m);
                S.late = S.steady_frames = 0;
                pthread_mutex_unlock(&S.m);
            }
        }
        const uint64_t t2 = now_us();

        pthread_mutex_lock(&S.m);
        S.slots[it.slot].busy = false;
        S.wait_gpu_us += t1 - t0;
        S.blit_swap_us += t2 - t1;
        const uint64_t n = ++S.presented;
        pthread_cond_broadcast(&S.cv_free);
        pthread_mutex_unlock(&S.m);

        ++fps_n;
        if (t2 - fps_t0 >= 500000) {
            fps = (float)(fps_n * 1000000.0 / (double)(t2 - fps_t0));
            fps_n = 0;
            fps_t0 = t2;
        }
        if (n - last_report >= 300) {
            const double d = (double)(n - last_report);
            pthread_mutex_lock(&S.m);
            printf("[PRESENT] %llu frames, ms/frame: game waited for a free buffer %.1f, "
                   "presenter waited for the GPU %.1f, copy + swap %.1f, shown every %.1f\n",
                   (unsigned long long)(n - last_report), S.wait_slot_us / d / 1000.0,
                   S.wait_gpu_us / d / 1000.0, S.blit_swap_us / d / 1000.0,
                   last_report_t ? (t2 - last_report_t) / d / 1000.0 : 0.0);
            last_report_t = t2;
            S.wait_slot_us = S.wait_gpu_us = S.blit_swap_us = 0;
            pthread_mutex_unlock(&S.m);
            last_report = n;
        }
    }
    SDL_GL_MakeCurrent(S.window, NULL);
    return NULL;
}

bool fail(const char *why)
{
    printf("[PRESENT] presenter thread not used: %s\n", why);
    return false;
}

} // namespace

bool thug_present_init(SDL_Window *window, SDL_GLContext ctx1, int w, int h)
{
    if (S.tried)
        return S.on;
    S.tried = true;
    const char *e = getenv("THUG_PRESENTER");
    if (e && strcmp(e, "0") == 0) {
        printf("[PRESENT] presenter thread off (THUG_PRESENTER=0)\n");
        return false;
    }
    if (!gl4es_thug_set_mainfbo || !gl4es_thug_restore)
        return fail("gl4es without the THUG patch");
    S.nslots = 2;
    {   // THUG_PACING_MODE=steady (opt-in): fixed number of refreshes per frame
        const char *pm = getenv("THUG_PACING_MODE");
        if (pm && strcmp(pm, "steady") == 0) {
            if (S.refresh_hz < 30) {
                printf("[PRESENT] steady pacing off: the refresh rate is unknown\n");
            } else {
                const char *v = getenv("THUG_STEADY_VBLANKS");
                int n = v ? atoi(v) : (S.refresh_hz > 100 ? 3 : 2);
                S.hold_base = S.hold = n < 1 ? 1 : n > 6 ? 6 : n;
                S.steady = true;
                S.nslots = 3;
            }
        }
    }
    const bool fn = load(F.GenTextures, "glGenTextures") && load(F.BindTexture, "glBindTexture")
        && load(F.TexImage2D, "glTexImage2D") && load(F.TexParameteri, "glTexParameteri")
        && load(F.GenRenderbuffers, "glGenRenderbuffers") && load(F.BindRenderbuffer, "glBindRenderbuffer")
        && load(F.RenderbufferStorage, "glRenderbufferStorage") && load(F.GenFramebuffers, "glGenFramebuffers")
        && load(F.BindFramebuffer, "glBindFramebuffer") && load(F.FramebufferTexture2D, "glFramebufferTexture2D")
        && load(F.FramebufferRenderbuffer, "glFramebufferRenderbuffer")
        && load(F.CheckFramebufferStatus, "glCheckFramebufferStatus") && load(F.GetIntegerv, "glGetIntegerv")
        && load(F.Clear, "glClear") && load(F.ClearColor, "glClearColor") && load(F.Viewport, "glViewport")
        && load(F.Flush, "glFlush") && load(F.FenceSync, "glFenceSync")
        && load(F.ClientWaitSync, "glClientWaitSync") && load(F.DeleteSync, "glDeleteSync")
        && load(F.CreateShader, "glCreateShader") && load(F.ShaderSource, "glShaderSource")
        && load(F.CompileShader, "glCompileShader") && load(F.GetShaderiv, "glGetShaderiv")
        && load(F.CreateProgram, "glCreateProgram") && load(F.AttachShader, "glAttachShader")
        && load(F.BindAttribLocation, "glBindAttribLocation") && load(F.LinkProgram, "glLinkProgram")
        && load(F.GetProgramiv, "glGetProgramiv") && load(F.GetUniformLocation, "glGetUniformLocation")
        && load(F.UseProgram, "glUseProgram") && load(F.Uniform1i, "glUniform1i")
        && load(F.Uniform4f, "glUniform4f") && load(F.EnableVertexAttribArray, "glEnableVertexAttribArray")
        && load(F.VertexAttribPointer, "glVertexAttribPointer") && load(F.DrawArrays, "glDrawArrays")
        && load(F.Enable, "glEnable") && load(F.Disable, "glDisable") && load(F.BlendFunc, "glBlendFunc")
        && load(F.ActiveTexture, "glActiveTexture") && load(F.BindBuffer, "glBindBuffer");
    if (!fn)
        return fail("GLES 3 entry points missing");
    PfnCur egl_display = reinterpret_cast<PfnCur>(look("eglGetCurrentDisplay"));
    PfnCur egl_context = reinterpret_cast<PfnCur>(look("eglGetCurrentContext"));
    PfnQueryString egl_query = reinterpret_cast<PfnQueryString>(look("eglQueryString"));
    PfnMakeCurrent egl_make_current = reinterpret_cast<PfnMakeCurrent>(look("eglMakeCurrent"));
    if (!egl_display || !egl_context || !egl_query || !egl_make_current)
        return fail("no EGL entry points");
    void *dpy = egl_display();
    void *egl_ctx1 = egl_context();
    if (!dpy || !egl_ctx1)
        return fail("not an EGL context");
    const char *ext = egl_query(dpy, 0x3055 /* EGL_EXTENSIONS */);
    if (!ext || !strstr(ext, "EGL_KHR_surfaceless_context"))
        return fail("EGL_KHR_surfaceless_context missing");
    {   // a fence must work on this context
        void *f = F.FenceSync(K_SYNC_GPU_COMMANDS_COMPLETE, 0);
        F.Flush();
        if (!f)
            return fail("glFenceSync does not work");
        F.ClientWaitSync(f, K_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        F.DeleteSync(f);
    }

    // Off-screen buffers, created with real GLES calls on the game context.
    S.w = w;
    S.h = h;
    SDL_GL_GetDrawableSize(window, &S.dw, &S.dh);
    if (S.dw <= 0 || S.dh <= 0) {
        S.dw = w;
        S.dh = h;
    }
    const GLintT filt = (S.dw == w && S.dh == h) ? K_NEAREST : K_LINEAR;
    GLintT rb_before = 0, fb_before = 0;
    F.GetIntegerv(K_RENDERBUFFER_BINDING, &rb_before);
    F.GetIntegerv(K_FRAMEBUFFER_BINDING, &fb_before);
    bool complete = true;
    for (int i = 0; i < S.nslots; ++i) {
        Slot &s = S.slots[i];
        F.GenTextures(1, &s.tex);
        F.BindTexture(K_TEXTURE_2D, s.tex);
        F.TexImage2D(K_TEXTURE_2D, 0, K_RGBA, w, h, 0, K_RGBA, K_UNSIGNED_BYTE, NULL);
        F.TexParameteri(K_TEXTURE_2D, K_TEXTURE_MIN_FILTER, filt);
        F.TexParameteri(K_TEXTURE_2D, K_TEXTURE_MAG_FILTER, filt);
        F.TexParameteri(K_TEXTURE_2D, K_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
        F.TexParameteri(K_TEXTURE_2D, K_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
        F.GenRenderbuffers(1, &s.rb);
        F.BindRenderbuffer(K_RENDERBUFFER, s.rb);
        F.RenderbufferStorage(K_RENDERBUFFER, K_DEPTH24_STENCIL8, w, h);
        F.GenFramebuffers(1, &s.fbo);
        F.BindFramebuffer(K_FRAMEBUFFER, s.fbo);
        F.FramebufferTexture2D(K_FRAMEBUFFER, K_COLOR_ATTACHMENT0, K_TEXTURE_2D, s.tex, 0);
        F.FramebufferRenderbuffer(K_FRAMEBUFFER, K_DEPTH_STENCIL_ATTACHMENT, K_RENDERBUFFER, s.rb);
        complete = complete && F.CheckFramebufferStatus(K_FRAMEBUFFER) == K_FRAMEBUFFER_COMPLETE;
        F.ClearColor(0, 0, 0, 1);
        F.Clear(K_COLOR_BUFFER_BIT | K_DEPTH_BUFFER_BIT | K_STENCIL_BUFFER_BIT);
        s.busy = false;
    }
    F.BindRenderbuffer(K_RENDERBUFFER, (GLuintT)rb_before);
    F.BindFramebuffer(K_FRAMEBUFFER, (GLuintT)fb_before);
    gl4es_thug_restore(1);    // texture unit 0 binding, active unit, program, array buffer
    if (!complete)
        return fail("off-screen buffer incomplete");

    S.window = window;
    S.ctx1 = ctx1;
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    S.ctx2 = SDL_GL_CreateContext(window);    // current on this thread for now
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    SDL_GL_MakeCurrent(window, ctx1);
    if (!S.ctx2)
        return fail("could not create the second context");
    // The game context leaves the window: surfaceless from now on.
    if (egl_make_current(dpy, NULL, NULL, egl_ctx1) != 1) {
        SDL_GL_MakeCurrent(window, ctx1);
        SDL_GL_DeleteContext(S.ctx2);
        S.ctx2 = NULL;
        return fail("eglMakeCurrent without a surface failed");
    }
    pthread_mutex_init(&S.m, NULL);
    pthread_cond_init(&S.cv_item, NULL);
    pthread_cond_init(&S.cv_free, NULL);
    S.stop = S.ready = S.failed = false;
    if (pthread_create(&S.th, NULL, thread_main, NULL) != 0) {
        SDL_GL_MakeCurrent(window, ctx1);
        return fail("pthread_create failed");
    }
    pthread_mutex_lock(&S.m);
    while (!S.ready && !S.failed)
        pthread_cond_wait(&S.cv_free, &S.m);
    pthread_mutex_unlock(&S.m);
    if (!S.ready) {
        pthread_join(S.th, NULL);
        SDL_GL_MakeCurrent(window, ctx1);
        SDL_GL_DeleteContext(S.ctx2);
        S.ctx2 = NULL;
        return fail("the presenter thread could not take the window");
    }
    S.cur = 0;
    gl4es_thug_set_mainfbo(S.slots[0].fbo, S.slots[0].tex, w, h);
    S.on = true;
    {   // THUG_FPS_METER=1: start with the FPS meter shown (L3 + Select toggles)
        const char *m = getenv("THUG_FPS_METER");
        S.overlay = m && strcmp(m, "1") == 0;
    }
    printf("[PRESENT] presenter thread on: %d off-screen buffers %dx%d, shown at %dx%d "
           "(THUG_PRESENTER=0 turns it off)\n", S.nslots, w, h, S.dw, S.dh);
    if (S.steady)
        printf("[PRESENT] steady pacing: each frame shown for %d refreshes of %d Hz = %.1f FPS "
               "(THUG_STEADY_VBLANKS, THUG_STEADY_ADAPT=0 = never lengthen; THUG_PACING_MODE=vsync = off)\n",
               S.hold, S.refresh_hz, (double)S.refresh_hz / S.hold);
    return true;
}

bool thug_present_active()
{
    return S.on;
}

void thug_present_set_refresh(int hz)
{
    S.refresh_hz = hz;
}

// Game time step in microseconds under steady pacing (hold refreshes), else 0.
unsigned thug_present_step_us()
{
    return S.steady && S.refresh_hz > 0 ? (unsigned)(1000000u * (unsigned)S.hold / (unsigned)S.refresh_hz) : 0;
}

// Quitting: the presenter shows what it was handed, leaves the window and ends.
// Called after thug_glrec_stop, so no more buffers arrive.
void thug_present_stop()
{
    if (!S.on)
        return;
    pthread_mutex_lock(&S.m);
    S.stop = true;
    pthread_cond_broadcast(&S.cv_item);
    pthread_mutex_unlock(&S.m);
    pthread_join(S.th, NULL);
    S.on = false;
}

// Hand the current off-screen buffer to the presenter and wait until the next
// one is free. Runs where the GL context is: the game thread, or the render
// thread (portmaster_glrec.cpp) when it owns the context.
static void handoff(float cpu_ms, int mirror)
{
    S.cpu_ms = cpu_ms;
    void *fence = F.FenceSync(K_SYNC_GPU_COMMANDS_COMPLETE, 0);
    F.Flush();
    pthread_mutex_lock(&S.m);
    S.slots[S.cur].busy = true;
    S.items[S.n_items].slot = S.cur;
    S.items[S.n_items].fence = fence;
    S.items[S.n_items].mirror = mirror;
    ++S.n_items;
    pthread_cond_signal(&S.cv_item);
    S.cur = (S.cur + 1) % S.nslots;
    const uint64_t t0 = now_us();
    while (S.slots[S.cur].busy) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        if (pthread_cond_timedwait(&S.cv_free, &S.m, &ts) != 0) {
            printf("[PRESENT] WARNING: the presenter did not release a buffer within 5 s\n");
            S.slots[S.cur].busy = false;
        }
    }
    S.wait_slot_us += now_us() - t0;
    pthread_mutex_unlock(&S.m);
}

// Render thread side of a recorded present.
void thug_present_submit_rt(float cpu_ms, int mirror)
{
    handoff(cpu_ms, mirror);
}

extern "C" int g_thug_present_mirror = 0;

void thug_present_submit(float cpu_ms)
{
    const int mirror = g_thug_present_mirror;    // this frame's picture
    g_thug_present_mirror = 0;
    glFlush();    // gl4es first, then the fence behind everything it sent
    if (thug_glrec_active()) {
        // Recorded: the render thread runs handoff() in order. The game thread
        // switches gl4es to the other buffer now (its bind is recorded too).
        thug_glrec_present(cpu_ms, mirror);
        S.cur_game = (S.cur_game + 1) % S.nslots;
        gl4es_thug_set_mainfbo(S.slots[S.cur_game].fbo, S.slots[S.cur_game].tex, S.w, S.h);
        return;
    }
    handoff(cpu_ms, mirror);
    S.cur_game = S.cur;
    gl4es_thug_set_mainfbo(S.slots[S.cur].fbo, S.slots[S.cur].tex, S.w, S.h);
}

void thug_present_toggle_overlay()
{
    S.overlay = !S.overlay;
    printf("[PRESENT] FPS meter %s (L3 + Select)\n", S.overlay ? "on" : "off");
}

bool thug_present_overlay()
{
    return S.overlay;
}
