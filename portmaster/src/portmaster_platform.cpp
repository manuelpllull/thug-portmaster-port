/*****************************************************************************
**  THUG PortMaster / Linux R36S Platform Harness                           **
**  portmaster/src/portmaster_platform.cpp                                  **
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>

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

// --- Runtime Globals ---
unsigned int vgl_framecount = 0;
bool g_hasJustEnteredNetworkGame = false;
int sceLibcHeapSize = 4 * 1024 * 1024;
int _newlib_heap_size_user = 192 * 1024 * 1024;
int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// --- Logging ---
static FILE *s_log_file = NULL;

void vita_log_init(void)
{
    if (!s_log_file)
    {
        s_log_file = fopen("log.txt", "a");
        if (!s_log_file) s_log_file = stdout;
    }
    printf("[SYS] THUG PortMaster Linux R36S Initialized\n");
    if (s_log_file && s_log_file != stdout)
    {
        fprintf(s_log_file, "[SYS] THUG PortMaster Linux R36S Initialized\n");
        fflush(s_log_file);
    }
}

void vita_log_shutdown(void)
{
    if (s_log_file && s_log_file != stdout)
    {
        fclose(s_log_file);
        s_log_file = NULL;
    }
}

void vita_log_start_watchdog(void) {}

void vita_log_flush(void)
{
    if (s_log_file) fflush(s_log_file);
    fflush(stdout);
}

void vita_log_printf(const char *sys, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[%s] %s\n", sys ? sys : "LOG", buf);
    if (s_log_file && s_log_file != stdout)
    {
        fprintf(s_log_file, "[%s] %s\n", sys ? sys : "LOG", buf);
        fflush(s_log_file);
    }
}

int vita_log_ring_read(unsigned int *pos, char *out, int max)
{
    return 0;
}

// Vita_OutputDebugString in compat.cpp

// --- SDL2 Video / Window / OpenGL ---
static SDL_Window *s_window = NULL;
static SDL_GLContext s_gl_ctx = NULL;
static bool s_vgl_inited = false;

extern "C" void vita_vgl_init_tot(void);

int vglInitExtended(int flags, int width, int height, int ram_sz, int msaa)
{
    if (s_vgl_inited) return 0;
    s_vgl_inited = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0)
    {
        printf("[SDL] Failed to init SDL: %s\n", SDL_GetError());
        return -1;
    }

    // Try loading gamecontrollerdb.txt
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Default 640x480 resolution for R36S native display
    int screen_w = 640;
    int screen_h = 480;

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    s_window = SDL_CreateWindow("Tony Hawk's Underground", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screen_w, screen_h, win_flags);
    if (!s_window)
    {
        printf("[SDL] Failed to create window: %s\n", SDL_GetError());
        return -1;
    }

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx)
    {
        printf("[SDL] Failed to create GL context: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);

    printf("[GL] Initialized: %s, Renderer: %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // Open first connected gamepad
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            SDL_GameControllerOpen(i);
            printf("[PAD] GameController %d connected: %s\n", i, SDL_GameControllerNameForIndex(i));
            break;
        }
    }

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
        SDL_GL_SwapWindow(s_window);
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_QUIT)
        {
            exit(0);
        }
        else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
        {
            exit(0);
        }
    }
}

void vglEnd(void)
{
    if (s_gl_ctx)
    {
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

int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count)
{
    return sceCtrlPeekBufferPositiveExt2(port, pad, count);
}

int sceCtrlPeekBufferPositiveExt2(int port, SceCtrlData *pad, int count)
{
    if (!pad) return -1;
    memset(pad, 0, sizeof(SceCtrlData) * count);

    pad->lx = 128;
    pad->ly = 128;
    pad->rx = 128;
    pad->ry = 128;

    SDL_PumpEvents();

    SDL_GameController *gc = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            gc = SDL_GameControllerFromInstanceID(i);
            if (!gc) gc = SDL_GameControllerOpen(i);
            if (gc) break;
        }
    }

    if (gc)
    {
        unsigned int b = 0;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))              b |= SCE_CTRL_CROSS;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B))              b |= SCE_CTRL_CIRCLE;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X))              b |= SCE_CTRL_SQUARE;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y))              b |= SCE_CTRL_TRIANGLE;
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
            exit(0);
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
struct AudioPort {
    SDL_AudioDeviceID dev;
    int grain;
    int freq;
    int channels;
    bool active;
};

static AudioPort s_audio_ports[3] = { {0}, {0}, {0} };

int sceAudioOutOpenPort(int type, int len, int freq, int mode)
{
    if (type < 0 || type >= 3) return -1;
    AudioPort *p = &s_audio_ports[type];
    if (p->active) return type;

    SDL_AudioSpec wanted, obtained;
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = freq;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = (mode == SCE_AUDIO_OUT_MODE_MONO ? 1 : 2);
    wanted.samples = len;
    wanted.callback = NULL;

    p->dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (p->dev == 0)
    {
        printf("[SND] Failed to open audio device for port %d: %s\n", type, SDL_GetError());
        return -1;
    }

    p->grain = len;
    p->freq = freq;
    p->channels = wanted.channels;
    p->active = true;

    SDL_PauseAudioDevice(p->dev, 0);
    return type;
}

int sceAudioOutClosePort(int port)
{
    if (port < 0 || port >= 3) return -1;
    AudioPort *p = &s_audio_ports[port];
    if (p->active)
    {
        SDL_CloseAudioDevice(p->dev);
        p->active = false;
        p->dev = 0;
    }
    return 0;
}

int sceAudioOutOutput(int port, const void *buf)
{
    if (port < 0 || port >= 3) return -1;
    AudioPort *p = &s_audio_ports[port];
    if (!p->active || !buf) return -1;

    Uint32 bytes = p->grain * p->channels * sizeof(int16_t);
    SDL_QueueAudio(p->dev, buf, bytes);

    // Throttle if queue grows beyond 4 buffers to pace the audio thread
    if (SDL_GetQueuedAudioSize(p->dev) > bytes * 4)
    {
        SDL_Delay(3);
    }
    return 0;
}

int sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol)
{
    return 0;
}

int sceAudioOutGetRestSample(int port)
{
    if (port < 0 || port >= 3) return 0;
    AudioPort *p = &s_audio_ports[port];
    if (!p->active) return 0;

    Uint32 queued_bytes = SDL_GetQueuedAudioSize(p->dev);
    return queued_bytes / (p->channels * sizeof(int16_t));
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
    if (r < 0) return r;

    if (stat_out)
    {
        memset(stat_out, 0, sizeof(SceIoStat));
        stat_out->st_size = st.st_size;
        stat_out->st_mode = 0;
        if (S_ISDIR(st.st_mode)) stat_out->st_mode |= SCE_S_IFDIR;
        if (S_ISREG(st.st_mode)) stat_out->st_mode |= SCE_S_IFREG;

        time_t mt = st.st_mtim.tv_sec;
        struct tm *mti = localtime(&mt);
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
    return pthread_create(&s_threads[idx].thread, NULL, thread_start_routine, &s_threads[idx]);
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
    void vita_glspy_phase(int p) {}
    void vita_glspy_bilan(void) {}

    void vita_memspy_niveau(const char *nom) {}

    int vita_prof_enable = 0;
    void vita_prof_dump(void) {}
}
