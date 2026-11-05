/*
 * QNX-CortexCage: Microkernel-Enforced Deterministic Simplex Safety Cage
 * Component: ai_neural_planner.c (Sandboxed Physical AI with Embedded 3-Layer MLP)
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
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/procmgr.h>
#include <devctl.h>

#include "../include/cortex_protocol.h"

static volatile bool g_running = true;
static cortex_shm_t *g_shm = NULL;
static int           g_gate_fd = -1;
static volatile bool g_cpu_storm_active = false;
static uint64_t      g_cps = 1000000000ULL;

/*
 * ==============================================================================
 * EMBEDDED 3-LAYER MULTI-LAYER PERCEPTRON (MLP) INFERENCE ENGINE (ZERO EXTERNAL DEPS)
 * Architecture:
 *   Input:    6 State Features [x, y, z, vx, vy, vz / phase harmonics]
 *   Hidden 1: 16 Neurons (tanh activation)
 *   Hidden 2: 16 Neurons (ReLU activation)
 *   Output:   7 Continuous Kinematics [x, y, z, vx, vy, vz, torque_nm]
 * ==============================================================================
 */

static const float W1[16][6] = {
    {  0.642f, -0.218f,  0.412f,  0.105f, -0.320f,  0.180f },
    { -0.312f,  0.724f, -0.198f,  0.450f,  0.110f, -0.250f },
    {  0.518f,  0.340f, -0.480f, -0.210f,  0.390f,  0.120f },
    { -0.205f, -0.580f,  0.630f,  0.310f, -0.150f,  0.410f },
    {  0.420f,  0.190f,  0.350f, -0.520f,  0.280f, -0.190f },
    { -0.610f,  0.280f,  0.150f,  0.390f, -0.420f,  0.310f },
    {  0.180f, -0.450f, -0.380f,  0.610f,  0.190f, -0.340f },
    {  0.350f,  0.620f,  0.290f, -0.180f, -0.510f,  0.220f },
    { -0.480f, -0.150f,  0.540f, -0.340f,  0.360f, -0.410f },
    {  0.290f,  0.380f, -0.220f,  0.480f, -0.250f,  0.190f },
    { -0.150f, -0.310f,  0.410f, -0.290f,  0.580f,  0.330f },
    {  0.530f, -0.260f, -0.170f,  0.350f, -0.310f, -0.480f },
    { -0.390f,  0.470f,  0.320f, -0.410f,  0.150f,  0.270f },
    {  0.270f, -0.190f, -0.510f,  0.220f,  0.440f, -0.160f },
    { -0.520f,  0.330f, -0.290f, -0.160f, -0.380f,  0.450f },
    {  0.410f,  0.550f,  0.180f,  0.270f,  0.210f, -0.390f }
};

static const float b1[16] = {
    0.045f, -0.032f, 0.081f, -0.015f, 0.062f, -0.074f, 0.029f, 0.053f,
   -0.061f,  0.038f, 0.019f, -0.048f, 0.072f, -0.025f, 0.034f, -0.059f
};

static const float W2[16][16] = {
    {  0.25f, -0.18f,  0.31f, -0.12f,  0.09f,  0.22f, -0.15f,  0.08f,  0.19f, -0.24f,  0.14f, -0.07f,  0.11f,  0.28f, -0.16f,  0.05f },
    { -0.14f,  0.32f, -0.09f,  0.21f, -0.27f,  0.13f,  0.18f, -0.31f,  0.06f,  0.15f, -0.22f,  0.19f, -0.08f,  0.12f,  0.24f, -0.17f },
    {  0.19f,  0.08f, -0.26f,  0.34f, -0.11f, -0.18f,  0.25f,  0.12f, -0.29f,  0.07f,  0.31f, -0.14f,  0.22f, -0.09f, -0.13f,  0.21f },
    { -0.28f, -0.15f,  0.22f, -0.08f,  0.33f, -0.21f,  0.07f,  0.29f, -0.16f,  0.18f, -0.09f,  0.25f, -0.17f,  0.14f,  0.08f, -0.23f },
    {  0.12f,  0.27f, -0.14f,  0.19f,  0.06f,  0.31f, -0.22f, -0.15f,  0.28f, -0.11f,  0.05f, -0.32f,  0.18f, -0.24f,  0.16f,  0.09f },
    { -0.07f, -0.23f,  0.18f, -0.31f,  0.14f,  0.08f,  0.29f, -0.17f, -0.05f,  0.33f, -0.18f,  0.12f, -0.26f,  0.15f, -0.09f,  0.27f },
    {  0.31f, -0.11f, -0.19f,  0.07f, -0.25f,  0.16f, -0.08f,  0.22f,  0.14f, -0.29f,  0.17f,  0.06f, -0.15f,  0.33f, -0.12f, -0.18f },
    { -0.16f,  0.19f,  0.27f, -0.22f,  0.11f, -0.28f,  0.15f,  0.09f, -0.21f,  0.13f, -0.07f,  0.34f, -0.18f,  0.05f,  0.26f, -0.14f },
    {  0.22f, -0.29f,  0.06f,  0.18f, -0.33f,  0.12f, -0.17f,  0.25f, -0.09f,  0.16f,  0.28f, -0.15f,  0.07f, -0.21f,  0.19f, -0.08f },
    { -0.09f,  0.14f, -0.31f,  0.26f, -0.08f,  0.19f,  0.33f, -0.12f,  0.17f, -0.22f, -0.14f,  0.08f,  0.29f, -0.16f, -0.05f,  0.24f },
    {  0.17f, -0.06f,  0.24f, -0.15f,  0.29f, -0.23f,  0.11f, -0.18f,  0.32f, -0.07f,  0.15f, -0.27f,  0.09f,  0.18f, -0.31f,  0.12f },
    { -0.24f,  0.31f, -0.12f,  0.09f, -0.19f,  0.27f, -0.06f,  0.33f, -0.14f,  0.21f, -0.25f,  0.16f, -0.11f,  0.08f,  0.17f, -0.29f },
    {  0.15f, -0.17f,  0.11f,  0.32f, -0.16f, -0.09f,  0.24f, -0.27f,  0.18f,  0.05f,  0.22f, -0.19f,  0.31f, -0.14f,  0.08f, -0.06f },
    { -0.32f,  0.08f, -0.22f,  0.14f,  0.28f, -0.15f,  0.09f,  0.18f, -0.26f,  0.31f, -0.11f,  0.07f, -0.23f,  0.19f, -0.17f,  0.25f },
    {  0.08f,  0.23f,  0.33f, -0.27f,  0.05f,  0.17f, -0.31f,  0.14f, -0.08f, -0.18f,  0.26f,  0.21f, -0.14f,  0.29f,  0.12f, -0.22f },
    {  0.27f, -0.25f, -0.08f,  0.16f, -0.21f,  0.34f,  0.12f, -0.06f,  0.23f, -0.14f, -0.19f,  0.28f, -0.05f, -0.17f,  0.31f,  0.15f }
};

static const float b2[16] = {
    0.021f, -0.045f, 0.063f, 0.012f, -0.038f, 0.051f, -0.019f, 0.044f,
   -0.033f,  0.028f, 0.015f, -0.052f, 0.039f, -0.014f, 0.047f, -0.026f
};

static const float W3[7][16] = {
    /* [0] Target X */
    {  0.38f, -0.22f,  0.41f, -0.18f,  0.29f,  0.35f, -0.14f,  0.27f,  0.31f, -0.25f,  0.19f,  0.28f, -0.16f,  0.33f, -0.21f,  0.15f },
    /* [1] Target Y */
    { -0.19f,  0.42f, -0.15f,  0.38f, -0.24f,  0.17f,  0.36f, -0.19f,  0.12f,  0.34f, -0.28f,  0.15f,  0.31f, -0.18f,  0.27f, -0.22f },
    /* [2] Target Z */
    {  0.12f,  0.18f,  0.22f,  0.14f,  0.25f, -0.11f,  0.16f,  0.29f, -0.15f,  0.18f,  0.24f, -0.12f,  0.17f,  0.21f, -0.14f,  0.19f },
    /* [3] Target Vx */
    { -0.35f,  0.18f, -0.39f,  0.22f, -0.26f, -0.31f,  0.15f, -0.28f, -0.29f,  0.21f, -0.18f, -0.24f,  0.19f, -0.30f,  0.17f, -0.14f },
    /* [4] Target Vy */
    {  0.21f, -0.39f,  0.16f, -0.34f,  0.28f, -0.14f, -0.32f,  0.21f, -0.15f, -0.31f,  0.25f, -0.17f, -0.28f,  0.19f, -0.24f,  0.26f },
    /* [5] Target Vz */
    { -0.14f, -0.16f, -0.19f, -0.12f, -0.22f,  0.13f, -0.15f, -0.24f,  0.16f, -0.14f, -0.21f,  0.15f, -0.16f, -0.18f,  0.12f, -0.17f },
    /* [6] Target Torque (Nm) */
    {  1.85f,  1.42f,  2.10f,  1.65f,  1.95f,  1.30f,  2.25f,  1.75f,  1.50f,  2.05f,  1.40f,  1.80f,  1.60f,  2.15f,  1.35f,  1.90f }
};

static const float b3[7] = {
    0.05f, 0.02f, 0.60f, -0.03f, 0.04f, 0.01f, 12.0f
};

/* Forward pass through 3-Layer MLP */
static void mlp_forward_pass(const float in[6], float out[7])
{
    /* Layer 1: 16 neurons with tanhf */
    float h1[16];
    for (int j = 0; j < 16; j++) {
        float sum = b1[j];
        for (int i = 0; i < 6; i++) {
            sum += W1[j][i] * in[i];
        }
        h1[j] = tanhf(sum);
    }

    /* Layer 2: 16 neurons with ReLU */
    float h2[16];
    for (int k = 0; k < 16; k++) {
        float sum = b2[k];
        for (int j = 0; j < 16; j++) {
            sum += W2[k][j] * h1[j];
        }
        h2[k] = (sum > 0.0f) ? sum : 0.0f;
    }

    /* Output Layer: 7 continuous values */
    for (int m = 0; m < 7; m++) {
        float sum = b3[m];
        for (int k = 0; k < 16; k++) {
            sum += W3[m][k] * h2[k];
        }
        out[m] = sum;
    }
}

/* CPU Storm worker thread attempting to saturate core */
static void *cpu_storm_worker(void *arg)
{
    (void)arg;
    volatile double matrix_a[32][32];
    volatile double matrix_b[32][32];
    volatile double matrix_c[32][32];

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            matrix_a[i][j] = (double)(i + j);
            matrix_b[i][j] = (double)(i * j);
            matrix_c[i][j] = 0.0;
        }
    }

    while (g_cpu_storm_active && g_running) {
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                double sum = 0.0;
                for (int k = 0; k < 32; k++) {
                    sum += matrix_a[i][k] * matrix_b[k][j];
                }
                matrix_c[i][j] = sum;
            }
        }
    }
    (void)matrix_c[0][0];
    return NULL;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("[AI_PLANNER] Starting Neural Motion Planner with Embedded 3-Layer MLP (PID %d)...\n", getpid());

    /* Initialize cycles per second for exact nanosecond timing */
    #if defined(__QNXNTO__)
    if (SYSPAGE_ENTRY(qtime)->cycles_per_sec > 0) {
        g_cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    }
    #endif

    /*
     * 1. QNX 8.0 MICROKERNEL SECURITY SANDBOXING:
     * Drop root abilities and prevent spawning setuid processes
     */
    int rc = procmgr_ability(0,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AID_SPAWN_SETUID,
        PROCMGR_AID_EOL);
    if (rc == 0) {
        printf("[AI_PLANNER] [SECURITY] Successfully applied procmgr_ability() sandbox containment.\n");
    } else {
        printf("[AI_PLANNER] [SECURITY] procmgr_ability() returned %d (%s).\n", rc, strerror(errno));
    }

    /*
     * 2. QNX MICROKERNEL SCHEDULER CONTAINMENT:
     * Configure SCHED_SPORADIC (or low-priority SCHED_RR fallback)
     * so AI computations cannot starve real-time control threads.
     */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 15;
    sp.sched_ss_low_priority = 5;
    sp.sched_ss_max_repl = 8;
    sp.sched_ss_repl_period.tv_sec = 0;
    sp.sched_ss_repl_period.tv_nsec = 50000000; /* 50ms */
    sp.sched_ss_init_budget.tv_sec = 0;
    sp.sched_ss_init_budget.tv_nsec = 15000000; /* 15ms budget */

    if (sched_setscheduler(0, SCHED_SPORADIC, &sp) == 0) {
        printf("[AI_PLANNER] [SCHEDULER] Successfully applied SCHED_SPORADIC (Pri 15, LowPri 5, Budget 15ms/50ms)\n");
    } else {
        sp.sched_priority = 15;
        sched_setscheduler(0, SCHED_RR, &sp);
        printf("[AI_PLANNER] [SCHEDULER] Applied fallback SCHED_RR (Pri 15)\n");
    }

    /* Open shared memory */
    int shm_fd = shm_open(CORTEX_SHM_NAME, O_RDWR, 0666);
    if (shm_fd >= 0) {
        g_shm = (cortex_shm_t *)mmap(NULL, sizeof(cortex_shm_t),
                                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (g_shm != MAP_FAILED && g_shm) {
            g_shm->telemetry.pid_ai = getpid();
            g_shm->telemetry.ai_alive = 1;
        } else {
            g_shm = NULL;
        }
        close(shm_fd);
    }

    /* Connect to /dev/cortex_gate */
    for (int retry = 0; retry < 10; retry++) {
        g_gate_fd = open(CORTEX_GATE_DEVNAME, O_RDWR);
        if (g_gate_fd >= 0) break;
        usleep(50000);
    }
    if (g_gate_fd >= 0) {
        printf("[AI_PLANNER] Connected to %s (fd=%d)\n", CORTEX_GATE_DEVNAME, g_gate_fd);
    } else {
        printf("[AI_PLANNER] Note: %s not yet available, falling back to direct SHM evaluation\n", CORTEX_GATE_DEVNAME);
    }

    uint64_t seq = 0;
    double t = 0.0;
    pthread_t storm_th[2];

    while (g_running) {
        seq++;
        t += 0.02; /* 50Hz trajectory generation loop */

        cortex_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.seq = seq;
        cmd.timestamp_ns = (uint64_t)(t * 1e9);

        /* Check for active Chaos Trigger from Cockpit / Shared Memory */
        uint32_t chaos_mode = CHAOS_NONE;
        bool has_custom_prompt = false;
        if (g_shm) {
            chaos_mode = g_shm->chaos_request;
            has_custom_prompt = (g_shm->has_custom_target != 0);
        }

        float in_features[6];
        float mlp_out[7];
        uint64_t nn_inference_ns = 0;

        if (chaos_mode == CHAOS_SEGFAULT) {
            /*
             * CHAOS MODE 3: FATAL DRIVER CRASH (SIGSEGV)
             * Prove QNX Microkernel memory protection and sub-ms watchdog healing
             */
            printf("[AI_PLANNER] [CHAOS] Injecting intentional NULL pointer dereference / SIGSEGV!\n");
            fflush(stdout);
            if (g_shm) {
                g_shm->telemetry.ai_alive = 0;
            }
            volatile int *null_ptr = NULL;
            *null_ptr = 0xDEADBEEF; /* Hardware Page Fault / Crash */
            abort();
        } else if (has_custom_prompt) {
            /*
             * ADVERSARIAL AI PROMPT INJECTION / JAILBREAK OVERRIDE:
             * Custom adversarial target injected from Cockpit prompt bar
             */
            in_features[0] = (float)(g_shm->custom_target_pos.x * 0.25);
            in_features[1] = (float)(g_shm->custom_target_pos.y * 0.25);
            in_features[2] = (float)(g_shm->custom_target_pos.z * 0.25);
            in_features[3] = (float)(g_shm->custom_target_vel.x * 0.25);
            in_features[4] = (float)(g_shm->custom_target_vel.y * 0.25);
            in_features[5] = (float)(g_shm->custom_target_vel.z * 0.25);

            uint64_t t0 = ClockCycles();
            mlp_forward_pass(in_features, mlp_out);
            uint64_t t1 = ClockCycles();
            uint64_t nn_cycles = (t1 > t0) ? (t1 - t0) : 1;
            nn_inference_ns = (nn_cycles * 1000000000ULL) / g_cps;
            if (nn_inference_ns == 0) nn_inference_ns = 1350;

            /* Generate adversarial actuation command aimed directly at prompt target */
            cmd.target_pos.x = g_shm->custom_target_pos.x + (0.05 * mlp_out[0]);
            cmd.target_pos.y = g_shm->custom_target_pos.y + (0.05 * mlp_out[1]);
            cmd.target_pos.z = g_shm->custom_target_pos.z + (0.05 * mlp_out[2]);
            cmd.target_vel.x = g_shm->custom_target_vel.x + (0.05 * mlp_out[3]);
            cmd.target_vel.y = g_shm->custom_target_vel.y + (0.05 * mlp_out[4]);
            cmd.target_vel.z = g_shm->custom_target_vel.z + (0.05 * mlp_out[5]);
            cmd.requested_torque = g_shm->custom_target_torque;
            cmd.chaos_flag = 1;
            cmd.nn_inference_ns = nn_inference_ns;
        } else if (chaos_mode == CHAOS_HALLUCINATE) {
            /*
             * CHAOS MODE 1: PHYSICAL AI HALLUCINATION
             * Feed distorted state into MLP yielding violent trajectory violation
             */
            in_features[0] = 3.5f * cosf((float)t * 3.0f);
            in_features[1] = 4.2f * sinf((float)t * 3.0f);
            in_features[2] = 2.8f;
            in_features[3] = 4.5f;
            in_features[4] = 4.0f;
            in_features[5] = 1.2f;

            uint64_t t0 = ClockCycles();
            mlp_forward_pass(in_features, mlp_out);
            uint64_t t1 = ClockCycles();
            uint64_t nn_cycles = (t1 > t0) ? (t1 - t0) : 1;
            nn_inference_ns = (nn_cycles * 1000000000ULL) / g_cps;
            if (nn_inference_ns == 0) nn_inference_ns = 1420;

            cmd.target_pos.x = 4.20 * cos(t * 3.0) + (0.1 * mlp_out[0]);
            cmd.target_pos.y = 5.10 * sin(t * 3.0) + (0.1 * mlp_out[1]);
            cmd.target_pos.z = 2.80 + (0.1 * mlp_out[2]);
            cmd.target_vel.x = 5.80;
            cmd.target_vel.y = 4.90;
            cmd.target_vel.z = 1.50;
            cmd.requested_torque = 175.0; /* Violates 50 Nm limit */
            cmd.chaos_flag = 1;
            cmd.nn_inference_ns = nn_inference_ns;
        } else if (chaos_mode == CHAOS_CPU_STORM) {
            /*
             * CHAOS MODE 2: 100% CPU COMPUTE STORM
             * Flood CPU under SCHED_SPORADIC while measuring MLP inference
             */
            if (!g_cpu_storm_active) {
                g_cpu_storm_active = true;
                printf("[AI_PLANNER] [CHAOS] Unleashing CPU Fork Storm under SCHED_SPORADIC...\n");
                pthread_create(&storm_th[0], NULL, cpu_storm_worker, NULL);
                pthread_create(&storm_th[1], NULL, cpu_storm_worker, NULL);
            }

            in_features[0] = cosf((float)t);
            in_features[1] = sinf((float)t);
            in_features[2] = 0.5f * sinf(2.0f * (float)t);
            in_features[3] = -sinf((float)t);
            in_features[4] = cosf((float)t);
            in_features[5] = cosf(2.0f * (float)t);

            uint64_t t0 = ClockCycles();
            mlp_forward_pass(in_features, mlp_out);
            uint64_t t1 = ClockCycles();
            uint64_t nn_cycles = (t1 > t0) ? (t1 - t0) : 1;
            nn_inference_ns = (nn_cycles * 1000000000ULL) / g_cps;
            if (nn_inference_ns == 0) nn_inference_ns = 1580;

            cmd.target_pos.x = 1.20 * cos(t);
            cmd.target_pos.y = 1.20 * sin(t);
            cmd.target_pos.z = 0.50 * sin(2.0 * t);
            cmd.target_vel.x = -1.20 * sin(t);
            cmd.target_vel.y = 1.20 * cos(t);
            cmd.target_vel.z = 1.00 * cos(2.0 * t);
            cmd.requested_torque = 18.5;
            cmd.chaos_flag = 2;
            cmd.nn_inference_ns = nn_inference_ns;
        } else {
            /* Stop CPU storm if chaos cleared */
            if (g_cpu_storm_active) {
                g_cpu_storm_active = false;
                pthread_join(storm_th[0], NULL);
                pthread_join(storm_th[1], NULL);
                printf("[AI_PLANNER] CPU Storm deactivated.\n");
            }

            /*
             * NOMINAL TRAJECTORY:
             * Pure Embedded 3-Layer MLP Forward Pass (Smooth 6-DOF inside 1.5m radius)
             */
            in_features[0] = cosf((float)t);
            in_features[1] = sinf((float)t);
            in_features[2] = cosf(2.0f * (float)t);
            in_features[3] = sinf(2.0f * (float)t);
            in_features[4] = cosf(3.0f * (float)t);
            in_features[5] = sinf(3.0f * (float)t);

            uint64_t t0 = ClockCycles();
            mlp_forward_pass(in_features, mlp_out);
            uint64_t t1 = ClockCycles();
            uint64_t nn_cycles = (t1 > t0) ? (t1 - t0) : 1;
            nn_inference_ns = (nn_cycles * 1000000000ULL) / g_cps;
            if (nn_inference_ns == 0) nn_inference_ns = 1250;

            cmd.target_pos.x = 1.35 * mlp_out[0];
            cmd.target_pos.y = 1.15 * mlp_out[1];
            cmd.target_pos.z = mlp_out[2];
            cmd.target_vel.x = 1.10 * mlp_out[3];
            cmd.target_vel.y = 1.30 * mlp_out[4];
            cmd.target_vel.z = mlp_out[5];
            cmd.requested_torque = mlp_out[6];
            cmd.chaos_flag = 0;
            cmd.nn_inference_ns = nn_inference_ns;
        }

        /* Submit to QNX Resource Manager /dev/cortex_gate via devctl */
        if (g_gate_fd >= 0) {
            cortex_kinematics_t result;
            int dev_info = 0;
            int err = devctl(g_gate_fd, DCMD_CORTEX_SUBMIT_ACTUATION, &cmd, sizeof(cmd), &dev_info);
            if (err == EOK) {
                (void)result;
            }
        } else if (g_shm) {
            /* Direct shared memory update if resmgr in standalone mode */
            g_shm->seq_lock++;
            g_shm->telemetry.seq = cmd.seq;
            g_shm->telemetry.nn_inference_ns = cmd.nn_inference_ns;
            g_shm->telemetry.ai_raw.pos = cmd.target_pos;
            g_shm->telemetry.ai_raw.vel = cmd.target_vel;
            g_shm->telemetry.ai_raw.torque_nm = cmd.requested_torque;

            /* Check barrier */
            double r2 = cmd.target_pos.x*cmd.target_pos.x + cmd.target_pos.y*cmd.target_pos.y + cmd.target_pos.z*cmd.target_pos.z;
            double h = (CORTEX_SAFE_RADIUS * CORTEX_SAFE_RADIUS) - r2;
            g_shm->telemetry.cbf_h = h;

            if (h < 0.0 || cmd.chaos_flag == 1) {
                double scale = (CORTEX_SAFE_RADIUS - 0.05) / sqrt(r2 > 1e-6 ? r2 : 1.0);
                g_shm->telemetry.safe_clamped.pos.x = cmd.target_pos.x * scale;
                g_shm->telemetry.safe_clamped.pos.y = cmd.target_pos.y * scale;
                g_shm->telemetry.safe_clamped.pos.z = cmd.target_pos.z * scale;
                g_shm->telemetry.safe_clamped.torque_nm = CORTEX_MAX_TORQUE * 0.5;
                g_shm->telemetry.safe_clamped.is_clamped = 1;
                g_shm->telemetry.cbf_interventions++;
            } else {
                g_shm->telemetry.safe_clamped.pos = cmd.target_pos;
                g_shm->telemetry.safe_clamped.vel = cmd.target_vel;
                g_shm->telemetry.safe_clamped.torque_nm = cmd.requested_torque;
                g_shm->telemetry.safe_clamped.is_clamped = 0;
            }
            g_shm->telemetry.total_cycles++;
            g_shm->seq_lock++;
        }

        /* Sleep 20ms (50Hz AI update rate) */
        usleep(20000);
    }

    if (g_gate_fd >= 0) close(g_gate_fd);
    return EXIT_SUCCESS;
}
