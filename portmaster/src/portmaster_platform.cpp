/*****************************************************************************
**  THUG PortMaster / Linux R36S Platform Harness                           **
**  portmaster/src/portmaster_platform.cpp                                  **
*****************************************************************************/

#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <dlfcn.h>
#ifdef __arm__
#include <ucontext.h>
#endif

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gl4esinit.h>
#include <algorithm>

#include <psp2/types.h>
#include <psp2/ctrl.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <vitaGL.h>
#include <vita_log.h>
#include <vita_dbgsrv.h>
#include "portmaster_present.h"
#include "portmaster_glrec.h"
void thug_profile_start();    // portmaster_profile.cpp (THUG_PROFILE=1)

// --- Runtime Globals ---
unsigned int vgl_framecount = 0;
extern "C" {
volatile unsigned g_portmaster_crash_stage = 0;
}
bool g_hasJustEnteredNetworkGame = false;
bool gbQuit = false;
bool gun_fired = false;
float screen_angle = 72.0f;
int sceLibcHeapSize = 4 * 1024 * 1024;
int _newlib_heap_size_user = 192 * 1024 * 1024;
int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// --- Logging ---
static bool s_verbose_log = false;

void vita_log_init(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *verbose = getenv("THUG_VERBOSE");
    s_verbose_log = verbose && strcmp(verbose, "1") == 0;
    printf("[SYS] THUG PortMaster\n");
}

void vita_log_shutdown(void) {}
void vita_log_start_watchdog(void) {}

void vita_log_flush(void)
{
    fflush(stdout);
}

void vita_log_printf(const char *sys, const char *fmt, ...)
{
    if (!s_verbose_log && strncmp(fmt, "!!", 2) != 0) {
        static const char *const categories[] = {
            "SYS", "GFX", "ERR", "ASSERT", "CRASH", "LVL", "ACC", "PAD",
            "PERF", "SCN", "SHD", "OMB", "PROF"
        };
        bool keep = false;
        for (size_t index = 0; sys && index < sizeof(categories) / sizeof(categories[0]); ++index)
            keep |= strcmp(sys, categories[index]) == 0;
        if (!keep)
            return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[%s] %s\n", sys ? sys : "LOG", buf);
}

int vita_log_ring_read(unsigned int *pos, char *out, int max)
{
    return 0;
}

// Vita_OutputDebugString in compat.cpp

#ifdef __arm__
static void crash_hex(unsigned value, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) { out[i] = digits[value & 15]; value >>= 4; }
}

static void crash_report(int signal_number, siginfo_t *info, void *context)
{
    const ucontext_t *state = static_cast<const ucontext_t *>(context);
    char line[] = "[CRASH] signal=0x00000000 pc=0x00000000 lr=0x00000000 addr=0x00000000\n";
    crash_hex((unsigned)signal_number, line + 17);
    crash_hex((unsigned)state->uc_mcontext.arm_pc, line + 31);
    crash_hex((unsigned)state->uc_mcontext.arm_lr, line + 45);
    crash_hex((unsigned)(uintptr_t)info->si_addr, line + 61);
    write(STDERR_FILENO, line, sizeof(line) - 1);
    char stage[] = "[CRASH] stage=0x00000000\n";
    crash_hex(g_portmaster_crash_stage, stage + 16);
    write(STDERR_FILENO, stage, sizeof(stage) - 1);
    // which thread: thug.armhf (game), thug-render, thug-present, or a library's
    int comm = open("/proc/thread-self/comm", O_RDONLY);
    if (comm >= 0) {
        const char label[] = "[CRASH] thread=";
        char name[32];
        const ssize_t n = read(comm, name, sizeof(name));
        close(comm);
        if (n > 0) {
            write(STDERR_FILENO, label, sizeof(label) - 1);
            write(STDERR_FILENO, name, n);    // ends with a newline
        }
    }
    const char heading[] = "[CRASH] memory maps:\n";
    write(STDERR_FILENO, heading, sizeof(heading) - 1);
    int maps = open("/proc/self/maps", O_RDONLY);
    if (maps >= 0) {
        char chunk[4096];
        ssize_t bytes;
        while ((bytes = read(maps, chunk, sizeof(chunk))) > 0)
            write(STDERR_FILENO, chunk, bytes);
        close(maps);
    }
    _exit(128 + signal_number);
}

static void install_crash_report(void)
{
    struct sigaction action = {};
    action.sa_sigaction = crash_report;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
}
#endif

// --- SDL2 Video / Window / OpenGL ---
static SDL_Window *s_window = NULL;
static SDL_GLContext s_gl_ctx = NULL;
static SDL_GameController *s_controller = NULL;
static int s_refresh_hz = 0;
static int s_fps_cap = 30;
static int s_swap_interval = 0;
static Uint64 s_last_swap = 0;
static Uint64 s_swap_lead = 0;
static unsigned s_short_swaps = 0;
static bool s_vblank_lead = false;
static bool s_perf_enabled = false;
static bool s_adaptive_pacing = true;
// THUG_PACING_MODE: 0 legacy (deadline from last swap), 1 grid (absolute
// deadlines at the 30/60 cap), 2 locked (swap_interval 1, every Nth vblank),
// 3 vsync (default): swap_interval 1, no cap, no software sleep -- every frame
// is shown at the first refresh after it is ready, like the NFS ports. The
// game time step then follows whole refreshes (Code/Sys/Vita/p_timer.cpp).
static int s_pace_mode = -1;
static bool s_vsync_requested = false;    // vsync asked for, even if it fell back
static int s_lock_vblanks = 3;
static Uint64 s_deadline = 0;
static bool s_gpu_probe = false;
// Refresh period in microseconds while vsync pacing is active, 0 otherwise.
// Read by Tmr::OncePerRender to step game time in whole refreshes.
extern "C" unsigned g_thug_vblank_us = 0;
// Screen shape, for any PortMaster device: the game renders at the screen's
// aspect ratio (g_thug_render_w x g_thug_render_h) and the presenter scales
// that to the whole screen without changing its shape.
extern "C" int g_thug_render_w = THUG_FRAMEBUFFER_WIDTH;
extern "C" int g_thug_render_h = THUG_FRAMEBUFFER_HEIGHT;
extern "C" float g_thug_screen_aspect = 4.0f / 3.0f;

static void resolve_pacing_mode()
{
    if (s_pace_mode >= 0)
        return;
    const char *m = getenv("THUG_PACING_MODE");
    s_pace_mode = !m ? 3 : !strcmp(m, "legacy") ? 0 : !strcmp(m, "locked") ? 2
                : !strcmp(m, "grid") ? 1 : 3;
    s_vsync_requested = s_pace_mode == 3;
    const char *n = getenv("THUG_LOCK_VBLANKS");
    s_lock_vblanks = n ? atoi(n) : 3;
    if (s_lock_vblanks < 1 || s_lock_vblanks > 6)
        s_lock_vblanks = 3;
    s_gpu_probe = getenv("THUG_GPU_PROBE") && strcmp(getenv("THUG_GPU_PROBE"), "0") != 0;
}

extern "C" void vita_vgl_init_tot(void);
// Patched gl4es (portmaster/cmake/gl4es-userprog-state.patch); weak so a
// build against an unpatched gl4es or desktop GL still links.
extern "C" unsigned int gl4es_thug_counts[2] __attribute__((weak));
extern "C" void gl4es_thug_save_psa(void) __attribute__((weak));

// gl4es PrecompiledShaderArchive: fixed-pipeline programs kept as binaries in
// ports/thug/.gl4es.psa, so later launches skip their first-use compile
// (40-60 ms each on the Mali). THUG_PSA=0 turns it off.
static void save_psa_at_exit()
{
    if (gl4es_thug_save_psa)
        gl4es_thug_save_psa();
}

static void get_framebuffer_size(int *width, int *height)
{
    if (thug_present_active()) {    // the game draws into the presenter's buffers
        *width = g_thug_render_w;
        *height = g_thug_render_h;
        return;
    }
    SDL_GL_GetDrawableSize(s_window, width, height);
}

static void open_controller()
{
    for (int index = 0; index < SDL_NumJoysticks() && !s_controller; ++index) {
        if (SDL_IsGameController(index))
            s_controller = SDL_GameControllerOpen(index);
    }
    if (s_controller) {
        char *mapping = SDL_GameControllerMapping(s_controller);
        printf("[PAD] %s, mapping: %s\n", SDL_GameControllerName(s_controller),
               mapping ? mapping : "(none)");
        SDL_free(mapping);
    }
}

int vglSwapInterval(void *, int interval)
{
    s_fps_cap = interval == 2 ? 30 : 60;
    const char *override = getenv("THUG_FPS");
    if (override && strcmp(override, "0") == 0)
        s_fps_cap = 0;
    if (thug_present_active()) {
        // The presenter thread owns the window and swaps with interval 1;
        // the game context has no surface, so nothing to set here.
        s_last_swap = 0;
        s_pace_mode = 3;
        s_swap_interval = 1;
        s_vblank_lead = false;
        // steady pacing: the game steps in whole holds of refreshes, not single ones
        const unsigned steady_us = thug_present_step_us();
        g_thug_vblank_us = steady_us ? steady_us : s_refresh_hz ? 1000000u / s_refresh_hz : 0;
        printf("[VIDEO] refresh=%d Hz pacing_mode=%s (presenter thread)\n", s_refresh_hz,
               steady_us ? "steady" : "vsync");
        return 1;
    }
    const bool exact_divisor = s_fps_cap && s_refresh_hz && s_refresh_hz % s_fps_cap == 0;
    // An inexact refresh divisor needs software pacing. A one-vblank swap
    // quantizes 30 FPS to three 77 Hz refreshes (about 26 FPS on R36S).
    s_swap_interval = exact_divisor && s_fps_cap ? 1 : 0;
    const int wanted = exact_divisor ? s_refresh_hz / s_fps_cap : s_swap_interval;
    if (SDL_GL_SetSwapInterval(wanted) == 0) {
        s_swap_interval = wanted;
    } else if (SDL_GL_SetSwapInterval(s_swap_interval) != 0) {
        s_swap_interval = 0;
    }
    s_last_swap = 0;
    s_swap_lead = 0;
    s_short_swaps = 0;
    s_deadline = 0;
    s_vblank_lead = s_swap_interval != 0;
    resolve_pacing_mode();
    if ((s_pace_mode == 2 || s_pace_mode == 3) && s_refresh_hz && (s_fps_cap || s_pace_mode == 3)) {
        if (SDL_GL_SetSwapInterval(1) == 0) {
            s_swap_interval = 1;
            s_vblank_lead = false;
        } else {
            s_pace_mode = 1;
        }
    } else if (s_pace_mode == 3) {
        s_pace_mode = 1;    // refresh rate unknown: software pacing
    }
    g_thug_vblank_us = (s_pace_mode == 3 && s_refresh_hz) ? 1000000u / s_refresh_hz : 0;
    printf("[VIDEO] refresh=%d Hz cap=%d swap_interval=%d pacing_mode=%s",
           s_refresh_hz, s_pace_mode == 3 ? 0 : s_fps_cap, s_swap_interval,
           s_pace_mode == 0 ? "legacy" : s_pace_mode == 2 ? "locked"
           : s_pace_mode == 3 ? "vsync (uncapped)" : "grid");
    if (s_pace_mode == 2)
        printf(" lock_vblanks=%d (%.1f FPS)", s_lock_vblanks,
               static_cast<double>(s_refresh_hz) / s_lock_vblanks);
    printf("\n");
    return 1;
}

// Only on a failed window or context: SDL replaces the driver's own error with a
// generic one, so name the video driver, the EGL library and what EGL says.
static void report_gl_failure(const char *what)
{
    fprintf(stderr, "[SDL] %s failed: %s\n", what, SDL_GetError());
    const char *cur = SDL_GetCurrentVideoDriver();
    fprintf(stderr, "[GLDIAG] video driver: %s (built in:", cur ? cur : "(none)");
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++)
        fprintf(stderr, " %s", SDL_GetVideoDriver(i));
    fprintf(stderr, ")\n");
    static const char *const env[] = { "SDL_VIDEODRIVER", "SDL_VIDEO_EGL_DRIVER", "SDL_VIDEO_GL_DRIVER",
                                       "LD_LIBRARY_PATH", "WAYLAND_DISPLAY", "DISPLAY" };
    for (const char *e : env) {
        const char *v = getenv(e);
        fprintf(stderr, "[GLDIAG] %s=%s\n", e, v ? v : "(unset)");
    }
    static const char *const nodes[] = { "/dev/fb0", "/dev/mali", "/dev/mali0", "/dev/dri/card0" };
    for (const char *n : nodes)
        fprintf(stderr, "[GLDIAG] %s: %s\n", n, access(n, R_OK | W_OK) == 0 ? "rw" : strerror(errno));
    static const char *const libs[] = { "libEGL.so", "libEGL.so.1", "libGLESv2.so", "libGLESv2.so.2", "libmali.so" };
    for (const char *l : libs) {
        void *h = dlopen(l, RTLD_NOW | RTLD_LOCAL);
        fprintf(stderr, "[GLDIAG] dlopen %s: %s\n", l, h ? "ok" : dlerror());
        if (h) dlclose(h);
    }
    // EGL itself, on the default display (what SDL's fbdev drivers use).
    if (void *h = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL)) {
        typedef void *(*GetDisplay)(void *);
        typedef unsigned (*Initialize)(void *, int *, int *);
        typedef int (*GetError)(void);
        typedef const char *(*QueryString)(void *, int);
        typedef unsigned (*Terminate)(void *);
        GetDisplay get_display = (GetDisplay)dlsym(h, "eglGetDisplay");
        Initialize initialize = (Initialize)dlsym(h, "eglInitialize");
        GetError get_error = (GetError)dlsym(h, "eglGetError");
        QueryString query = (QueryString)dlsym(h, "eglQueryString");
        Terminate terminate = (Terminate)dlsym(h, "eglTerminate");
        if (get_display && initialize && get_error) {
            void *dpy = get_display(NULL);
            fprintf(stderr, "[GLDIAG] eglGetDisplay(default) = %p, error 0x%x\n", dpy, get_error());
            int major = 0, minor = 0;
            if (dpy && initialize(dpy, &major, &minor)) {
                fprintf(stderr, "[GLDIAG] eglInitialize ok: EGL %d.%d, vendor %s\n", major, minor,
                        query ? query(dpy, 0x3053 /* EGL_VENDOR */) : "?");
                if (terminate) terminate(dpy);
            } else {
                fprintf(stderr, "[GLDIAG] eglInitialize failed, error 0x%x\n", get_error());
            }
        } else {
            fprintf(stderr, "[GLDIAG] libEGL.so lacks eglGetDisplay/eglInitialize\n");
        }
        dlclose(h);
    }
    // SDL's own attempt again, now that the generic message is logged.
    if (SDL_GL_LoadLibrary(NULL) == 0) {
        fprintf(stderr, "[GLDIAG] SDL_GL_LoadLibrary: ok on retry\n");
        SDL_GL_UnloadLibrary();
    } else {
        fprintf(stderr, "[GLDIAG] SDL_GL_LoadLibrary: %s\n", SDL_GetError());
    }
    fflush(stderr);
}

// On multiarch firmwares (Knulli) the bare names libEGL.so / libGLESv2.so find the
// 64-bit driver ("wrong ELF class") and the 32-bit one is only known as libmali.so.0,
// so SDL cannot load GLES. When neither bare name loads, hand SDL the 32-bit files
// by full path. Does nothing when the names load (R36S) or SDL_VIDEO_*_DRIVER is
// already set. Off switch: THUG_GL_LIB_SEARCH=0.
static void find_gles_libraries()
{
    const char *sw = getenv("THUG_GL_LIB_SEARCH");
    if (sw && strcmp(sw, "0") == 0)
        return;
    if (getenv("SDL_VIDEO_EGL_DRIVER") || getenv("SDL_VIDEO_GL_DRIVER"))
        return;
    static const char *const egl_names[] = { "libEGL.so.1", "libEGL.so" };
    for (const char *n : egl_names)
        if (void *h = dlopen(n, RTLD_LAZY | RTLD_LOCAL)) {
            dlclose(h);
            return;
        }
    static const char *const dirs[] = { "/usr/lib32", "/lib32", "/usr/lib/arm-linux-gnueabihf",
                                        "/lib/arm-linux-gnueabihf" };
    static const char *const egl_files[] = { "libEGL.so.1", "libEGL.so" };
    static const char *const gles_files[] = { "libGLESv2.so.2", "libGLESv2.so" };
    char egl[256], gles[256];
    for (const char *d : dirs) {
        egl[0] = gles[0] = '\0';
        for (const char *f : egl_files) {
            snprintf(egl, sizeof egl, "%s/%s", d, f);
            void *h = dlopen(egl, RTLD_LAZY | RTLD_LOCAL);
            const bool ok = h && dlsym(h, "eglChooseConfig");
            if (h) dlclose(h);
            if (ok) break;
            egl[0] = '\0';
        }
        for (const char *f : gles_files) {
            snprintf(gles, sizeof gles, "%s/%s", d, f);
            void *h = dlopen(gles, RTLD_LAZY | RTLD_LOCAL);
            if (h) { dlclose(h); break; }
            gles[0] = '\0';
        }
        if (egl[0] && gles[0]) {
            setenv("SDL_VIDEO_EGL_DRIVER", egl, 1);
            setenv("SDL_VIDEO_GL_DRIVER", gles, 1);
            fprintf(stderr, "[VIDEO] GLES libraries by path: %s, %s\n", gles, egl);
            return;
        }
    }
    fprintf(stderr, "[VIDEO] no loadable EGL library found by name or in the 32-bit folders\n");
}

// Quitting (Select + Start, the window closing). On the RG34XX (Knulli, Mali
// fbdev r20p0) exit() crashed inside libmali while the render and presenter
// threads were still running; the R36S driver did not. A clean quit ends those
// threads first, runs the atexit work (shader archive, profiler) and then
// leaves with _exit, so the GPU driver is never torn down in-process (the
// kernel frees it). THUG_CLEAN_QUIT=0 = plain exit().
static bool s_clean_quit = true;
static volatile int s_quit_code = -1;

static void quit_last()    // registered first, so it runs after every other atexit handler
{
    if (s_quit_code < 0)
        return;    // an exit() not from thug_quit: unchanged
    fflush(NULL);
    _exit(s_quit_code);
}

static void thug_quit(int code)
{
    if (s_clean_quit) {
        s_quit_code = code;
        thug_glrec_stop();       // finishes recorded GL work, ends the render thread
        thug_present_stop();     // shows what it was handed, ends the presenter thread
    }
    exit(code);
}

int vglInitExtended(int flags, int width, int height, int ram_sz, int msaa)
{
#ifdef __arm__
    install_crash_report();
#endif
    static bool s_quit_registered = false;
    if (!s_quit_registered) {
        s_quit_registered = true;
        const char *q = getenv("THUG_CLEAN_QUIT");
        s_clean_quit = !(q && strcmp(q, "0") == 0);
        if (s_clean_quit)
            atexit(quit_last);
    }
    thug_profile_start();
    if (s_gl_ctx) return 0;

    find_gles_libraries();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0)
    {
        fprintf(stderr, "[SDL] Failed to init SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Try loading gamecontrollerdb.txt
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Full screen at the screen's own size on the handhelds; a 640x480 window
    // on a desktop (x11). THUG_WINDOW_SIZE=WxH sets the size (tests).
    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    int win_w = THUG_FRAMEBUFFER_WIDTH, win_h = THUG_FRAMEBUFFER_HEIGHT;
    const char *video_driver = SDL_GetCurrentVideoDriver();
    SDL_DisplayMode desktop;
    if (video_driver && strcmp(video_driver, "x11") != 0 &&
        SDL_GetDesktopDisplayMode(0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0) {
        win_w = desktop.w;
        win_h = desktop.h;
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    {
        const char *size = getenv("THUG_WINDOW_SIZE");
        int w = 0, h = 0;
        if (size && sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            win_w = w;
            win_h = h;
            win_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
    }
    s_window = SDL_CreateWindow("Tony Hawk's Underground", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
    if (!s_window)
    {
        report_gl_failure("window creation");
        exit(EXIT_FAILURE);
    }

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx)
    {
        report_gl_failure("GLES context creation");
        exit(EXIT_FAILURE);
    }

    typedef const GLubyte *(*GetString)(GLenum);
    GetString driver_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    if (!driver_string || !driver_string(GL_VERSION)) {
        fprintf(stderr, "[SDL] No current GLES context\n");
        exit(EXIT_FAILURE);
    }
    printf("[VIDEO] driver=%s GLES=%s renderer=%s\n", SDL_GetCurrentVideoDriver(),
           driver_string(GL_VERSION), driver_string(GL_RENDERER));
    // Recording stubs for every real GLES call (render thread, portmaster_glrec.cpp);
    // they call the driver directly until the render thread is started.
    set_getprocaddress(thug_glrec_getproc);
    set_getmainfbsize(get_framebuffer_size);
    {
        const char *psa = getenv("THUG_PSA");
        if (psa && strcmp(psa, "0") == 0)
            setenv("LIBGL_NOPSA", "1", 1);
        else if (!getenv("LIBGL_PSA_FOLDER"))
            setenv("LIBGL_PSA_FOLDER", ".", 0);    // the port folder (cwd)
    }
    initialize_gl4es();
    atexit(save_psa_at_exit);

    SDL_DisplayMode mode;
    const int display = SDL_GetWindowDisplayIndex(s_window);
    if (SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0)
        s_refresh_hz = mode.refresh_rate;
    {
        // List the panel's modes once; THUG_DISPLAY_HZ=60 (opt-in) asks for
        // a mode at that refresh, same size, so 30 FPS divides it evenly.
        const int count = SDL_GetNumDisplayModes(display);
        const char *want_env = getenv("THUG_DISPLAY_HZ");
        const int want = want_env ? atoi(want_env) : 0;
        SDL_DisplayMode chosen;
        bool found = false;
        for (int i = 0; i < count; ++i) {
            SDL_DisplayMode m;
            if (SDL_GetDisplayMode(display, i, &m) != 0)
                continue;
            printf("[VIDEO] mode %d: %dx%d @ %d Hz\n", i, m.w, m.h, m.refresh_rate);
            if (want && !found && m.refresh_rate == want && m.w == mode.w && m.h == mode.h) {
                chosen = m;
                found = true;
            }
        }
        if (want && want != s_refresh_hz) {
            if (found && SDL_SetWindowDisplayMode(s_window, &chosen) == 0 &&
                SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN) == 0 &&
                SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0) {
                s_refresh_hz = mode.refresh_rate;
                printf("[VIDEO] THUG_DISPLAY_HZ=%d: now %d Hz\n", want, s_refresh_hz);
            } else {
                printf("[VIDEO] THUG_DISPLAY_HZ=%d: no such mode or switch failed (%s), staying at %d Hz\n",
                       want, found ? SDL_GetError() : "not listed", s_refresh_hz);
            }
        }
    }
    s_perf_enabled = getenv("THUG_PERF") && strcmp(getenv("THUG_PERF"), "1") == 0;
    s_adaptive_pacing = !getenv("THUG_PACING_ADAPT") ||
                        strcmp(getenv("THUG_PACING_ADAPT"), "0") != 0;
    vglSwapInterval(NULL, 2);
    int screen_w = 0, screen_h = 0;
    SDL_GL_GetDrawableSize(s_window, &screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) {
        screen_w = win_w;
        screen_h = win_h;
    }
    g_thug_screen_aspect = (float)screen_w / (float)screen_h;
    g_thug_render_w = screen_w;
    g_thug_render_h = screen_h;
    if (s_vsync_requested) {
        // vsync pacing: let a presenter thread own the window so the game
        // thread never waits in the swap (portmaster_present.cpp). The game
        // renders at the screen's shape, at most THUG_RENDER_HEIGHT lines
        // (default 480, the R36S's cost), and the presenter scales it up.
        const char *cap_env = getenv("THUG_RENDER_HEIGHT");
        const int cap = cap_env ? atoi(cap_env) : 480;
        int rh = (cap > 0 && screen_h > cap) ? cap : screen_h;
        // THUG_RENDER_SCALE=0.75 (default 1 = off): render a fraction of that
        // height, e.g. 480 -> 360 lines on the RG34XX. Clamped to 0.25..1.
        const char *scale_env = getenv("THUG_RENDER_SCALE");
        float scale = scale_env ? (float)atof(scale_env) : 1.0f;
        if (scale < 0.25f) scale = 0.25f;
        if (scale > 1.0f) scale = 1.0f;
        if (scale < 1.0f) {
            rh = ((int)((float)rh * scale + 0.5f)) & ~1;
            if (rh < 2) rh = 2;
            printf("[VIDEO] THUG_RENDER_SCALE=%.2f: render height %d\n", scale, rh);
        }
        int rw = ((int)((float)rh * g_thug_screen_aspect + 0.5f) + 1) & ~1;
        if (rw == screen_w + 1)
            rw = screen_w;
        thug_present_set_refresh(s_refresh_hz);
        if (thug_present_init(s_window, s_gl_ctx, rw, rh)) {
            g_thug_render_w = rw;
            g_thug_render_h = rh;
            // gl4es took the screen size as its default viewport; the game
            // draws into rw x rh buffers and never sets a full viewport itself.
            glViewport(0, 0, rw, rh);
            vglSwapInterval(NULL, 2);    // logs the presenter pacing line
            thug_glrec_start();          // driver calls move to a render thread
        }
    }
    printf("[VIDEO] screen %dx%d (aspect %.3f), game renders %dx%d\n", screen_w, screen_h,
           g_thug_screen_aspect, g_thug_render_w, g_thug_render_h);
    SDL_ShowCursor(SDL_DISABLE);

    open_controller();

    return 0;
}

int vglInit(int width, int height)
{
    return vglInitExtended(0, width, height, 0, 0);
}

void vglSwapBuffers(GLboolean wait)
{
    vgl_framecount++;
    if (s_window)
    {
        const Uint64 frequency = SDL_GetPerformanceFrequency();
        const Uint64 submit = SDL_GetPerformanceCounter();
        bool paced = false;
        // THUG_PACING_MODE: "grid" (default) keeps an absolute deadline grid so
        // sleep and swap overshoot no longer add up each frame; "legacy" is the
        // old last-swap-relative deadline; "locked" (vblank-locked) uses
        // swap_interval 1 and presents every THUG_LOCK_VBLANKS-th vblank
        // (default 3: 77/3 = 25.7 FPS, even cadence).
        const int pace_mode = s_pace_mode;
        // THUG_PACE_FLUSH=0 disables: flush the frame before a pacing sleep so
        // the GPU renders during the sleep instead of after it.
        static int pace_flush = -1;
        if (pace_flush < 0)
            pace_flush = !getenv("THUG_PACE_FLUSH") || strcmp(getenv("THUG_PACE_FLUSH"), "0") != 0;
        if (pace_mode == 2 && s_last_swap && s_fps_cap) {
            // Swap returned on vblank V0. Submit 1 ms after V0+N-1 so the
            // interval-1 swap flips on V0+N.
            const Uint64 target = s_last_swap +
                                  static_cast<Uint64>(s_lock_vblanks - 1) * frequency / s_refresh_hz +
                                  frequency / 1000;
            const Uint64 now = SDL_GetPerformanceCounter();
            if (target > now) {
                if (pace_flush)
                    glFlush();
                const Uint64 ns = (target - now) * 1000000000ULL / frequency;
                struct timespec delay = { static_cast<time_t>(ns / 1000000000),
                                          static_cast<long>(ns % 1000000000) };
                while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
                paced = true;
            }
        } else if (pace_mode != 3 && s_last_swap && s_fps_cap &&
            (!s_vblank_lead || s_fps_cap * s_swap_interval != s_refresh_hz)) {
            // With a one-vblank swap, submit during the vblank before the deadline.
            const Uint64 lead = s_vblank_lead ? frequency / (2 * s_refresh_hz) :
                                (s_adaptive_pacing ? std::min(s_swap_lead, frequency / 100) : 0);
            const Uint64 period = frequency / s_fps_cap;
            Uint64 base = s_last_swap;
            if (pace_mode == 1 && !s_vblank_lead) {
                // Absolute grid: next deadline = previous deadline + period.
                // Resynchronise only after a frame misses by a whole period.
                const Uint64 now0 = SDL_GetPerformanceCounter();
                if (!s_deadline || now0 > s_deadline + period)
                    s_deadline = s_last_swap;
                base = s_deadline;
                s_deadline += period;
            }
            const Uint64 target = base + period - lead;
            const Uint64 now = SDL_GetPerformanceCounter();
            if (target > now) {
                if (pace_flush)
                    glFlush();
                const Uint64 ns = (target - now) * 1000000000ULL / frequency;
                struct timespec delay = { static_cast<time_t>(ns / 1000000000),
                                          static_cast<long>(ns % 1000000000) };
                while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
                paced = true;
            }
        }
        // THUG_GPU_PROBE=1 (diagnostic, costs overlap): glFinish before the
        // swap splits present time into GPU tail and flip/vblank wait.
        Uint64 gpu_done = 0, pre_finish = 0;
        if (s_gpu_probe) {
            pre_finish = SDL_GetPerformanceCounter();
            glFinish();
            gpu_done = SDL_GetPerformanceCounter();
        }
        const Uint64 before_swap = SDL_GetPerformanceCounter();
        const bool presenter = thug_present_active();
        if (presenter)
            thug_present_submit(s_last_swap ? (submit - s_last_swap) * 1000.0f / frequency : 0.0f);
        else
            SDL_GL_SwapWindow(s_window);
        const Uint64 now = SDL_GetPerformanceCounter();
        if (!presenter && (pace_mode == 2 || pace_mode == 3) && s_last_swap && s_refresh_hz) {
            // swap_interval 1 accepted but not honoured: fall back to grid.
            const double vblank_ms = 1000.0 / s_refresh_hz;
            const int expected = pace_mode == 3 ? 1 : s_lock_vblanks;
            static unsigned short_locked = 0;
            if ((now - s_last_swap) * 1000.0 / frequency < (expected - 0.75) * vblank_ms) {
                if (++short_locked == 8) {
                    s_pace_mode = 1;
                    g_thug_vblank_us = 0;
                    SDL_GL_SetSwapInterval(0);
                    s_swap_interval = 0;
                    if (!s_fps_cap)
                        s_fps_cap = 30;
                    printf("[VIDEO] %s pacing: swap does not wait for vblank, using grid\n",
                           pace_mode == 3 ? "vsync" : "locked");
                }
            } else {
                short_locked = 0;
            }
        }
        if (paced && !s_vblank_lead && s_adaptive_pacing) {
            const Uint64 cost = std::min(now - before_swap, frequency / 100);
            s_swap_lead = s_swap_lead ? (7 * s_swap_lead + cost) / 8 : cost;
        }
        const double scale = 1000.0 / frequency;
        const double frame_ms = (now - s_last_swap) * scale;
        if (s_last_swap && s_vblank_lead && s_fps_cap) {
            // Some EGL implementations accept an interval but return before vblank.
            if (frame_ms < 1000.0 / s_fps_cap - 250.0 / s_refresh_hz) {
                if (++s_short_swaps == 3) {
                    if (s_swap_interval > 1) {
                        s_swap_interval = SDL_GL_SetSwapInterval(1) == 0 ? 1 : 0;
                        s_vblank_lead = s_swap_interval != 0;
                    } else {
                        s_vblank_lead = false;
                    }
                    s_short_swaps = 0;
                    printf("[VIDEO] using software pacing with swap_interval=%d lead=%d\n",
                           s_swap_interval, s_vblank_lead);
                }
            } else {
                s_short_swaps = 0;
            }
        }
        if (s_perf_enabled && s_last_swap) {
            static const unsigned window = 120;
            static double frames[window], work[window], wait_time[window];
            static double gpu_tail[window], flip_wait[window];
            static unsigned count = 0;
            static unsigned on_time = 0, on_cadence = 0, hitches = 0;
            static unsigned vblanks[5];
            static long last_faults = 0;
            frames[count] = frame_ms;
            work[count] = (submit - s_last_swap) * scale;
            wait_time[count] = (now - submit) * scale;
            gpu_tail[count] = gpu_done ? (gpu_done - pre_finish) * scale : 0.0;
            flip_wait[count] = (now - before_swap) * scale;
            static unsigned changes = 0;
            static int last_v = 0;
            if (s_refresh_hz) {
                // Vblanks this frame stayed on screen (playbook vblank-pacing).
                // Exact with vsync/locked; with grid it is only the rounded
                // swap interval (the panel still alternates 2 and 3).
                int v = static_cast<int>(frame_ms * s_refresh_hz / 1000.0 + 0.5);
                v = v < 1 ? 1 : v > 4 ? 4 : v;
                vblanks[v]++;
                // Judder: consecutive frames shown for a different number of
                // refreshes.
                changes += last_v && v != last_v;
                last_v = v;
            }
            // Half a refresh separates a late vblank from timer jitter.
            const int cap = s_fps_cap ? s_fps_cap : 30;
            const double target = 1000.0 / cap;
            const double half_vblank = 500.0 / (s_refresh_hz ? s_refresh_hz : 60);
            on_time += frame_ms < target + half_vblank;
            on_cadence += frame_ms >= target - half_vblank && frame_ms < target + half_vblank;
            hitches += frames[count] >= 50.0;
            if (++count == window) {
                std::sort(frames, frames + window);
                std::sort(work, work + window);
                std::sort(wait_time, wait_time + window);
                struct rusage usage = {};
                getrusage(RUSAGE_SELF, &usage);
                printf("[PERF] frames=%u cap=%d target=%d p50_ms=%.2f p90_ms=%.2f work_p90_ms=%.2f "
                       "present_p90_ms=%.2f pace_lead_ms=%.2f on_time=%u/%u cadence=%u/%u hitches_50ms=%u "
                       "rss_peak_kb=%ld major_faults=%ld\n",
                       vgl_framecount, s_fps_cap, cap, frames[59], frames[107], work[107],
                       wait_time[107], s_swap_lead * scale, on_time, window, on_cadence, window, hitches,
                       usage.ru_maxrss, usage.ru_majflt - last_faults);
                last_faults = usage.ru_majflt;
                printf("[PACE] cadence_changes=%u/%u ", changes, window);
                changes = 0;
                printf("mode=%s refresh=%d Hz vblanks 1:%u 2:%u 3:%u 4+:%u",
                       s_pace_mode == 0 ? "legacy" : s_pace_mode == 2 ? "locked"
                       : s_pace_mode == 3 ? "vsync" : "grid",
                       s_refresh_hz, vblanks[1], vblanks[2], vblanks[3], vblanks[4]);
                if (gl4es_thug_counts) {
                    // gl4es patch counters: GLSL draws, and those through a
                    // customised program copy (uniform re-sync every draw).
                    printf(" | glsl_draws=%u custom=%u", gl4es_thug_counts[0] / window,
                           gl4es_thug_counts[1] / window);
                    gl4es_thug_counts[0] = gl4es_thug_counts[1] = 0;
                }
                if (s_gpu_probe) {
                    std::sort(gpu_tail, gpu_tail + window);
                    std::sort(flip_wait, flip_wait + window);
                    printf(" | gpu_tail p50=%.2f p90=%.2f flip_wait p50=%.2f p90=%.2f ms",
                           gpu_tail[59], gpu_tail[107], flip_wait[59], flip_wait[107]);
                }
                printf("\n");
                memset(vblanks, 0, sizeof(vblanks));
                count = on_time = on_cadence = hitches = 0;
            }
        }
        s_last_swap = now;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_QUIT)
        {
            thug_quit(0);
        }
        else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
        {
            thug_quit(0);
        }
        else if (ev.type == SDL_CONTROLLERDEVICEADDED && !s_controller)
        {
            open_controller();
        }
        else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && s_controller &&
                 SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_controller)) == ev.cdevice.which)
        {
            SDL_GameControllerClose(s_controller);
            s_controller = NULL;
            open_controller();
        }
    }
}

void vglEnd(void)
{
    if (s_controller) {
        SDL_GameControllerClose(s_controller);
        s_controller = NULL;
    }
    if (s_gl_ctx)
    {
        close_gl4es();
        SDL_GL_DeleteContext(s_gl_ctx);
        s_gl_ctx = NULL;
    }
    if (s_window)
    {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}

extern "C" void vita_ecran_boot_tot(void)
{
    vglInit(640, 480);
}

extern "C" int vita_ecran_boot(void)
{
    return 0;
}

// --- Controller (sceCtrl) ---
static int s_sampling_mode = SCE_CTRL_MODE_ANALOG_WIDE;

int sceCtrlSetSamplingMode(int mode)
{
    s_sampling_mode = mode;
    return 0;
}

int sceCtrlSetSamplingModeExt(int mode)
{
    s_sampling_mode = mode;
    return 0;
}

// Face buttons by label. PortMaster maps every pad by its printed labels, so
// SDL's A/B/X/Y are the buttons labeled A/B/X/Y on any device. Each action is
// one label, set in thug.cfg: THUG_BTN_OLLIE, THUG_BTN_GRAB, THUG_BTN_FLIP,
// THUG_BTN_GRIND = a, b, x or y. Defaults: ollie B, grab A, flip Y, grind X.
// a, b, x, y gives the positional map of earlier builds.
struct FaceButton {
    SDL_GameControllerButton sdl;
    unsigned int sce;
};

static SDL_GameControllerButton face_label(const char *var, SDL_GameControllerButton def)
{
    const char *v = getenv(var);
    if (!v || !*v) return def;
    switch (v[0] | 0x20) {
    case 'a': return SDL_CONTROLLER_BUTTON_A;
    case 'b': return SDL_CONTROLLER_BUTTON_B;
    case 'x': return SDL_CONTROLLER_BUTTON_X;
    case 'y': return SDL_CONTROLLER_BUTTON_Y;
    }
    printf("[PAD] %s=%s is not a, b, x or y: using the default\n", var, v);
    return def;
}

static const FaceButton *face_buttons_init(FaceButton *t)
{
    static const char kName[] = "ABXY";
    t[0] = { face_label("THUG_BTN_OLLIE", SDL_CONTROLLER_BUTTON_B), SCE_CTRL_CROSS };
    t[1] = { face_label("THUG_BTN_GRAB",  SDL_CONTROLLER_BUTTON_A), SCE_CTRL_CIRCLE };
    t[2] = { face_label("THUG_BTN_FLIP",  SDL_CONTROLLER_BUTTON_Y), SCE_CTRL_SQUARE };
    t[3] = { face_label("THUG_BTN_GRIND", SDL_CONTROLLER_BUTTON_X), SCE_CTRL_TRIANGLE };
    printf("[PAD] face buttons by label: ollie=%c grab=%c flip=%c grind=%c\n",
           kName[t[0].sdl], kName[t[1].sdl], kName[t[2].sdl], kName[t[3].sdl]);
    return t;
}

struct FaceButtons {
    FaceButton t[4];
    const FaceButton *begin() const { return t; }
    const FaceButton *end() const { return t + 4; }
};

static const FaceButtons &face_buttons()
{
    static FaceButtons s;
    static const bool s_init = (face_buttons_init(s.t), true);
    (void)s_init;
    return s;
}

int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count)
{
    return sceCtrlPeekBufferPositiveExt2(port, pad, count);
}

int sceCtrlPeekBufferPositiveExt2(int port, SceCtrlData *pad, int count)
{
    if (!pad || count <= 0) return -1;
    memset(pad, 0, sizeof(SceCtrlData) * count);

    pad->lx = 128;
    pad->ly = 128;
    pad->rx = 128;
    pad->ry = 128;

    SDL_PumpEvents();

    SDL_GameController *gc = s_controller;

    if (gc)
    {
        unsigned int b = 0;
        for (const FaceButton &f : face_buttons())
            if (SDL_GameControllerGetButton(gc, f.sdl))                            b |= f.sce;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))   b |= SCE_CTRL_L1;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))  b |= SCE_CTRL_R1;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))          b |= SCE_CTRL_START;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))           b |= SCE_CTRL_SELECT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))        b |= SCE_CTRL_UP;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))      b |= SCE_CTRL_DOWN;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))      b |= SCE_CTRL_LEFT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))     b |= SCE_CTRL_RIGHT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK))      b |= SCE_CTRL_L3;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK))     b |= SCE_CTRL_R3;

        Sint16 lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 8000) b |= SCE_CTRL_LTRIGGER;
        if (rt > 8000) b |= SCE_CTRL_RTRIGGER;

        // L3 or R3 + Select toggles the FPS meter (the R36S pad seen in the
        // logs reports R3 but no L3). While held, the combo is not passed to
        // the game (Select alone keeps its game action).
        {
            static bool s_combo_prev = false;
            const bool combo = (b & (SCE_CTRL_L3 | SCE_CTRL_R3)) && (b & SCE_CTRL_SELECT);
            if (combo && !s_combo_prev) {
                if (thug_present_active())
                    thug_present_toggle_overlay();
                else
                    printf("[PAD] L3+Select: the FPS meter needs the presenter thread "
                           "(THUG_PRESENTER=1, THUG_PACING_MODE=vsync)\n");
            }
            s_combo_prev = combo;
            if (combo)
                b &= ~(SCE_CTRL_L3 | SCE_CTRL_R3 | SCE_CTRL_SELECT);
        }

        pad->buttons = b;

        Sint16 lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        pad->lx = (unsigned char)(((int)lx + 32768) >> 8);
        pad->ly = (unsigned char)(((int)ly + 32768) >> 8);

        Sint16 rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
        Sint16 ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
        pad->rx = (unsigned char)(((int)rx + 32768) >> 8);
        pad->ry = (unsigned char)(((int)ry + 32768) >> 8);

        // PortMaster hotkey: Select + Start quits
        if ((b & SCE_CTRL_START) && (b & SCE_CTRL_SELECT))
        {
            printf("[PAD] Select+Start pressed: exiting game\n");
            thug_quit(0);
        }
    }
    else
    {
        // Keyboard fallback
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        unsigned int b = 0;
        if (ks[SDL_SCANCODE_SPACE])  b |= SCE_CTRL_CROSS;
        if (ks[SDL_SCANCODE_C])      b |= SCE_CTRL_CIRCLE;
        if (ks[SDL_SCANCODE_X])      b |= SCE_CTRL_SQUARE;
        if (ks[SDL_SCANCODE_Z])      b |= SCE_CTRL_TRIANGLE;
        if (ks[SDL_SCANCODE_RETURN]) b |= SCE_CTRL_START;
        if (ks[SDL_SCANCODE_TAB])    b |= SCE_CTRL_SELECT;
        if (ks[SDL_SCANCODE_UP])     b |= SCE_CTRL_UP;
        if (ks[SDL_SCANCODE_DOWN])   b |= SCE_CTRL_DOWN;
        if (ks[SDL_SCANCODE_LEFT])   b |= SCE_CTRL_LEFT;
        if (ks[SDL_SCANCODE_RIGHT])  b |= SCE_CTRL_RIGHT;
        if (ks[SDL_SCANCODE_Q])      b |= SCE_CTRL_L1;
        if (ks[SDL_SCANCODE_E])      b |= SCE_CTRL_R1;
        pad->buttons = b;
    }

    return count;
}

// --- Audio (sceAudioOut) ---
enum { AUDIO_RATE = 48000, AUDIO_FRAMES = 4096, AUDIO_PORTS = SCE_AUDIO_OUT_PORT_TYPE_MOVIE + 1 };

struct AudioPort {
    int16_t samples[AUDIO_FRAMES][2];
    unsigned read, queued;
    int grain, channels;
    int volume[2];
    bool active;
};

static AudioPort s_audio_ports[AUDIO_PORTS];
static SDL_AudioDeviceID s_audio_device;

static void mix_audio(void *, Uint8 *stream, int bytes)
{
    int16_t *output = reinterpret_cast<int16_t *>(stream);
    for (int frame = 0; frame < bytes / (2 * static_cast<int>(sizeof(int16_t))); ++frame) {
        int mixed[2] = { 0, 0 };
        for (int id = 0; id < AUDIO_PORTS; ++id) {
            AudioPort &port = s_audio_ports[id];
            if (!port.queued)
                continue;
            for (int ch = 0; ch < 2; ++ch)
                mixed[ch] += port.samples[port.read][ch] * port.volume[ch] / SCE_AUDIO_VOLUME_0DB;
            port.read = (port.read + 1) % AUDIO_FRAMES;
            --port.queued;
        }
        for (int ch = 0; ch < 2; ++ch)
            *output++ = std::min(32767, std::max(-32768, mixed[ch]));
    }
}

int sceAudioOutOpenPort(int type, int len, int freq, int mode)
{
    if (type < 0 || type >= AUDIO_PORTS || len <= 0 || len > AUDIO_FRAMES ||
        freq != AUDIO_RATE)
        return -1;
    if (!s_audio_device) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "[AUDIO] %s\n", SDL_GetError());
            return -1;
        }
        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.freq = AUDIO_RATE;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 512;
        spec.callback = mix_audio;
        s_audio_device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
        if (!s_audio_device) {
            fprintf(stderr, "[AUDIO] %s\n", SDL_GetError());
            return -1;
        }
        SDL_PauseAudioDevice(s_audio_device, 0);
        printf("[AUDIO] driver=%s rate=%d channels=2\n", SDL_GetCurrentAudioDriver(), AUDIO_RATE);
    }

    SDL_LockAudioDevice(s_audio_device);
    AudioPort &port = s_audio_ports[type];
    port.read = port.queued = 0;
    port.grain = len;
    port.channels = mode == SCE_AUDIO_OUT_MODE_MONO ? 1 : 2;
    port.volume[0] = port.volume[1] = SCE_AUDIO_VOLUME_0DB;
    port.active = true;
    SDL_UnlockAudioDevice(s_audio_device);
    return type;
}

int sceAudioOutClosePort(int id)
{
    if (id < 0 || id >= AUDIO_PORTS || !s_audio_device)
        return -1;
    SDL_LockAudioDevice(s_audio_device);
    s_audio_ports[id].active = false;
    s_audio_ports[id].queued = 0;
    SDL_UnlockAudioDevice(s_audio_device);
    return 0;
}

int sceAudioOutOutput(int id, const void *buffer)
{
    if (id < 0 || id >= AUDIO_PORTS || !s_audio_device || !buffer)
        return -1;
    const int16_t *input = static_cast<const int16_t *>(buffer);
    AudioPort &port = s_audio_ports[id];
    for (;;) {
        SDL_LockAudioDevice(s_audio_device);
        if (!port.active) {
            SDL_UnlockAudioDevice(s_audio_device);
            return -1;
        }
        // Match the blocking Vita API and keep latency to one queued grain.
        if (!port.queued)
            break;
        SDL_UnlockAudioDevice(s_audio_device);
        SDL_Delay(1);
    }
    for (int frame = 0; frame < port.grain; ++frame) {
        const unsigned slot = (port.read + frame) % AUDIO_FRAMES;
        port.samples[slot][0] = input[frame * port.channels];
        port.samples[slot][1] = input[frame * port.channels + port.channels - 1];
    }
    port.queued = port.grain;
    SDL_UnlockAudioDevice(s_audio_device);
    return 0;
}

int sceAudioOutSetVolume(int id, SceAudioOutChannelFlag channels, int *volume)
{
    if (id < 0 || id >= AUDIO_PORTS || !s_audio_device || !volume)
        return -1;
    SDL_LockAudioDevice(s_audio_device);
    if (channels & SCE_AUDIO_VOLUME_FLAG_L_CH)
        s_audio_ports[id].volume[0] = std::min(SCE_AUDIO_VOLUME_0DB, std::max(0, volume[0]));
    if (channels & SCE_AUDIO_VOLUME_FLAG_R_CH)
        s_audio_ports[id].volume[1] = std::min(SCE_AUDIO_VOLUME_0DB, std::max(0, volume[1]));
    SDL_UnlockAudioDevice(s_audio_device);
    return 0;
}

int sceAudioOutGetRestSample(int id)
{
    if (id < 0 || id >= AUDIO_PORTS || !s_audio_device)
        return 0;
    SDL_LockAudioDevice(s_audio_device);
    const int queued = s_audio_ports[id].queued;
    SDL_UnlockAudioDevice(s_audio_device);
    return queued;
}

// --- POSIX Filesystem Shims (sceIo*) ---
static char s_data_root[512] = "";

static const char *get_data_root()
{
    if (s_data_root[0] != '\0') return s_data_root;

    const char *env = getenv("THUG_DATA_DIR");
    if (env && access(env, F_OK) == 0)
    {
        strncpy(s_data_root, env, sizeof(s_data_root)-1);
        return s_data_root;
    }

    if (access("Data", F_OK) == 0) strcpy(s_data_root, "Data");
    else if (access("data", F_OK) == 0) strcpy(s_data_root, "data");
    else if (access("gamedata/Data", F_OK) == 0) strcpy(s_data_root, "gamedata/Data");
    else if (access("gamedata/data", F_OK) == 0) strcpy(s_data_root, "gamedata/data");
    else strcpy(s_data_root, "Data");

    return s_data_root;
}

static const char *map_path(const char *in, char *out, size_t out_len)
{
    if (!in) return NULL;

    const char *prefix_data = "ux0:data/thug/Data";
    const char *prefix_data_slash = "ux0:data/thug/Data/";
    const char *prefix_save = "ux0:data/thug/save";
    const char *prefix_save_slash = "ux0:data/thug/save/";
    const char *prefix_app = "app0:";

    if (strncmp(in, prefix_data_slash, strlen(prefix_data_slash)) == 0)
    {
        snprintf(out, out_len, "%s/%s", get_data_root(), in + strlen(prefix_data_slash));
    }
    else if (strcmp(in, prefix_data) == 0)
    {
        snprintf(out, out_len, "%s", get_data_root());
    }
    else if (strncmp(in, prefix_save_slash, strlen(prefix_save_slash)) == 0)
    {
        mkdir("save", 0777);
        snprintf(out, out_len, "save/%s", in + strlen(prefix_save_slash));
    }
    else if (strcmp(in, prefix_save) == 0)
    {
        mkdir("save", 0777);
        snprintf(out, out_len, "save");
    }
    else if (strncmp(in, prefix_app, strlen(prefix_app)) == 0)
    {
        snprintf(out, out_len, "./%s", in + strlen(prefix_app));
    }
    else if (strncmp(in, "ux0:data/thug/", sizeof("ux0:data/thug/") - 1) == 0)
    {
        snprintf(out, out_len, "./%s", in + sizeof("ux0:data/thug/") - 1);
    }
    else
    {
        strncpy(out, in, out_len - 1);
        out[out_len - 1] = '\0';
    }

    // Convert backslashes
    for (char *c = out; *c; ++c)
    {
        if (*c == '\\') *c = '/';
    }

    return out;
}

// Xbox paths are case-insensitive. Resolve missing components on Linux filesystems.
static bool resolve_asset_case(const char *original, char *path)
{
    const char prefix[] = "ux0:data/thug/Data/";
    if (strncmp(original, prefix, sizeof(prefix) - 1) != 0)
        return false;

    const size_t root_length = strlen(get_data_root());
    if (strlen(path) <= root_length || path[root_length] != '/')
        return false;
    char *component = path + root_length + 1;
    while (*component) {
        char *slash = strchr(component, '/');
        if (slash) *slash = '\0';
        if (access(path, F_OK) != 0) {
            component[-1] = '\0';
            DIR *parent = opendir(path);
            component[-1] = '/';
            if (!parent)
                return false;
            bool found = false;
            while (struct dirent *entry = readdir(parent)) {
                if (strcasecmp(entry->d_name, component) == 0) {
                    memcpy(component, entry->d_name, strlen(component));
                    found = true;
                    break;
                }
            }
            closedir(parent);
            if (!found)
                return false;
        }
        if (!slash)
            return true;
        *slash = '/';
        component = slash + 1;
    }
    return true;
}

SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
    char path[512];
    map_path(file, path, sizeof(path));

    int pflags = 0;
    if ((flags & SCE_O_RDWR) == SCE_O_RDWR) pflags |= O_RDWR;
    else if (flags & SCE_O_WRONLY)           pflags |= O_WRONLY;
    else                                     pflags |= O_RDONLY;

    if (flags & SCE_O_CREAT)  pflags |= O_CREAT;
    if (flags & SCE_O_TRUNC)  pflags |= O_TRUNC;
    if (flags & SCE_O_APPEND) pflags |= O_APPEND;

    int fd = open(path, pflags, mode ? mode : 0666);
    if (fd < 0 && errno == ENOENT && pflags == O_RDONLY && resolve_asset_case(file, path))
        fd = open(path, pflags);
    return (SceUID)fd;
}

int sceIoClose(SceUID fd)
{
    return close((int)fd);
}

int sceIoRead(SceUID fd, void *data, SceSize size)
{
    return read((int)fd, data, size);
}

int sceIoWrite(SceUID fd, const void *data, SceSize size)
{
    return write((int)fd, data, size);
}

SceOff sceIoLseek(SceUID fd, SceOff offset, int whence)
{
    int pwhence = SEEK_SET;
    if (whence == SCE_SEEK_CUR) pwhence = SEEK_CUR;
    else if (whence == SCE_SEEK_END) pwhence = SEEK_END;
    return lseek((int)fd, offset, pwhence);
}

int sceIoRemove(const char *file)
{
    char path[512];
    map_path(file, path, sizeof(path));
    return unlink(path);
}

int sceIoMkdir(const char *dir, SceMode mode)
{
    char path[512];
    map_path(dir, path, sizeof(path));
    return mkdir(path, mode ? mode : 0777);
}

int sceIoRmdir(const char *dir)
{
    char path[512];
    map_path(dir, path, sizeof(path));
    return rmdir(path);
}

int sceIoGetstat(const char *file, SceIoStat *stat_out)
{
    char path[512];
    map_path(file, path, sizeof(path));

    struct stat st;
    int r = stat(path, &st);
    if (r < 0 && errno == ENOENT && resolve_asset_case(file, path))
        r = stat(path, &st);
    if (r < 0) return r;

    if (stat_out)
    {
        memset(stat_out, 0, sizeof(SceIoStat));
        stat_out->st_size = st.st_size;
        stat_out->st_mode = 0;
        if (S_ISDIR(st.st_mode)) stat_out->st_mode |= SCE_S_IFDIR;
        if (S_ISREG(st.st_mode)) stat_out->st_mode |= SCE_S_IFREG;

        time_t mt = st.st_mtim.tv_sec;
        struct tm time_info;
        struct tm *mti = localtime_r(&mt, &time_info);
        if (mti)
        {
            stat_out->st_mtime.year = mti->tm_year + 1900;
            stat_out->st_mtime.month = mti->tm_mon + 1;
            stat_out->st_mtime.day = mti->tm_mday;
            stat_out->st_mtime.hour = mti->tm_hour;
            stat_out->st_mtime.minute = mti->tm_min;
            stat_out->st_mtime.second = mti->tm_sec;
        }
    }
    return 0;
}

// Directory handling table
#define MAX_OPEN_DIRS 32
static DIR *s_open_dirs[MAX_OPEN_DIRS] = { NULL };

SceUID sceIoDopen(const char *dirname)
{
    char path[512];
    map_path(dirname, path, sizeof(path));

    DIR *d = opendir(path);
    if (!d && errno == ENOENT && resolve_asset_case(dirname, path))
        d = opendir(path);
    if (!d) return -1;

    for (int i = 0; i < MAX_OPEN_DIRS; i++)
    {
        if (!s_open_dirs[i])
        {
            s_open_dirs[i] = d;
            return (SceUID)(i + 1);
        }
    }
    closedir(d);
    return -1;
}

int sceIoDread(SceUID fd, SceIoDirent *dir)
{
    int idx = (int)fd - 1;
    if (idx < 0 || idx >= MAX_OPEN_DIRS || !s_open_dirs[idx]) return -1;

    struct dirent *de = readdir(s_open_dirs[idx]);
    if (!de) return 0;

    if (dir)
    {
        memset(dir, 0, sizeof(SceIoDirent));
        strncpy(dir->d_name, de->d_name, sizeof(dir->d_name) - 1);
    }
    return 1;
}

int sceIoDclose(SceUID fd)
{
    int idx = (int)fd - 1;
    if (idx < 0 || idx >= MAX_OPEN_DIRS || !s_open_dirs[idx]) return -1;

    closedir(s_open_dirs[idx]);
    s_open_dirs[idx] = NULL;
    return 0;
}

// --- Threads & Mutexes ---
struct SceThreadWrapper {
    pthread_t thread;
    SceKernelThreadEntry entry;
    SceSize args;
    void *argp;
    int ret;
    bool joined;
};

#define MAX_THREADS 64
static SceThreadWrapper s_threads[MAX_THREADS];
static bool s_threads_inited = false;

static void *thread_start_routine(void *arg)
{
    SceThreadWrapper *w = (SceThreadWrapper *)arg;
    w->ret = w->entry(w->args, w->argp);
    return NULL;
}

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, int stackSize, unsigned int attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
    if (!s_threads_inited)
    {
        memset(s_threads, 0, sizeof(s_threads));
        s_threads_inited = true;
    }

    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (s_threads[i].entry == NULL)
        {
            s_threads[i].entry = entry;
            s_threads[i].joined = false;
            return (SceUID)(i + 1);
        }
    }
    return -1;
}

int sceKernelStartThread(SceUID thid, SceSize args, void *argp)
{
    int idx = (int)thid - 1;
    if (idx < 0 || idx >= MAX_THREADS || !s_threads[idx].entry) return -1;

    s_threads[idx].args = args;
    s_threads[idx].argp = argp;
    const int result = pthread_create(&s_threads[idx].thread, NULL, thread_start_routine, &s_threads[idx]);
    return -result;
}

int sceKernelWaitThreadEnd(SceUID thid, int *stat, void *timeout)
{
    int idx = (int)thid - 1;
    if (idx < 0 || idx >= MAX_THREADS || !s_threads[idx].entry) return -1;

    if (!s_threads[idx].joined)
    {
        pthread_join(s_threads[idx].thread, NULL);
        s_threads[idx].joined = true;
    }
    if (stat) *stat = s_threads[idx].ret;
    return 0;
}

int sceKernelDeleteThread(SceUID thid)
{
    int idx = (int)thid - 1;
    if (idx < 0 || idx >= MAX_THREADS) return -1;
    memset(&s_threads[idx], 0, sizeof(SceThreadWrapper));
    return 0;
}

#define MAX_MUTEXES 128
static pthread_mutex_t s_mutexes[MAX_MUTEXES];
static bool s_mutex_used[MAX_MUTEXES] = { false };

SceUID sceKernelCreateMutex(const char *name, unsigned int attr, int initCount, const SceKernelMutexOptParam *option)
{
    for (int i = 0; i < MAX_MUTEXES; i++)
    {
        if (!s_mutex_used[i])
        {
            pthread_mutex_init(&s_mutexes[i], NULL);
            s_mutex_used[i] = true;
            return (SceUID)(i + 1);
        }
    }
    return -1;
}

int sceKernelLockMutex(SceUID mutexid, int count, unsigned int *timeout)
{
    int idx = (int)mutexid - 1;
    if (idx < 0 || idx >= MAX_MUTEXES || !s_mutex_used[idx]) return -1;
    return pthread_mutex_lock(&s_mutexes[idx]);
}

int sceKernelUnlockMutex(SceUID mutexid, int count)
{
    int idx = (int)mutexid - 1;
    if (idx < 0 || idx >= MAX_MUTEXES || !s_mutex_used[idx]) return -1;
    return pthread_mutex_unlock(&s_mutexes[idx]);
}

int sceKernelDeleteMutex(SceUID mutexid)
{
    int idx = (int)mutexid - 1;
    if (idx < 0 || idx >= MAX_MUTEXES || !s_mutex_used[idx]) return -1;
    pthread_mutex_destroy(&s_mutexes[idx]);
    s_mutex_used[idx] = false;
    return 0;
}

// The render workers use binary semaphores to hand off immutable draw lists.
enum { MAX_SEMAPHORES = 16 };
static SDL_sem *s_semaphores[MAX_SEMAPHORES];

SceUID sceKernelCreateSema(const char *, unsigned int, int initial, int maximum, const SceKernelSemaOptParam *)
{
    if (maximum != 1 || initial < 0 || initial > maximum)
        return -1;
    for (int index = 0; index < MAX_SEMAPHORES; ++index) {
        if (!s_semaphores[index]) {
            s_semaphores[index] = SDL_CreateSemaphore(initial);
            return s_semaphores[index] ? index + 1 : -1;
        }
    }
    return -1;
}

int sceKernelWaitSema(SceUID id, int count, unsigned int *timeout)
{
    if (id <= 0 || id > MAX_SEMAPHORES || count != 1 || !s_semaphores[id - 1])
        return -1;
    const int result = timeout
        ? SDL_SemWaitTimeout(s_semaphores[id - 1], *timeout / 1000 + (*timeout % 1000 != 0))
        : SDL_SemWait(s_semaphores[id - 1]);
    return result == 0 ? 0 : -1;
}

int sceKernelSignalSema(SceUID id, int count)
{
    if (id <= 0 || id > MAX_SEMAPHORES || count != 1 || !s_semaphores[id - 1])
        return -1;
    return SDL_SemPost(s_semaphores[id - 1]);
}

int sceKernelDeleteSema(SceUID id)
{
    if (id <= 0 || id > MAX_SEMAPHORES || !s_semaphores[id - 1])
        return -1;
    SDL_DestroySemaphore(s_semaphores[id - 1]);
    s_semaphores[id - 1] = NULL;
    return 0;
}

// --- Debug Server Stubs ---
void vita_dbgsrv_start(void) {}
void vita_dbgsrv_frame(void) {}
int vita_dbgsrv_take_command(char *out, int size) { return 0; }
int vita_dbgsrv_pad(VitaDbgPad *out) { return 0; }
void vita_dbgsrv_set_busy(int busy) {}

extern "C" uint8_t *vgl_reserve_data_pool(uint32_t size)
{
    static uint8_t s_dummy[128];
    return s_dummy;
}

// --- Fallback skinning and spy stubs ---
namespace NxVita
{
    bool g_vita_vpr = false;
    bool g_vita_gxm_peau = false;
    int g_vita_gp_reutil[2] = { 0, 0 };

    bool GxmPeauPret() { return false; }
    bool GxmPeauEclaireePret() { return false; }
    void GxmPeauDebut(const float *mvp, const float *p_os, int num_os, bool remplir_os, const float *params) {}
    void GxmPeauPiece(unsigned int vbo_repos, unsigned int vbo_poids, unsigned int vbo_os,
                      unsigned int uvbo, void *indices, bool clippe, const float *couleur,
                      unsigned int nb_indices, int prim, unsigned int cbo) {}
    void GxmPeauFin() {}
}

extern "C" {
    int vita_glspy_ouverte = 0;
    int vita_glspy_phase = 0;
    void vita_glspy_bilan(void) {}

    void vita_memspy_niveau(const char *nom) {}

    int vita_prof_enable = 0;
    void vita_prof_dump(void) {}
}
