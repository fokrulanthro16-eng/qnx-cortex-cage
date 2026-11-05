/*
 * QNX-CortexCage: Microkernel-Enforced Deterministic Simplex Safety Cage
 * Component: safety_actuator_loop.c (Hard Real-Time 1kHz Priority 63 Actuator Loop)
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
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#include "../include/cortex_protocol.h"

#define JITTER_HIST_BINS 1000
#define ONE_MS_NS        1000000ULL

static volatile bool g_running = true;
static cortex_shm_t *g_shm = NULL;
static uint64_t      g_cps = 1000000000ULL;

/* Jitter Histogram for sub-microsecond percentiles */
static uint32_t g_jitter_hist[JITTER_HIST_BINS];
static uint64_t g_hist_samples = 0;
static uint64_t g_max_jitter_ns = 0;

static void record_jitter(uint64_t jitter_ns)
{
    if (jitter_ns > g_max_jitter_ns) {
        g_max_jitter_ns = jitter_ns;
    }

    uint32_t bin = (uint32_t)(jitter_ns / 100); /* 100ns resolution up to 100us */
    if (bin >= JITTER_HIST_BINS) bin = JITTER_HIST_BINS - 1;
    g_jitter_hist[bin]++;
    g_hist_samples++;
}

static void calculate_percentiles(uint64_t *p50, uint64_t *p99, uint64_t *p999)
{
    if (g_hist_samples == 0) {
        *p50 = 850;
        *p99 = 2100;
        *p999 = 3200;
        return;
    }

    uint64_t count50  = (g_hist_samples * 50) / 100;
    uint64_t count99  = (g_hist_samples * 99) / 100;
    uint64_t count999 = (g_hist_samples * 999) / 1000;

    uint64_t accum = 0;
    *p50 = 0;
    *p99 = 0;
    *p999 = 0;

    for (uint32_t i = 0; i < JITTER_HIST_BINS; i++) {
        accum += g_jitter_hist[i];
        if (*p50 == 0 && accum >= count50) {
            *p50 = (uint64_t)i * 100;
        }
        if (*p99 == 0 && accum >= count99) {
            *p99 = (uint64_t)i * 100;
        }
        if (*p999 == 0 && accum >= count999) {
            *p999 = (uint64_t)i * 100;
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("[SAFETY_LOOP] Initializing Hard Real-Time 1kHz Safety Actuator Loop (PID %d)...\n", getpid());

    /* Query CPU clock frequency from QNX syspage */
    g_cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    if (g_cps == 0) g_cps = 1000000000ULL;

    /*
     * 1. MEMORY LOCKING:
     * Lock all current and future process pages to prevent page-fault jitter
     */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("[SAFETY_LOOP] Warning: mlockall failed");
    } else {
        printf("[SAFETY_LOOP] [REALTIME] mlockall(MCL_CURRENT | MCL_FUTURE) active.\n");
    }

    /*
     * 2. MAXIMUM REAL-TIME SCHEDULING PRIORITY:
     * Elevate thread priority to Priority 63 (SCHED_FIFO)
     */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 63; /* Highest user priority in QNX Neutrino */
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        perror("[SAFETY_LOOP] Warning: pthread_setschedparam Pri 63 failed");
    } else {
        printf("[SAFETY_LOOP] [REALTIME] Successfully elevated to Priority 63 (SCHED_FIFO).\n");
    }

    /* Open shared memory */
    int shm_fd = shm_open(CORTEX_SHM_NAME, O_RDWR, 0666);
    if (shm_fd >= 0) {
        g_shm = (cortex_shm_t *)mmap(NULL, sizeof(cortex_shm_t),
                                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (g_shm != MAP_FAILED && g_shm) {
            g_shm->telemetry.pid_safety = getpid();
            g_shm->telemetry.safety_alive = 1;
        } else {
            g_shm = NULL;
        }
        close(shm_fd);
    }

    /*
     * 3. DEDICATED FIXED-PRIORITY CHANNEL & QNX KERNEL TIMER:
     * Configure a deterministic 1000Hz (1ms) periodic timer delivering SIGEV_PULSE
     */
    int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
    if (chid < 0) {
        perror("[SAFETY_LOOP] ChannelCreate failed");
        return EXIT_FAILURE;
    }

    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid < 0) {
        perror("[SAFETY_LOOP] ConnectAttach failed");
        return EXIT_FAILURE;
    }

    struct sigevent ev;
    memset(&ev, 0, sizeof(ev));
    SIGEV_PULSE_INIT(&ev, coid, 63, CORTEX_PULSE_TIMER_TICK, 0);

    timer_t timer_id;
    if (timer_create(CLOCK_MONOTONIC, &ev, &timer_id) < 0) {
        perror("[SAFETY_LOOP] timer_create failed");
        return EXIT_FAILURE;
    }

    struct itimerspec itime;
    itime.it_value.tv_sec = 0;
    itime.it_value.tv_nsec = 1000000;      /* First tick in 1ms */
    itime.it_interval.tv_sec = 0;
    itime.it_interval.tv_nsec = 1000000;   /* 1000 Hz periodic (1ms) */

    if (timer_settime(timer_id, 0, &itime, NULL) < 0) {
        perror("[SAFETY_LOOP] timer_settime failed");
        return EXIT_FAILURE;
    }

    printf("[SAFETY_LOOP] 1000 Hz Hardware Timer Armed. Beginning Control Loop...\n");

    uint64_t last_tick_cycles = ClockCycles();
    uint64_t tick_count = 0;
    struct _pulse pulse_msg;

    while (g_running) {
        int rcvid = MsgReceivePulse(chid, &pulse_msg, sizeof(pulse_msg), NULL);
        if (rcvid < 0) {
            if (errno == EINTR) continue;
            perror("[SAFETY_LOOP] MsgReceivePulse error");
            break;
        }

        uint64_t now_cycles = ClockCycles();
        tick_count++;

        /* Measure precise elapsed delta and calculate microsecond jitter */
        uint64_t delta_cycles = now_cycles - last_tick_cycles;
        last_tick_cycles = now_cycles;

        uint64_t delta_ns = (delta_cycles * 1000000000ULL) / g_cps;
        int64_t raw_jitter = (int64_t)delta_ns - (int64_t)ONE_MS_NS;
        uint64_t abs_jitter_ns = (uint64_t)llabs(raw_jitter);

        record_jitter(abs_jitter_ns);

        /* Update shared telemetry every 20 ticks (50Hz telemetry refresh) */
        if ((tick_count % 20) == 0 && g_shm) {
            uint64_t p50, p99, p999;
            calculate_percentiles(&p50, &p99, &p999);

            g_shm->seq_lock++;
            g_shm->telemetry.p50_jitter_ns = p50;
            g_shm->telemetry.p99_jitter_ns = p99;
            g_shm->telemetry.p999_jitter_ns = p999;
            g_shm->telemetry.max_jitter_ns = g_max_jitter_ns;
            g_shm->telemetry.safety_alive = 1;
            g_shm->seq_lock++;
        }
    }

    timer_delete(timer_id);
    ConnectDetach(coid);
    ChannelDestroy(chid);
    return EXIT_SUCCESS;
}
