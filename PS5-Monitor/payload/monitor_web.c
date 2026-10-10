#include "monitor_web.h"
#include "monitor_perf.h"
#include "web_assets.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <unistd.h>

#define PS5_MONITOR_WEB_PORT 9843
#define HTTP_MAX_REQUEST (64 * 1024)
#define CORE_MAX_RESPONSE (1024 * 1024)

#define RESP_ERROR 0x02

/* Read-only monitoring commands exposed by the internal core. */
#define CMD_LIST_STORAGE      0x02
#define CMD_GET_SYSTEM_INFO   0x32
#define CMD_GET_HW_INFO       0x34
#define CMD_GET_TEMPS         0x35
#define CMD_GET_POWER_INFO    0x39
#define CMD_GET_EXTENDED_INFO 0x47
#define CMD_GET_CPU_USAGE     0x48
#define CMD_GET_MEMORY_INFO   0x49
#define CMD_NET_INFO          0x75

static int g_core_port = 0;
static char g_version[32] = "0.0.0";
static volatile int g_started = 0;

int ps5_monitor_web_port(void) { return PS5_MONITOR_WEB_PORT; }

static int send_all_local(int fd, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all_local(int fd, void *data, size_t len)
{
    unsigned char *p = (unsigned char *)data;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static void http_reply(int fd, int status, const char *reason,
                       const char *content_type, const void *body, size_t body_len)
{
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "\r\n",
        status, reason, content_type ? content_type : "text/plain", body_len);
    if (n > 0) (void)send_all_local(fd, hdr, (size_t)n);
    if (body && body_len) (void)send_all_local(fd, body, body_len);
}

static void reply_json(int fd, int status, const char *reason, const char *json)
{
    if (!json) json = "{}";
    http_reply(fd, status, reason, "application/json; charset=utf-8", json, strlen(json));
}

static void reply_text(int fd, int status, const char *reason, const char *text, size_t len)
{
    if (!text) { text = ""; len = 0; }
    http_reply(fd, status, reason, "text/plain; charset=utf-8", text, len);
}

/*
 * Monitoring data already implemented by the inherited core is consumed over
 * loopback.  Port 9113+ is bound to 127.0.0.1 only in PS5 Monitor; the LAN
 * interface exposed to the user is HTTP on port 9843.
 */
static int core_request(uint8_t cmd, uint8_t *response_type,
                        unsigned char **out, uint32_t *out_len)
{
    int fd = -1;
    struct sockaddr_in sa;
    uint8_t hdr[5] = {0};
    uint8_t rh[5];
    unsigned char *buf = NULL;
    uint32_t len = 0;

    if (!response_type || !out || !out_len || g_core_port <= 0) return -1;
    *response_type = RESP_ERROR;
    *out = NULL;
    *out_len = 0;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)g_core_port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }

    hdr[0] = cmd;
    if (send_all_local(fd, hdr, sizeof(hdr)) != 0) {
        close(fd);
        return -1;
    }

    if (recv_all_local(fd, rh, sizeof(rh)) != 0) {
        close(fd);
        return -1;
    }

    memcpy(&len, rh + 1, 4);
    if (len > CORE_MAX_RESPONSE) {
        close(fd);
        return -1;
    }

    if (len) {
        buf = (unsigned char *)malloc((size_t)len + 1);
        if (!buf) {
            close(fd);
            return -1;
        }
        if (recv_all_local(fd, buf, len) != 0) {
            free(buf);
            close(fd);
            return -1;
        }
        buf[len] = 0;
    }

    close(fd);
    *response_type = rh[0];
    *out = buf;
    *out_len = len;
    return 0;
}

static void handle_core_text(int fd, uint8_t cmd)
{
    uint8_t type = RESP_ERROR;
    unsigned char *data = NULL;
    uint32_t len = 0;

    if (core_request(cmd, &type, &data, &len) != 0) {
        reply_json(fd, 503, "Service Unavailable", "{\"error\":\"monitor core unavailable\"}");
        return;
    }

    if (type == RESP_ERROR)
        reply_text(fd, 502, "Bad Gateway", (const char *)data, len);
    else
        reply_text(fd, 200, "OK", (const char *)data, len);

    free(data);
}

static void handle_status(int fd)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"name\":\"PS5 Monitor\",\"version\":\"%s\",\"web_port\":%d}",
        g_version, PS5_MONITOR_WEB_PORT);
    reply_json(fd, 200, "OK", json);
}

static void handle_perf(int fd)
{
    ps5_monitor_perf_snapshot_t s;
    char json[1400];
    if (ps5_monitor_perf_collect(&s) != 0 ||
        ps5_monitor_perf_to_json(json, sizeof(json), &s) < 0) {
        reply_json(fd, 500, "Internal Server Error", "{\"error\":\"performance collector failed\"}");
        return;
    }
    reply_json(fd, 200, "OK", json);
}

static int find_mount_capacity(const char *mountpoint,
                               uint64_t *total, uint64_t *free_bytes,
                               uint64_t *used) {
    struct statfs *mounts = NULL;
    int count = getfsstat(NULL, 0, MNT_NOWAIT);
    int found = 0;

    if (total) *total = 0;
    if (free_bytes) *free_bytes = 0;
    if (used) *used = 0;
    if (count <= 0) return 0;

    mounts = (struct statfs *)calloc((size_t)count, sizeof(*mounts));
    if (!mounts) return 0;
    count = getfsstat(mounts, (long)((size_t)count * sizeof(*mounts)), MNT_NOWAIT);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            uint64_t block_size, t, f;
            if (strcmp(mounts[i].f_mntonname, mountpoint) != 0)
                continue;
            block_size = mounts[i].f_bsize > 0 ? (uint64_t)mounts[i].f_bsize : 1;
            t = (uint64_t)mounts[i].f_blocks * block_size;
            f = (uint64_t)mounts[i].f_bavail * block_size;
            if (f > t) f = t;
            if (total) *total = t;
            if (free_bytes) *free_bytes = f;
            if (used) *used = t - f;
            found = 1;
            break;
        }
    }
    free(mounts);
    return found;
}

static void handle_volumes(int fd)
{
    uint64_t m2_total = 0, m2_free = 0, m2_used = 0;
    uint64_t usb_total = 0, usb_free = 0, usb_used = 0;
    int m2 = find_mount_capacity("/mnt/ext1", &m2_total, &m2_free, &m2_used);
    int usb = find_mount_capacity("/mnt/ext0", &usb_total, &usb_free, &usb_used);
    char json[1024];

    snprintf(json, sizeof(json),
        "{\"m2\":{\"mounted\":%s,\"path\":\"/mnt/ext1\","
        "\"total\":%llu,\"used\":%llu,\"free\":%llu},"
        "\"usb\":{\"mounted\":%s,\"path\":\"/mnt/ext0\","
        "\"total\":%llu,\"used\":%llu,\"free\":%llu}}",
        m2 ? "true" : "false",
        (unsigned long long)m2_total,
        (unsigned long long)m2_used,
        (unsigned long long)m2_free,
        usb ? "true" : "false",
        (unsigned long long)usb_total,
        (unsigned long long)usb_used,
        (unsigned long long)usb_free);
    reply_json(fd, 200, "OK", json);
}

static void serve_asset(int fd, const char *path)
{
    const ps5_monitor_web_asset_t *asset = ps5_monitor_web_asset_find(path);
    if (!asset) {
        static const char not_found[] = "Not found\n";
        http_reply(fd, 404, "Not Found", "text/plain; charset=utf-8",
                   not_found, sizeof(not_found) - 1);
        return;
    }
    http_reply(fd, 200, "OK", asset->content_type, asset->data, asset->size);
}

typedef struct client_arg { int fd; } client_arg_t;

static void *web_client_thread(void *arg)
{
    client_arg_t *ca = (client_arg_t *)arg;
    int fd = ca ? ca->fd : -1;
    char *req = NULL;
    size_t used = 0;
    size_t cap = 8192;

    free(ca);
    if (fd < 0) return NULL;

    req = (char *)malloc(cap + 1);
    if (!req) { close(fd); return NULL; }

    for (;;) {
        ssize_t n;
        if (used == cap) {
            size_t next;
            char *nr;
            if (cap >= HTTP_MAX_REQUEST) break;
            next = cap * 2;
            nr = (char *)realloc(req, next + 1);
            if (!nr) break;
            req = nr;
            cap = next;
        }

        n = recv(fd, req + used, cap - used, 0);
        if (n <= 0) break;
        used += (size_t)n;
        req[used] = '\0';

        if (strstr(req, "\r\n\r\n")) break;
    }

    if (used > 0) {
        char method[8] = {0};
        char path[512] = {0};
        if (sscanf(req, "%7s %511s", method, path) == 2) {
            char *q = strchr(path, '?');
            if (q) *q = '\0';

            if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/status")) handle_status(fd);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/perf")) handle_perf(fd);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/hardware")) handle_core_text(fd, CMD_GET_HW_INFO);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/system")) handle_core_text(fd, CMD_GET_SYSTEM_INFO);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/extended")) handle_core_text(fd, CMD_GET_EXTENDED_INFO);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/sensors")) handle_core_text(fd, CMD_GET_TEMPS);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/cpu")) handle_core_text(fd, CMD_GET_CPU_USAGE);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/memory")) handle_core_text(fd, CMD_GET_MEMORY_INFO);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/network")) handle_core_text(fd, CMD_NET_INFO);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/storage")) handle_core_text(fd, CMD_LIST_STORAGE);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/volumes")) handle_volumes(fd);
            else if (!strcmp(method, "GET") && !strcmp(path, "/api/v1/power")) handle_core_text(fd, CMD_GET_POWER_INFO);
            else if (!strcmp(method, "GET")) serve_asset(fd, path);
            else reply_json(fd, 405, "Method Not Allowed", "{\"error\":\"method not allowed\"}");
        } else {
            reply_json(fd, 400, "Bad Request", "{\"error\":\"bad request\"}");
        }
    }

    free(req);
    close(fd);
    return NULL;
}

static void *web_server_thread(void *arg)
{
    int server;
    int one = 1;
    struct sockaddr_in addr;
    (void)arg;

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { g_started = -1; return NULL; }

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(server, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PS5_MONITOR_WEB_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(server, 32) != 0) {
        close(server);
        g_started = -1;
        return NULL;
    }

    g_started = 1;
    for (;;) {
        int fd = accept(server, NULL, NULL);
        client_arg_t *ca;
        pthread_t th;
        pthread_attr_t at;

        if (fd < 0) continue;
        ca = (client_arg_t *)malloc(sizeof(*ca));
        if (!ca) { close(fd); continue; }
        ca->fd = fd;

        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, web_client_thread, ca) != 0) {
            close(fd);
            free(ca);
        }
        pthread_attr_destroy(&at);
    }
}

int ps5_monitor_web_start(int core_port, const char *version)
{
    pthread_t th;
    pthread_attr_t at;

    if (g_started != 0) return g_started > 0 ? 0 : -1;

    g_core_port = core_port;
    snprintf(g_version, sizeof(g_version), "%s", version ? version : "unknown");
    ps5_monitor_perf_init();

    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, web_server_thread, NULL) != 0) {
        pthread_attr_destroy(&at);
        return -1;
    }
    pthread_attr_destroy(&at);
    return 0;
}
