// Sampling CPU profiler for the R36S (THUG_PROFILE=1, off by default).
//
// SIGPROF every millisecond of process CPU time; the handler records the
// interrupted program counter, the link register (the caller, for leaf
// functions) and the thread id. On exit (Select + Start) it writes, next to
// log.txt:
//   prof_samples.txt  "<pc hex> <tid> <lr hex>" per sample
//   prof_maps.txt     /proc/self/maps (the binary is position-independent)
//   prof_threads.txt  "<tid> <name>"
// Read on the PC with build-armhf/host-tools/prof-report.py against the
// unstripped binary of the same build.
// C++98, async-signal-safe handler (no allocation, no locks).

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __arm__
#include <ucontext.h>
#endif
#include <dirent.h>

namespace {

const int kMax = 600000;
uint32_t s_pc[kMax];
uint32_t s_lr[kMax];
uint16_t s_tid[kMax];
volatile int s_n = 0;
bool s_on = false;

void on_prof(int, siginfo_t *, void *ctx)
{
    const int saved = errno;
    const int i = __sync_fetch_and_add(&s_n, 1);
    if (i < kMax) {
#ifdef __arm__
        const ucontext_t *u = static_cast<const ucontext_t *>(ctx);
        s_pc[i] = (uint32_t)u->uc_mcontext.arm_pc;
        s_lr[i] = (uint32_t)u->uc_mcontext.arm_lr;
#else
        (void)ctx;
        s_pc[i] = 0;
        s_lr[i] = 0;
#endif
        s_tid[i] = (uint16_t)syscall(SYS_gettid);
    }
    errno = saved;
}

void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "r");
    FILE *out = fopen(to, "w");
    if (in && out) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            fwrite(buf, 1, n, out);
    }
    if (in) fclose(in);
    if (out) fclose(out);
}

void dump()
{
    if (!s_on)
        return;
    s_on = false;
    struct itimerval off;
    memset(&off, 0, sizeof(off));
    setitimer(ITIMER_PROF, &off, NULL);
    const int n = s_n < kMax ? s_n : kMax;
    FILE *f = fopen("prof_samples.txt", "w");
    if (f) {
        for (int i = 0; i < n; ++i)
            fprintf(f, "%08x %u %08x\n", s_pc[i], (unsigned)s_tid[i], s_lr[i]);
        fclose(f);
    }
    copy_file("/proc/self/maps", "prof_maps.txt");
    FILE *t = fopen("prof_threads.txt", "w");
    if (t) {
        DIR *d = opendir("/proc/self/task");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] < '0' || e->d_name[0] > '9')
                    continue;
                char path[64], name[64] = "";
                snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
                FILE *c = fopen(path, "r");
                if (c) {
                    if (fgets(name, sizeof(name), c))
                        name[strcspn(name, "\n")] = 0;
                    fclose(c);
                }
                fprintf(t, "%s %s\n", e->d_name, name);
            }
            closedir(d);
        }
        fclose(t);
    }
    printf("[PROF] profiler: %d samples written to prof_samples.txt (%d dropped)\n",
           n, s_n > kMax ? s_n - kMax : 0);
}

} // namespace

void thug_profile_start()
{
    const char *e = getenv("THUG_PROFILE");
    if (!e || strcmp(e, "1") != 0)
        return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0)
        return;
    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_PROF, &it, NULL) != 0)
        return;
    s_on = true;
    atexit(dump);
    printf("[PROF] profiler on: SIGPROF every 1 ms of CPU time, written at exit (THUG_PROFILE=1)\n");
}
