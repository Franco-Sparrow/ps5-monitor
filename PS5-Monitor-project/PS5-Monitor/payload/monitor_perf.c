#include "monitor_perf.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#define SOC_CLOCK_COUNT 26
#define SOC_CLOCK_GFXCLK 20
#define FAN_DUTY_SCALE 1024.0


#define DCE_FPS_DEVICE "/dev/dce"
#define DCE_FPS_IOCTL_REQUEST ((unsigned long)0x80308217UL)
#define DCE_FPS_SELECTOR 0x10000000AULL
#define DCE_FPS_MASK 0x8000000000ULL
#define DCE_FPS_WORDS 12
#define DCE_FPS_COUNTER_WORD 1
#define DCE_FPS_MIN 1.0
#define DCE_FPS_MAX 240.0

#define MSGBUF_FALLBACK_SIZE (256u * 1024u)
#define MSGBUF_MAX_SIZE      (512u * 1024u)
#define SHELL_HOME_APP_ID    0x00000007u

/* Extra metrics retained from the previous standalone telemetry payload.
 * Core CPU, temperature and system-memory collectors are not duplicated here. */
int sceKernelGetSocClock(uint32_t *out_raw_domains);
int sceKernelGetCurrentFanDuty(uint16_t *out_duty, uint64_t *out_chassis_info);

/* App information export used only for GAME/OS context detection. */
typedef struct ps5_monitor_app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} ps5_monitor_app_info_t;
extern int sceKernelGetAppInfo(pid_t pid, ps5_monitor_app_info_t *info);

typedef struct dce_fps_ioctl_arg {
    uint64_t selector;
    uint64_t mask;
    uint64_t output;
    uint64_t reserved[3];
} dce_fps_ioctl_arg_t;

typedef struct fps_state {
    int game_active;
    int have_baseline;
    uint64_t previous_counter;
    struct timespec previous_time;
    int have_last_good;
    unsigned last_good_fps;
    struct timespec last_good_time;
} fps_state_t;


typedef struct shell_state {
    uint32_t game_app_id;
    uint32_t controller_focus;
    int have_game_app_id;
    int have_controller_focus;
    char shell_scene[96];
} shell_state_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static fps_state_t g_fps;
static ps5_monitor_perf_snapshot_t g_cache;
static struct timespec g_cache_time;
static int g_cache_valid = 0;
static struct timespec g_context_time;
static ps5_monitor_perf_snapshot_t g_context_cache;
static int g_context_valid = 0;

static uint64_t delta_ms(const struct timespec *a, const struct timespec *b)
{
    int64_t sec = (int64_t)b->tv_sec - (int64_t)a->tv_sec;
    int64_t ns = (int64_t)b->tv_nsec - (int64_t)a->tv_nsec;
    if (ns < 0) { --sec; ns += 1000000000LL; }
    if (sec < 0) return 0;
    return (uint64_t)sec * 1000ULL + (uint64_t)ns / 1000000ULL;
}

const char *ps5_monitor_foreground_name(ps5_monitor_foreground_kind_t kind)
{
    switch (kind) {
        case PS5_MONITOR_FOREGROUND_OS: return "OS";
        case PS5_MONITOR_FOREGROUND_GAME: return "GAME";
        default: return "UNKNOWN";
    }
}

static int is_game_title_id(const char *title_id)
{
    return title_id && (!strncmp(title_id, "PPSA", 4) || !strncmp(title_id, "CUSA", 4));
}

static int find_game_process(ps5_monitor_perf_snapshot_t *s)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t sz = 0;
    uint8_t *buf = NULL;
    pid_t self = getpid();
    int best_pid = 0;
    char best_title[16] = {0};

    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || sz == 0) return -1;
    buf = malloc(sz);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) != 0) { free(buf); return -1; }

    for (uint8_t *p = buf; p < buf + sz;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        ps5_monitor_app_info_t ai;
        if (ki->ki_structsize == 0 || p + ki->ki_structsize > buf + sz) break;
        p += ki->ki_structsize;
        if (ki->ki_pid <= 0 || ki->ki_pid == self) continue;
        memset(&ai, 0, sizeof(ai));
        if (sceKernelGetAppInfo(ki->ki_pid, &ai) != 0) continue;
        ai.title_id[sizeof(ai.title_id) - 1] = '\0';
        if (!is_game_title_id(ai.title_id)) continue;
        if (ki->ki_pid > best_pid) {
            best_pid = ki->ki_pid;
            snprintf(best_title, sizeof(best_title), "%s", ai.title_id);
        }
    }
    free(buf);

    if (best_pid <= 0) return 0;
    s->game_resident = 1;
    s->have_game_pid = 1;
    s->game_pid = (uint32_t)best_pid;
    snprintf(s->game_title_id, sizeof(s->game_title_id), "%s", best_title);
    return 1;
}

static int read_msgbuf(char **out, size_t *out_len)
{
    size_t size = 0;
    char *buf;
    if (!out || !out_len) return -1;
    *out = NULL; *out_len = 0;
    if (sysctlbyname("kern.msgbuf", NULL, &size, NULL, 0) != 0 || size == 0)
        size = MSGBUF_FALLBACK_SIZE;
    if (size > MSGBUF_MAX_SIZE) size = MSGBUF_MAX_SIZE;
    buf = calloc(1, size + 1);
    if (!buf) return -1;
    if (sysctlbyname("kern.msgbuf", buf, &size, NULL, 0) != 0) { free(buf); return -1; }
    buf[size] = '\0';
    *out = buf; *out_len = size;
    return 0;
}

static int parse_u32(const char *p, int base, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!p || !out) return 0;
    errno = 0;
    v = strtoul(p, &end, base);
    if (errno || end == p) return 0;
    *out = (uint32_t)v;
    return 1;
}

static void copy_scene(char dst[96], const char *start)
{
    const char *end = strchr(start, ']');
    size_t n = (end && end > start) ? (size_t)(end - start) : strlen(start);
    if (n > 95) n = 95;
    memcpy(dst, start, n); dst[n] = '\0';
}

static void parse_msgbuf(const char *buf, size_t len, uint32_t game_pid, shell_state_t *st)
{
    const char *cur = buf;
    const char *end = buf + len;
    memset(st, 0, sizeof(*st));
    while (cur < end && *cur) {
        const char *nl = memchr(cur, '\n', (size_t)(end - cur));
        size_t ln = nl ? (size_t)(nl - cur) : strlen(cur);
        char tmp[512];
        if (ln >= sizeof(tmp)) ln = sizeof(tmp) - 1;
        memcpy(tmp, cur, ln); tmp[ln] = '\0';

        const char *p = strstr(tmp, "VideoOut: shared (pid=0x");
        if (p) {
            uint32_t pid = 0, app = 0;
            p += strlen("VideoOut: shared (pid=0x");
            const char *ap = strstr(p, "appId=0x");
            if (parse_u32(p, 16, &pid) && ap && parse_u32(ap + 8, 16, &app) && pid == game_pid) {
                st->game_app_id = app; st->have_game_app_id = 1;
            }
        }
        p = strstr(tmp, "SetControllerFocus(");
        if (p) {
            uint32_t focus = 0; p += strlen("SetControllerFocus(");
            if (*p != '-' && parse_u32(p, 0, &focus)) {
                st->controller_focus = focus; st->have_controller_focus = 1;
            }
        }
        p = strstr(tmp, "OnFocusActiveSceneChanged");
        if (p) {
            const char *arrow = strstr(tmp, "-> [");
            if (arrow) copy_scene(st->shell_scene, arrow + 4);
        }
        if (!nl) break;
        cur = nl + 1;
    }
}

static int contains(const char *s, const char *needle)
{
    return s && s[0] && needle && strstr(s, needle) != NULL;
}

static void detect_context(ps5_monitor_perf_snapshot_t *s)
{
    char *msg = NULL;
    size_t msg_len = 0;
    shell_state_t st;
    int r = find_game_process(s);
    if (r < 0) { s->foreground = PS5_MONITOR_FOREGROUND_UNKNOWN; return; }
    if (r == 0) { s->foreground = PS5_MONITOR_FOREGROUND_OS; return; }
    if (read_msgbuf(&msg, &msg_len) != 0 || !msg) {
        s->foreground = PS5_MONITOR_FOREGROUND_UNKNOWN; return;
    }
    parse_msgbuf(msg, msg_len, s->game_pid, &st);
    free(msg);

    if (st.have_game_app_id && st.have_controller_focus &&
        st.game_app_id != 0 && st.controller_focus == st.game_app_id) {
        s->foreground = PS5_MONITOR_FOREGROUND_GAME;
    } else if (st.have_controller_focus && st.controller_focus == SHELL_HOME_APP_ID) {
        s->foreground = PS5_MONITOR_FOREGROUND_OS;
    } else if (contains(st.shell_scene, "AppScreen") ||
               contains(st.shell_scene, "ApplicationScreenScene")) {
        s->foreground = PS5_MONITOR_FOREGROUND_GAME;
    } else if (contains(st.shell_scene, "NPXS40002") ||
               contains(st.shell_scene, "NPXS40003") ||
               contains(st.shell_scene, "ReactModalScene") ||
               contains(st.shell_scene, "FocusCapture")) {
        s->foreground = PS5_MONITOR_FOREGROUND_OS;
    } else {
        s->foreground = PS5_MONITOR_FOREGROUND_UNKNOWN;
    }
}

static void reset_fps(void)
{
    g_fps.have_baseline = 0;
    g_fps.previous_counter = 0;
    memset(&g_fps.previous_time, 0, sizeof(g_fps.previous_time));
    g_fps.have_last_good = 0;
    g_fps.last_good_fps = 0;
    memset(&g_fps.last_good_time, 0, sizeof(g_fps.last_good_time));
}

static int sample_fps(int game_resident, unsigned *out_fps)
{
    int fd, saved_errno;
    dce_fps_ioctl_arg_t arg;
    uint64_t words[DCE_FPS_WORDS] = {0};
    struct timespec now;
    uint64_t counter, elapsed, delta;
    double candidate;

    if (!game_resident || !out_fps) {
        reset_fps();
        g_fps.game_active = 0;
        return 1;
    }

    g_fps.game_active = 1;
    fd = open(DCE_FPS_DEVICE, O_RDONLY);
    if (fd < 0) return errno ? -errno : -1;

    memset(&arg, 0, sizeof(arg));
    arg.selector = DCE_FPS_SELECTOR;
    arg.mask = DCE_FPS_MASK;
    arg.output = (uint64_t)(uintptr_t)words;

    if (ioctl(fd, DCE_FPS_IOCTL_REQUEST, &arg) != 0) {
        saved_errno = errno;
        close(fd);
        return saved_errno ? -saved_errno : -1;
    }
    close(fd);

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    counter = words[DCE_FPS_COUNTER_WORD];

    if (!g_fps.have_baseline || counter < g_fps.previous_counter) {
        g_fps.previous_counter = counter;
        g_fps.previous_time = now;
        g_fps.have_baseline = 1;
        return 1;
    }

    elapsed = delta_ms(&g_fps.previous_time, &now);
    delta = counter - g_fps.previous_counter;
    g_fps.previous_counter = counter;
    g_fps.previous_time = now;

    if (elapsed) {
        candidate = (double)delta * 1000.0 / (double)elapsed;
        if (candidate >= DCE_FPS_MIN && candidate <= DCE_FPS_MAX) {
            *out_fps = (unsigned)(candidate + 0.5);
            if (*out_fps >= 1 && *out_fps <= 240) {
                g_fps.have_last_good = 1;
                g_fps.last_good_fps = *out_fps;
                g_fps.last_good_time = now;
                return 0;
            }
        }
    }

    /* A single odd DCE sample should not make the dashboard flash to "—".
     * Hold the most recent valid value briefly; if the game really stops
     * presenting frames the hold naturally expires. */
    if (g_fps.have_last_good && delta_ms(&g_fps.last_good_time, &now) <= 1500) {
        *out_fps = g_fps.last_good_fps;
        return 0;
    }

    return 1;
}


void ps5_monitor_perf_init(void)
{
    pthread_mutex_lock(&g_lock);
    memset(&g_fps, 0, sizeof(g_fps));
    memset(&g_cache, 0, sizeof(g_cache));
    memset(&g_context_cache, 0, sizeof(g_context_cache));
    memset(&g_cache_time, 0, sizeof(g_cache_time));
    memset(&g_context_time, 0, sizeof(g_context_time));
    g_cache_valid = 0; g_context_valid = 0;
    pthread_mutex_unlock(&g_lock);
}

int ps5_monitor_perf_collect(ps5_monitor_perf_snapshot_t *out)
{
    struct timespec now;
    ps5_monitor_perf_snapshot_t s;
    uint32_t clocks[SOC_CLOCK_COUNT] = {0};
    uint16_t fan_raw = 0;
    uint64_t chassis = 0;

    if (!out) return -1;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&g_lock);
    if (g_cache_valid && delta_ms(&g_cache_time, &now) < 200) {
        *out = g_cache; pthread_mutex_unlock(&g_lock); return 0;
    }
    memset(&s, 0, sizeof(s));

    if (!g_context_valid || delta_ms(&g_context_time, &now) >= 1000) {
        memset(&g_context_cache, 0, sizeof(g_context_cache));
        detect_context(&g_context_cache);
        g_context_time = now; g_context_valid = 1;
    }
    s.foreground = g_context_cache.foreground;
    s.game_resident = g_context_cache.game_resident;
    s.have_game_pid = g_context_cache.have_game_pid;
    s.game_pid = g_context_cache.game_pid;
    snprintf(s.game_title_id, sizeof(s.game_title_id), "%s", g_context_cache.game_title_id);

    if (sceKernelGetSocClock(clocks) == 0 && clocks[SOC_CLOCK_GFXCLK] > 0) {
        s.have_gpu_clock = 1; s.gpu_clock_mhz = clocks[SOC_CLOCK_GFXCLK];
    }
    if (sceKernelGetCurrentFanDuty(&fan_raw, &chassis) == 0) {
        s.have_fan_duty = 1; s.fan_duty_percent = (double)fan_raw * 100.0 / FAN_DUTY_SCALE;
    }
    /* DCE FPS is tied to a resident game, not to the ShellUI focus
     * classifier.  This prevents a transient UNKNOWN context from resetting
     * the FPS baseline while a game is still resident. */
    if (sample_fps(s.game_resident, &s.fps) == 0) {
        s.have_fps = 1;
        /* An advancing DCE presentation counter is stronger evidence that a
         * game is actively rendering than stale ShellUI msgbuf history. */
        s.foreground = PS5_MONITOR_FOREGROUND_GAME;
    }

    g_cache = s; g_cache_time = now; g_cache_valid = 1;
    *out = s;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static const char *json_bool(int v) { return v ? "true" : "false"; }

int ps5_monitor_perf_to_json(char *out, size_t out_size, const ps5_monitor_perf_snapshot_t *s)
{
    int n;
    if (!out || !out_size || !s) return -1;
    n = snprintf(out, out_size,
        "{\"foreground\":\"%s\",\"game_resident\":%s,\"game_pid\":%u,"
        "\"title_id\":\"%s\",\"gpu_clock_valid\":%s,\"gpu_clock_mhz\":%u,"
        "\"fan_valid\":%s,\"fan_duty_pct\":%.1f,"
        "\"fps_valid\":%s,\"fps\":%u}",
        ps5_monitor_foreground_name(s->foreground), json_bool(s->game_resident), s->game_pid,
        s->game_title_id, json_bool(s->have_gpu_clock), s->gpu_clock_mhz,
        json_bool(s->have_fan_duty), s->fan_duty_percent,
        json_bool(s->have_fps), s->fps);
    return (n >= 0 && (size_t)n < out_size) ? n : -1;
}
