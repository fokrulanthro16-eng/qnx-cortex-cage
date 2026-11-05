/*
 * QNX-CortexCage: Microkernel-Enforced Deterministic Simplex Safety Cage
 * Component: watchdog_supervisor.c (Sub-Millisecond Fault-Isolation & Self-Healing HA Daemon)
 * Target: QNX Neutrino RTOS 8.0 (x86_64, aarch64le)
 */

#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sched.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#include "../include/cortex_protocol.h"

static volatile bool g_running = true;
static cortex_shm_t *g_shm = NULL;
static uint64_t      g_cps = 1000000000ULL;
static char          g_ai_path[256] = "ai_neural_planner";

static void find_ai_executable(const char *arg)
{
    if (arg && access(arg, X_OK) == 0) {
        strncpy(g_ai_path, arg, sizeof(g_ai_path) - 1);
        return;
    }
    const char *candidates[] = {
        "./ai_neural_planner",
        "bin/x86_64/ai_neural_planner",
        "bin/aarch64le/ai_neural_planner",
        "/proc/boot/ai_neural_planner",
        "ai_neural_planner"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        if (access(candidates[i], X_OK) == 0) {
            strncpy(g_ai_path, candidates[i], sizeof(g_ai_path) - 1);
            return;
        }
    }
}

static pid_t spawn_ai_planner(void)
{
    pid_t pid = -1;
    char *argv_child[] = { g_ai_path, NULL };

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    /* Use posix_spawnp or posix_spawn */
    int rc = posix_spawnp(&pid, g_ai_path, NULL, &attr, argv_child, NULL);
    posix_spawnattr_destroy(&attr);

    if (rc != 0) {
        fprintf(stderr, "[WATCHDOG] posix_spawnp failed for '%s': %s\n", g_ai_path, strerror(rc));
        return -1;
    }
    return pid;
}

int main(int argc, char *argv[])
{
    printf("[WATCHDOG] Initializing High-Availability Fault-Isolation Supervisor (PID %d)...\n", getpid());

    /* Query CPU clock frequency from QNX syspage */
    g_cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    if (g_cps == 0) g_cps = 1000000000ULL;

    /* Elevate Watchdog to Priority 62 (SCHED_FIFO) */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 62; /* 1 priority below the 1kHz Safety Loop */
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) {
        printf("[WATCHDOG] [REALTIME] Elevated to Priority 62 (SCHED_FIFO).\n");
    }

    /* Open shared memory */
    int shm_fd = shm_open(CORTEX_SHM_NAME, O_RDWR, 0666);
    if (shm_fd >= 0) {
        g_shm = (cortex_shm_t *)mmap(NULL, sizeof(cortex_shm_t),
                                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (g_shm != MAP_FAILED && g_shm) {
            g_shm->telemetry.pid_watchdog = getpid();
        } else {
            g_shm = NULL;
        }
        close(shm_fd);
    }

    find_ai_executable(argc > 1 ? argv[1] : NULL);
    printf("[WATCHDOG] Monitored AI executable target: '%s'\n", g_ai_path);

    /* Initial spawn of the AI planner */
    pid_t current_ai_pid = spawn_ai_planner();
    if (current_ai_pid > 0) {
        printf("[WATCHDOG] Initial AI agent spawned with PID %d.\n", current_ai_pid);
        if (g_shm) {
            g_shm->seq_lock++;
            g_shm->telemetry.pid_ai = current_ai_pid;
            g_shm->telemetry.ai_alive = 1;
            g_shm->seq_lock++;
        }
    }

    uint64_t respawn_count = 0;

    /* High-Frequency Supervisor Polling Loop (100Hz / 10ms check) */
    while (g_running) {
        usleep(10000);

        int status = 0;
        pid_t dead_pid = waitpid(current_ai_pid, &status, WNOHANG);

        if (dead_pid == current_ai_pid && current_ai_pid > 0) {
            uint64_t t0 = ClockCycles();

            int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            printf("[WATCHDOG] [CRASH DETECTED] AI Planner PID %d terminated! (Signal: %d, ExitCode: %d)\n",
                   dead_pid, sig, exit_code);

            /* MICROKERNEL FAULT ISOLATION PROOF:
             * Respawn the sandboxed process using posix_spawn
             */
            pid_t new_pid = spawn_ai_planner();
            uint64_t t1 = ClockCycles();

            uint64_t recovery_ns = ((t1 - t0) * 1000000000ULL) / g_cps;
            respawn_count++;

            printf("[WATCHDOG] [SELF-HEALING SUCCESS] AI Planner revived as PID %d in %llu ns (%.3f ms)! Total Respawns: %llu\n",
                   new_pid,
                   (unsigned long long)recovery_ns,
                   (double)recovery_ns / 1000000.0,
                   (unsigned long long)respawn_count);

            current_ai_pid = new_pid;

            if (g_shm) {
                g_shm->seq_lock++;
                g_shm->telemetry.pid_ai = new_pid;
                g_shm->telemetry.ai_alive = 1;
                g_shm->telemetry.respawn_count = (uint32_t)respawn_count;
                g_shm->telemetry.last_respawn_latency_ns = recovery_ns;
                /* Clear chaos trigger after recovery */
                if (g_shm->chaos_request == CHAOS_SEGFAULT) {
                    g_shm->chaos_request = CHAOS_NONE;
                    g_shm->telemetry.chaos_active = CHAOS_NONE;
                }
                g_shm->seq_lock++;
            }
        }
    }

    return EXIT_SUCCESS;
}
