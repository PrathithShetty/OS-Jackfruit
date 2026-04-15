#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define CONTROL_PATH "/tmp/mini_runtime.sock"

/* ================= COMMAND TYPES ================= */

typedef enum { CMD_START=1, CMD_PS, CMD_STOP, CMD_LOGS } cmd_t;

/* ================= STRUCTS ================= */

typedef struct {
    cmd_t kind;
    char id[32];
    char rootfs[PATH_MAX];
    char command[256];
} request_t;

typedef struct {
    int status;
    char msg[256];
} response_t;

typedef struct container {
    char id[32];
    pid_t pid;
    int running;
    char log_path[PATH_MAX];
    struct container *next;
} container_t;

static container_t *containers = NULL;

/* ================= CHILD CONFIG ================= */

typedef struct {
    char rootfs[PATH_MAX];
    char command[256];
    char log_path[PATH_MAX];
} child_cfg;

/* ================= CHILD FUNCTION ================= */

static int child_fn(void *arg)
{
    child_cfg *cfg = (child_cfg*)arg;

    /* ===== OPEN LOG FIRST ===== */
    int fd = open(cfg->log_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        perror("log open failed");
        return 1;
    }

    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    printf("Container starting...\n");

    if (sethostname("container", 9) != 0)
        perror("sethostname");

    if (chroot(cfg->rootfs) != 0) {
        perror("chroot failed");
        return 1;
    }

    if (chdir("/") != 0) {
        perror("chdir failed");
        return 1;
    }

    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", 0, NULL);

    printf("Executing command: %s\n", cfg->command);

    execl("/bin/sh", "sh", "-c", cfg->command, NULL);

    perror("exec failed");
    return 1;
}

/* ================= UTIL ================= */

static container_t* find_container(const char *id)
{
    container_t *c = containers;
    while (c) {
        if (strcmp(c->id, id) == 0)
            return c;
        c = c->next;
    }
    return NULL;
}

/* ================= SUPERVISOR ================= */

static int run_supervisor(const char *rootfs)
{
    int server_fd;
    struct sockaddr_un addr;

    printf("Supervisor using rootfs: %s\n", rootfs);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    unlink(CONTROL_PATH);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    chmod(CONTROL_PATH, 0666);
    listen(server_fd, 5);

    printf("Supervisor running...\n");

    while (1) {
        int client = accept(server_fd, NULL, NULL);

        request_t req;
        response_t resp;

        ssize_t r = read(client, &req, sizeof(req));
        if (r <= 0) {
            close(client);
            continue;
        }

        resp.status = 0;

        /* -------- START -------- */
        if (req.kind == CMD_START) {

            void *stack = malloc(STACK_SIZE);
            child_cfg *cfg = malloc(sizeof(child_cfg));

            snprintf(cfg->rootfs, PATH_MAX, "%s", req.rootfs);
            snprintf(cfg->command, 256, "%s", req.command);
            snprintf(cfg->log_path, PATH_MAX, "/tmp/%s.log", req.id);

            pid_t pid = clone(child_fn,
                              (char*)stack + STACK_SIZE,
                              CLONE_NEWPID|CLONE_NEWUTS|CLONE_NEWNS|SIGCHLD,
                              cfg);

            if (pid < 0) {
                perror("clone");
                snprintf(resp.msg, 256, "Clone failed");
            } else {

                /* ===== REGISTER WITH KERNEL MONITOR ===== */
                int fd = open("/dev/container_monitor", O_RDWR);
                if (fd >= 0) {
                    struct monitor_request mreq;

                    mreq.pid = pid;
                    snprintf(mreq.container_id,
                             sizeof(mreq.container_id),
                             "%s", req.id);

                    /* 🔥 LOW LIMITS (FOR TESTING) */
                    mreq.soft_limit_bytes = 1 * 1024 * 1024;  // 1 MB
                    mreq.hard_limit_bytes = 2 * 1024 * 1024;  // 2 MB

                    ioctl(fd, MONITOR_REGISTER, &mreq);
                    close(fd);
                }

                container_t *c = malloc(sizeof(container_t));
                snprintf(c->id, 32, "%s", req.id);
                c->pid = pid;
                c->running = 1;
                snprintf(c->log_path, PATH_MAX, "/tmp/%s.log", req.id);

                c->next = containers;
                containers = c;

                snprintf(resp.msg, 256,
                         "Started %s (PID %d)", req.id, pid);
            }
        }

        /* -------- PS -------- */
        else if (req.kind == CMD_PS) {
            container_t *c = containers;
            while (c) {
                printf("ID=%s PID=%d RUN=%d\n",
                       c->id, c->pid, c->running);
                c = c->next;
            }
            snprintf(resp.msg, 256, "Listed");
        }

        /* -------- STOP -------- */
        else if (req.kind == CMD_STOP) {
            container_t *c = find_container(req.id);
            if (c) {
                kill(c->pid, SIGKILL);
                c->running = 0;
                snprintf(resp.msg, 256, "Stopped %s", req.id);
            }
        }

        /* -------- LOGS -------- */
        else if (req.kind == CMD_LOGS) {
            container_t *c = find_container(req.id);
            if (!c) {
                snprintf(resp.msg, 256, "No container");
            } else {
                FILE *f = fopen(c->log_path, "r");
                if (!f) {
                    snprintf(resp.msg, 256, "No logs");
                } else {
                    char buf[128];
                    while (fgets(buf, sizeof(buf), f))
                        printf("%s", buf);
                    fclose(f);
                    snprintf(resp.msg, 256, "Logs printed");
                }
            }
        }

        write(client, &resp, sizeof(resp));
        close(client);

        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    return 0;
}

/* ================= CLIENT ================= */

static int send_req(request_t *req)
{
    int fd;
    struct sockaddr_un addr;
    response_t resp;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    connect(fd, (struct sockaddr*)&addr, sizeof(addr));

    write(fd, req, sizeof(*req));
    read(fd, &resp, sizeof(resp));

    printf("%s\n", resp.msg);

    close(fd);
    return 0;
}

/* ================= MAIN ================= */

int main(int argc, char *argv[])
{
    if (argc < 2) return 1;

    if (strcmp(argv[1], "supervisor") == 0)
        return run_supervisor(argv[2]);

    request_t req;
    memset(&req, 0, sizeof(req));

    if (strcmp(argv[1], "start") == 0) {
        req.kind = CMD_START;
        snprintf(req.id, 32, "%s", argv[2]);
        snprintf(req.rootfs, PATH_MAX, "%s", argv[3]);
        snprintf(req.command, 256, "%s", argv[4]);
    }
    else if (strcmp(argv[1], "ps") == 0)
        req.kind = CMD_PS;
    else if (strcmp(argv[1], "stop") == 0) {
        req.kind = CMD_STOP;
        snprintf(req.id, 32, "%s", argv[2]);
    }
    else if (strcmp(argv[1], "logs") == 0) {
        req.kind = CMD_LOGS;
        snprintf(req.id, 32, "%s", argv[2]);
    }

    return send_req(&req);
}
