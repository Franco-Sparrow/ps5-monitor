#ifndef PS5_MONITOR_PERF_H
#define PS5_MONITOR_PERF_H

#include <stddef.h>
#include <stdint.h>

typedef enum ps5_monitor_foreground_kind {
    PS5_MONITOR_FOREGROUND_UNKNOWN = 0,
    PS5_MONITOR_FOREGROUND_OS = 1,
    PS5_MONITOR_FOREGROUND_GAME = 2
} ps5_monitor_foreground_kind_t;

typedef struct ps5_monitor_perf_snapshot {
    ps5_monitor_foreground_kind_t foreground;
    int game_resident;
    int have_game_pid;
    uint32_t game_pid;
    char game_title_id[16];

    int have_gpu_clock;
    unsigned gpu_clock_mhz;

    int have_fan_duty;
    double fan_duty_percent;


    int have_fps;
    unsigned fps;
} ps5_monitor_perf_snapshot_t;

void ps5_monitor_perf_init(void);
int ps5_monitor_perf_collect(ps5_monitor_perf_snapshot_t *out);
int ps5_monitor_perf_to_json(char *out, size_t out_size, const ps5_monitor_perf_snapshot_t *s);
const char *ps5_monitor_foreground_name(ps5_monitor_foreground_kind_t kind);

#endif
