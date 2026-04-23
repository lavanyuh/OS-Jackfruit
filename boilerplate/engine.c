/*
 * engine.c - Supervised Multi-Container Runtime (User Space)
 *
 * Intentionally partial starter:
 *   - command-line shape is defined
 *   - key runtime data structures are defined
 *   - bounded-buffer skeleton is defined
 *   - supervisor / client split is outlined
 *
 * Students are expected to design:
 *   - the control-plane IPC implementation
 *   - container lifecycle and metadata synchronization
 *   - clone + namespace setup for each container
 *   - producer/consumer behavior for log buffering
 *   - signal handling and graceful shutdown
 */
 
#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#define MAX_CONTAINERS 10

typedef struct {
    char id[32];
    pid_t pid;
} container_t;

static container_t containers[MAX_CONTAINERS];
static int container_count = 0;

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
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
#include <time.h>
#include <unistd.h>

#include "monitor_ioctl.h"


#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ID_LEN 32
#define CONTROL_PATH "/tmp/mini_runtime.sock"
#define LOG_DIR "logs"
#define CONTROL_MESSAGE_LEN 256
#define CHILD_COMMAND_LEN 256
#define LOG_CHUNK_SIZE 4096
#define LOG_BUFFER_CAPACITY 16
#define DEFAULT_SOFT_LIMIT (40UL << 20)
#define DEFAULT_HARD_LIMIT (64UL << 20)

typedef enum {
    CMD_SUPERVISOR = 0,
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_LOGS,
    CMD_STOP
} command_kind_t;

typedef enum {
    CONTAINER_STARTING = 0,
    CONTAINER_RUNNING,
    CONTAINER_STOPPED,
    CONTAINER_KILLED,
    CONTAINER_EXITED
} container_state_t;

typedef struct container_record {
    char id[CONTAINER_ID_LEN];
    pid_t host_pid;
    time_t started_at;
    container_state_t state;
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int exit_code;
    int exit_signal;
    char log_path[PATH_MAX];
    struct container_record *next;
} container_record_t;

typedef struct {
    char container_id[CONTAINER_ID_LEN];
    size_t length;
    char data[LOG_CHUNK_SIZE];
} log_item_t;

typedef struct {
    log_item_t items[LOG_BUFFER_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} bounded_buffer_t;

typedef struct {
    command_kind_t kind;
    char container_id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int nice_value;
} control_request_t;

// function prototypes
static int send_control_request(const control_request_t *req);

#define STACK_SIZE (1024 * 1024)

typedef struct {
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
} child_args_t;

static int child_func(void *arg)
{
    child_args_t *args = (child_args_t *)arg;

    sethostname("container", 9);

    if (chroot(args->rootfs) != 0) {
        perror("chroot failed");
        return 1;
    }

    chdir("/");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount /proc failed");
        return 1;
    }

    execl(args->command, args->command, NULL);
    perror("exec failed");
    return 1;
}

static int cmd_start(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s start <id> <rootfs> <command>\n", argv[0]);
        return 1;
    }

    printf("Starting container: %s\n", argv[2]);

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }

    char *stackTop = stack + STACK_SIZE;

    /* Pass rootfs and command to child */
    child_args_t child_args;
    memset(&child_args, 0, sizeof(child_args));
    strncpy(child_args.rootfs, argv[3], sizeof(child_args.rootfs) - 1);
    strncpy(child_args.command, argv[4], sizeof(child_args.command) - 1);

    int flags = CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD;
    pid_t pid = clone(child_func, stackTop, flags, &child_args);
    if (pid == -1) {
        perror("clone failed");
        free(stack);
        return 1;
    }

    printf("Container started with PID: %d\n", pid);

    /* Register with kernel monitor */
    int monitor_fd = open("/dev/container_monitor", O_RDWR);
    if (monitor_fd < 0) {
        perror("open /dev/container_monitor");
    } else {
        unsigned long soft = DEFAULT_SOFT_LIMIT;
        unsigned long hard = DEFAULT_HARD_LIMIT;

        for (int i = 5; i < argc - 1; i++) {
            if (strcmp(argv[i], "--soft-mib") == 0)
                soft = (unsigned long)atol(argv[i + 1]) * 1024 * 1024;
            if (strcmp(argv[i], "--hard-mib") == 0)
                hard = (unsigned long)atol(argv[i + 1]) * 1024 * 1024;
        }

        if (register_with_monitor(monitor_fd, argv[2], pid, soft, hard) < 0)
            perror("register_with_monitor");
        else
            printf("[MONITOR] Registered %s (PID %d) soft=%luMiB hard=%luMiB\n",
                   argv[2], pid, soft / (1024 * 1024), hard / (1024 * 1024));

        close(monitor_fd);
    }

    /* Log container start */
    FILE *log = fopen("log.txt", "a");
    if (log) {
        fprintf(log, "[LOG] Started container %s with PID %d\n", argv[2], pid);
        fclose(log);
    }
    printf("[CONSUMER] Log written for %s\n", argv[2]);

    /* Track in containers array and file */
    FILE *fp = fopen("containers.txt", "a");
    if (fp != NULL) {
        fprintf(fp, "%s %d\n", argv[2], pid);
        fclose(fp);
    }
    strcpy(containers[container_count].id, argv[2]);
    containers[container_count].pid = pid;
    container_count++;

    printf("[LOG] Starting container %s with PID %d\n", argv[2], pid);

    waitpid(pid, NULL, 0);
    free(stack);
    return 0;
}

static int cmd_run(int argc, char *argv[]);
typedef struct {
    int status;
    char message[CONTROL_MESSAGE_LEN];
} control_response_t;

typedef struct {
    char id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
    int nice_value;
    int log_write_fd;
} child_config_t;

typedef struct {
    int server_fd;
    int monitor_fd;
    int should_stop;
    pthread_t logger_thread;
    bounded_buffer_t log_buffer;
    pthread_mutex_t metadata_lock;
    container_record_t *containers;
} supervisor_ctx_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s supervisor <base-rootfs>\n"
            "  %s start <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s run <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s ps\n"
            "  %s logs <id>\n"
            "  %s stop <id>\n",
            prog, prog, prog, prog, prog, prog);
}

static int parse_mib_flag(const char *flag,
                          const char *value,
                          unsigned long *target_bytes)
{
    char *end = NULL;
    unsigned long mib;

    errno = 0;
    mib = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", flag, value);
        return -1;
    }

    if (mib > ULONG_MAX / (1UL << 20)) {
        fprintf(stderr, "Value for %s is too large: %s\n", flag, value);
        return -1;
    }

    *target_bytes = mib * (1UL << 20);
    return 0;
}

static int parse_optional_flags(control_request_t *req,
                                int argc,
                                char *argv[],
                                int start_index)
{
    int i;

    for (i = start_index; i < argc; i += 2) {
        char *end = NULL;
        long nice_value;

        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value for option: %s\n", argv[i]);
            return -1;
        }

        if (strcmp(argv[i], "--soft-mib") == 0) {
            if (parse_mib_flag("--soft-mib", argv[i + 1], &req->soft_limit_bytes) != 0)
                return -1;
            continue;
        }

        if (strcmp(argv[i], "--hard-mib") == 0) {
            if (parse_mib_flag("--hard-mib", argv[i + 1], &req->hard_limit_bytes) != 0)
                return -1;
            continue;
        }

        if (strcmp(argv[i], "--nice") == 0) {
            errno = 0;
            nice_value = strtol(argv[i + 1], &end, 10);
            if (errno != 0 || end == argv[i + 1] || *end != '\0' ||
                nice_value < -20 || nice_value > 19) {
                fprintf(stderr,
                        "Invalid value for --nice (expected -20..19): %s\n",
                        argv[i + 1]);
                return -1;
            }
            req->nice_value = (int)nice_value;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        return -1;
    }

    if (req->soft_limit_bytes > req->hard_limit_bytes) {
        fprintf(stderr, "Invalid limits: soft limit cannot exceed hard limit\n");
        return -1;
    }

    return 0;
}

static const char *state_to_string(container_state_t state)
{
    switch (state) {
    case CONTAINER_STARTING:
        return "starting";
    case CONTAINER_RUNNING:
        return "running";
    case CONTAINER_STOPPED:
        return "stopped";
    case CONTAINER_KILLED:
        return "killed";
    case CONTAINER_EXITED:
        return "exited";
    default:
        return "unknown";
    }
}

static int bounded_buffer_init(bounded_buffer_t *buffer)
{
    int rc;

    memset(buffer, 0, sizeof(*buffer));

    rc = pthread_mutex_init(&buffer->mutex, NULL);
    if (rc != 0)
        return rc;

    rc = pthread_cond_init(&buffer->not_empty, NULL);
    if (rc != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return rc;
    }

    rc = pthread_cond_init(&buffer->not_full, NULL);
    if (rc != 0) {
        pthread_cond_destroy(&buffer->not_empty);
        pthread_mutex_destroy(&buffer->mutex);
        return rc;
    }

    return 0;
}

static void bounded_buffer_destroy(bounded_buffer_t *buffer)
{
    pthread_cond_destroy(&buffer->not_full);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mutex);
}

static void bounded_buffer_begin_shutdown(bounded_buffer_t *buffer)
{
    pthread_mutex_lock(&buffer->mutex);
    buffer->shutting_down = 1;
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_cond_broadcast(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
}

/*
 * TODO:
 * Implement producer-side insertion into the bounded buffer.
 *
 * Requirements:
 *   - block or fail according to your chosen policy when the buffer is full
 *   - wake consumers correctly
 *   - stop cleanly if shutdown begins
 */

int bounded_buffer_push(bounded_buffer_t *buffer, const log_item_t *item)
{
    pthread_mutex_lock(&buffer->mutex);

    /* Wait while full, unless shutting down */
    while (buffer->count == LOG_BUFFER_CAPACITY && !buffer->shutting_down) {
        pthread_cond_wait(&buffer->not_full, &buffer->mutex);
    }

    if (buffer->shutting_down) {
        pthread_mutex_unlock(&buffer->mutex);
        return -1;
    }

    buffer->items[buffer->tail] = *item;
    buffer->tail = (buffer->tail + 1) % LOG_BUFFER_CAPACITY;
    buffer->count++;

    fprintf(stderr, "[PRODUCER] Inserted log chunk for container '%s' "
            "(buffer count: %zu/%d)\n",
            item->container_id, buffer->count, LOG_BUFFER_CAPACITY);

    pthread_cond_signal(&buffer->not_empty);
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

/*
 * TODO:
 * Implement consumer-side removal from the bounded buffer.
 *
 * Requirements:
 *   - wait correctly while the buffer is empty
 *   - return a useful status when shutdown is in progress
 *   - avoid races with producers and shutdown
 */

int bounded_buffer_pop(bounded_buffer_t *buffer, log_item_t *item)
{
    pthread_mutex_lock(&buffer->mutex);

    /* Wait while empty, unless shutting down */
    while (buffer->count == 0 && !buffer->shutting_down) {
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }

    /* On shutdown, drain remaining items before returning -1 */
    if (buffer->count == 0 && buffer->shutting_down) {
        pthread_mutex_unlock(&buffer->mutex);
        return -1;
    }

    *item = buffer->items[buffer->head];
    buffer->head = (buffer->head + 1) % LOG_BUFFER_CAPACITY;
    buffer->count--;

    fprintf(stderr, "[CONSUMER] Removed log chunk for container '%s' "
            "(buffer count: %zu/%d)\n",
            item->container_id, buffer->count, LOG_BUFFER_CAPACITY);

    pthread_cond_signal(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

/*
 * TODO:
 * Implement the logging consumer thread.
 *
 * Suggested responsibilities:
 *   - remove log chunks from the bounded buffer
 *   - route each chunk to the correct per-container log file
 *   - exit cleanly when shutdown begins and pending work is drained
 */

void *logging_thread(void *arg)
{
    supervisor_ctx_t *ctx = (supervisor_ctx_t *)arg;
    log_item_t item;
    char log_path[PATH_MAX];
    FILE *fp;

    fprintf(stderr, "[LOGGER THREAD] Started\n");

    while (1) {
        if (bounded_buffer_pop(&ctx->log_buffer, &item) != 0) {
            /* Shutdown signalled and buffer drained */
            fprintf(stderr, "[LOGGER THREAD] Buffer drained, exiting\n");
            break;
        }

        /* Build log file path: logs/<container_id>.log */
        snprintf(log_path, sizeof(log_path), "%s/%s.log",
                 LOG_DIR, item.container_id);

        fp = fopen(log_path, "a");
        if (fp) {
            fwrite(item.data, 1, item.length, fp);
            fclose(fp);
        } else {
            fprintf(stderr, "[LOGGER THREAD] Could not open log file: %s\n",
                    log_path);
        }
    }

    fprintf(stderr, "[LOGGER THREAD] Exited cleanly\n");
    return NULL;
}

/*
 * TODO:
 * Implement the clone child entrypoint.
 *
 * Required outcomes:
 *   - isolated PID / UTS / mount context
 *   - chroot or pivot_root into rootfs
 *   - working /proc inside container
 *   - stdout / stderr redirected to the supervisor logging path
 *   - configured command executed inside the container
 */
int child_fn(void *arg)
{
    (void)arg;
    return 1;
}

int register_with_monitor(int monitor_fd,
                          const char *container_id,
                          pid_t host_pid,
                          unsigned long soft_limit_bytes,
                          unsigned long hard_limit_bytes)
{
    struct monitor_request req;

    memset(&req, 0, sizeof(req));
    req.pid = host_pid;
    req.soft_limit_bytes = soft_limit_bytes;
    req.hard_limit_bytes = hard_limit_bytes;
    strncpy(req.container_id, container_id, sizeof(req.container_id) - 1);

    if (ioctl(monitor_fd, MONITOR_REGISTER, &req) < 0)
        return -1;

    return 0;
}

int unregister_from_monitor(int monitor_fd, const char *container_id, pid_t host_pid)
{
    struct monitor_request req;

    memset(&req, 0, sizeof(req));
    req.pid = host_pid;
    strncpy(req.container_id, container_id, sizeof(req.container_id) - 1);

    if (ioctl(monitor_fd, MONITOR_UNREGISTER, &req) < 0)
        return -1;

    return 0;
}

/*
 * TODO:
 * Implement the long-running supervisor process.
 *
 * Suggested responsibilities:
 *   - create and bind the control-plane IPC endpoint
 *   - initialize shared metadata and the bounded buffer
 *   - start the logging thread
 *   - accept control requests and update container state
 *   - reap children and respond to signals
 */

static volatile int g_should_stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_should_stop = 1;
}

static int run_supervisor(const char *rootfs)
{
    printf("Supervisor started with rootfs: %s\n", rootfs);

    mkdir(LOG_DIR, 0755);

    supervisor_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    bounded_buffer_init(&ctx.log_buffer);
    pthread_create(&ctx.logger_thread, NULL, logging_thread, &ctx);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (!g_should_stop) {
        FILE *req = fopen("request.txt", "r");
        if (req) {
            char cmd[128];

            if (fgets(cmd, sizeof(cmd), req)) {
                printf("[SUPERVISOR] Received: %s", cmd);

                /* Push every received command into the bounded buffer as a log entry */
                log_item_t item;
                memset(&item, 0, sizeof(item));
                strncpy(item.container_id, "supervisor", sizeof(item.container_id) - 1);
                item.length = snprintf(item.data, sizeof(item.data),
                                       "[SUPERVISOR] Command received: %s", cmd);
                bounded_buffer_push(&ctx.log_buffer, &item);

                /* ps command */
                if (strncmp(cmd, "ps", 2) == 0) {
                    FILE *cf = fopen("containers.txt", "r");
                    if (cf) {
                        char id[32];
                        int pid;

                        printf("[SUPERVISOR] Active containers:\n");
                        while (fscanf(cf, "%s %d", id, &pid) == 2) {
                            printf("  %s -> PID %d\n", id, pid);

                            /* Log each container entry into the bounded buffer */
                            log_item_t citem;
                            memset(&citem, 0, sizeof(citem));
                            strncpy(citem.container_id, id, sizeof(citem.container_id) - 1);
                            citem.length = snprintf(citem.data, sizeof(citem.data),
                                                    "[PS] Container %s -> PID %d\n", id, pid);
                            bounded_buffer_push(&ctx.log_buffer, &citem);
                        }
                        fclose(cf);
                    } else {
                        printf("[SUPERVISOR] No containers file\n");
                    }
                }

                /* stop command */
                if (strncmp(cmd, "stop", 4) == 0) {
                    char id[32];
                    sscanf(cmd, "stop %s", id);

                    FILE *cf = fopen("containers.txt", "r");
                    if (cf) {
                        char cid[32];
                        int pid;

                        while (fscanf(cf, "%s %d", cid, &pid) == 2) {
                            if (strcmp(cid, id) == 0) {
                                kill(pid, SIGKILL);
                                printf("[SUPERVISOR] Stopped %s (PID %d)\n", id, pid);

                                /* Log the stop event into the bounded buffer */
                                log_item_t sitem;
                                memset(&sitem, 0, sizeof(sitem));
                                strncpy(sitem.container_id, id, sizeof(sitem.container_id) - 1);
                                sitem.length = snprintf(sitem.data, sizeof(sitem.data),
                                                        "[STOP] Container %s (PID %d) killed\n",
                                                        id, pid);
                                bounded_buffer_push(&ctx.log_buffer, &sitem);
                            }
                        }
                        fclose(cf);
                    }
                }

                /* start command - log the container startup */
                if (strncmp(cmd, "start", 5) == 0) {
                    char id[32];
                    sscanf(cmd, "start %s", id);

                    log_item_t litem;
                    memset(&litem, 0, sizeof(litem));
                    strncpy(litem.container_id, id, sizeof(litem.container_id) - 1);
                    litem.length = snprintf(litem.data, sizeof(litem.data),
                                            "[START] Container %s launched\n", id);
                    bounded_buffer_push(&ctx.log_buffer, &litem);
                }
            }

            fclose(req);
            remove("request.txt");
        }

        sleep(1);
    }

    printf("[SUPERVISOR] Shutting down...\n");
    bounded_buffer_begin_shutdown(&ctx.log_buffer);
    pthread_join(ctx.logger_thread, NULL);
    bounded_buffer_destroy(&ctx.log_buffer);
    printf("[SUPERVISOR] Clean exit\n");

    return 0;
}
    
static int cmd_logs(int argc, char *argv[])
{
    control_request_t req;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s logs <id>\n", argv[0]);
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.kind = CMD_LOGS;
    strncpy(req.container_id, argv[2], sizeof(req.container_id) - 1);

    return send_control_request(&req);
}


static int cmd_ps(void)
{
    FILE *fp = fopen("request.txt", "w");
    if (fp) {
        fprintf(fp, "ps\n");
        fclose(fp);
    }

    printf("[CLI] Sent ps request to supervisor\n");
    return 0;
}


// TEMP STUB FUNCTIONS (to fix build)

static int send_control_request(const control_request_t *req)
{
    printf("Stub: sending request for container %s\n", req->container_id);
    return 0;
}

static int cmd_stop(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: stop <id>\n");
        return 1;
    }

    char *id = argv[2];

    FILE *fp = fopen("request.txt", "w");
    if (fp) {
        fprintf(fp, "stop %s\n", id);
        fclose(fp);
    }

    printf("[CLI] Sent stop request for %s\n", id);
    return 0;
}

static int cmd_run(int argc, char *argv[])
{
    printf("Stub: run command\n");
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s supervisor <base-rootfs>\n", argv[0]);
            return 1;
        }
        return run_supervisor(argv[2]);
    }
    else if (strcmp(argv[1], "start") == 0) {
        return cmd_start(argc, argv);
    }
    else if (strcmp(argv[1], "ps") == 0) {
        return cmd_ps();
    }
    else if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc, argv);
    }
    else if (strcmp(argv[1], "logs") == 0) {
        return cmd_logs(argc, argv);
    }
    else if (strcmp(argv[1], "stop") == 0) {
        return cmd_stop(argc, argv);
    }

    usage(argv[0]);
    return 1;
}
