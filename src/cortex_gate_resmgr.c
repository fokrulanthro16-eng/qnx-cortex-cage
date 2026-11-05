/*
 * QNX-CortexCage: Microkernel-Enforced Deterministic Simplex Safety Cage
 * Component: cortex_gate_resmgr.c (Native QNX Resource Manager & Simplex Safety Filter)
 * Target: QNX Neutrino RTOS 8.0 (x86_64, aarch64le)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/resmgr.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#include "../include/cortex_protocol.h"

/* Resource Manager State */
static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t      io_funcs;
static iofunc_attr_t          attr;
static dispatch_t            *dpp = NULL;
static dispatch_context_t    *d_ctp = NULL;
static cortex_shm_t          *g_shm = NULL;
static int                    g_shm_fd = -1;
static uint64_t               g_cps = 1000000000ULL;
static int                    g_resmgr_id = -1;

/* Forward Declarations */
static int cortex_io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb);
static int cortex_io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb);
static int cortex_io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb);

/* Evaluate Control Barrier Function (CBF) & apply Simplex Projection */
static void evaluate_and_clamp_cbf(const cortex_command_t *cmd,
                                  cortex_kinematics_t *raw_out,
                                  cortex_kinematics_t *safe_out)
{
    uint64_t t0 = ClockCycles();

    /* Fill raw state from AI planner */
    raw_out->pos = cmd->target_pos;
    raw_out->vel = cmd->target_vel;
    raw_out->torque_nm = cmd->requested_torque;
    raw_out->acc.x = 0.0;
    raw_out->acc.y = 0.0;
    raw_out->acc.z = 0.0;

    double x = cmd->target_pos.x;
    double y = cmd->target_pos.y;
    double z = cmd->target_pos.z;
    double vx = cmd->target_vel.x;
    double vy = cmd->target_vel.y;
    double vz = cmd->target_vel.z;

    double r2 = x*x + y*y + z*z;
    double r = sqrt(r2);
    double v_mag = sqrt(vx*vx + vy*vy + vz*vz);
    double radial_vel = (r > 1e-6) ? (x*vx + y*vy + z*vz) / r : 0.0;

    /*
     * Control Barrier Function:
     * h(x) = R_safe^2 - r^2 - alpha * (x*vx + y*vy + z*vz)
     */
    double h = (CORTEX_SAFE_RADIUS * CORTEX_SAFE_RADIUS) - r2 - (CORTEX_CBF_ALPHA * (x*vx + y*vy + z*vz));
    raw_out->cbf_h = h;
    raw_out->is_clamped = 0;

    /* Check physical cage safety violation */
    bool violates_barrier  = (h < 0.0) || (r > CORTEX_SAFE_RADIUS);
    bool violates_velocity = (v_mag > CORTEX_MAX_VELOCITY);
    bool violates_torque   = (fabs(cmd->requested_torque) > CORTEX_MAX_TORQUE);

    if (!violates_barrier && !violates_velocity && !violates_torque && (cmd->chaos_flag == 0)) {
        /* Safe AI actuation pass-through */
        *safe_out = *raw_out;
        safe_out->is_clamped = 0;
    } else {
        /*
         * MICROKERNEL SIMPLEX CAGE INTERVENTION:
         * Project unsafe vector onto safe tangent cone and clamp torque
         */
        safe_out->pos = raw_out->pos;
        safe_out->vel = raw_out->vel;

        if (r > (CORTEX_SAFE_RADIUS - 0.05)) {
            double scale = (CORTEX_SAFE_RADIUS - 0.05) / (r > 1e-6 ? r : 1.0);
            safe_out->pos.x = x * scale;
            safe_out->pos.y = y * scale;
            safe_out->pos.z = z * scale;
        }

        /* Remove outward radial velocity component */
        if (radial_vel > 0.0 && r > 1e-6) {
            double nx = x / r;
            double ny = y / r;
            double nz = z / r;
            safe_out->vel.x = vx - (radial_vel * nx);
            safe_out->vel.y = vy - (radial_vel * ny);
            safe_out->vel.z = vz - (radial_vel * nz);
        }

        /* Clamp velocity magnitude to safe margin */
        double new_v = sqrt(safe_out->vel.x * safe_out->vel.x +
                            safe_out->vel.y * safe_out->vel.y +
                            safe_out->vel.z * safe_out->vel.z);
        if (new_v > (CORTEX_MAX_VELOCITY * 0.75)) {
            double v_scale = (CORTEX_MAX_VELOCITY * 0.75) / (new_v > 1e-6 ? new_v : 1.0);
            safe_out->vel.x *= v_scale;
            safe_out->vel.y *= v_scale;
            safe_out->vel.z *= v_scale;
        }

        /* Clamp torque */
        if (cmd->requested_torque > CORTEX_MAX_TORQUE) {
            safe_out->torque_nm = CORTEX_MAX_TORQUE * 0.6;
        } else if (cmd->requested_torque < -CORTEX_MAX_TORQUE) {
            safe_out->torque_nm = -CORTEX_MAX_TORQUE * 0.6;
        } else {
            safe_out->torque_nm = cmd->requested_torque * 0.8;
        }

        safe_out->cbf_h = h;
        safe_out->is_clamped = 1;
    }

    uint64_t t1 = ClockCycles();
    uint64_t eval_ns = ((t1 - t0) * 1000000000ULL) / g_cps;

    /* Lock-free atomic sequence update of shared telemetry */
    if (g_shm) {
        g_shm->seq_lock++;
        g_shm->telemetry.seq = cmd->seq;
        g_shm->telemetry.timestamp_ns = cmd->timestamp_ns;
        g_shm->telemetry.ai_raw = *raw_out;
        g_shm->telemetry.safe_clamped = *safe_out;
        g_shm->telemetry.cbf_h = safe_out->cbf_h;
        if (safe_out->is_clamped) {
            g_shm->telemetry.cbf_interventions++;
        }
        g_shm->telemetry.total_cycles++;
        g_shm->telemetry.last_eval_ns = eval_ns;
        g_shm->telemetry.nn_inference_ns = cmd->nn_inference_ns;
        g_shm->telemetry.has_adversarial_prompt = g_shm->has_custom_target;
        if (g_shm->has_custom_target) {
            snprintf(g_shm->telemetry.adversarial_prompt, sizeof(g_shm->telemetry.adversarial_prompt),
                     "%s", (const char *)g_shm->adversarial_prompt);
        }
        g_shm->telemetry.pid_resmgr = getpid();
        g_shm->seq_lock++;
    }
}

/* devctl callback handling custom QNX DCMD commands */
static int cortex_io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb)
{
    int status, nbytes = 0;

    if ((status = iofunc_devctl_default(ctp, msg, ocb)) != _RESMGR_DEFAULT) {
        return status;
    }

    void *data = _DEVCTL_DATA(msg->i);

    switch (msg->i.dcmd) {
    case DCMD_CORTEX_SUBMIT_ACTUATION: {
        cortex_command_t *cmd = (cortex_command_t *)data;
        cortex_kinematics_t raw_state, safe_state;

        evaluate_and_clamp_cbf(cmd, &raw_state, &safe_state);

        /* Return safe clamped state in the reply buffer */
        memcpy(data, &safe_state, sizeof(safe_state));
        nbytes = sizeof(safe_state);
        break;
    }

    case DCMD_CORTEX_GET_TELEMETRY: {
        if (g_shm) {
            memcpy(data, (const void *)&g_shm->telemetry, sizeof(cortex_telemetry_t));
            nbytes = sizeof(cortex_telemetry_t);
        } else {
            return EIO;
        }
        break;
    }

    case DCMD_CORTEX_INJECT_CHAOS: {
        cortex_chaos_cmd_t *chaos = (cortex_chaos_cmd_t *)data;
        if (g_shm) {
            g_shm->chaos_request = chaos->chaos_mode;
            g_shm->chaos_duration_ms = chaos->duration_ms;
            g_shm->chaos_intensity = chaos->intensity;
            g_shm->telemetry.chaos_active = chaos->chaos_mode;
        }
        nbytes = 0;
        break;
    }

    case DCMD_CORTEX_INJECT_PROMPT_TARGET: {
        cortex_command_t *prompt_cmd = (cortex_command_t *)data;
        if (g_shm) {
            g_shm->has_custom_target = 1;
            g_shm->custom_target_pos = prompt_cmd->target_pos;
            g_shm->custom_target_vel = prompt_cmd->target_vel;
            g_shm->custom_target_torque = prompt_cmd->requested_torque;
            g_shm->telemetry.has_adversarial_prompt = 1;
        }
        nbytes = 0;
        break;
    }

    default:
        return ENOSYS;
    }

    memset(&msg->o, 0, sizeof(msg->o));
    msg->o.nbytes = nbytes;
    return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + nbytes);
}

/* io_read callback: returns live JSON telemetry for 'cat /dev/cortex_gate' */
static int cortex_io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
    int status;
    if ((status = iofunc_read_verify(ctp, msg, ocb, NULL)) != EOK)
        return status;

    if ((msg->i.xtype & _IO_XFLAG_BLOCK) != 0 && (msg->i.xtype & _IO_XFLAG_BLOCK) != _IO_XFLAG_BLOCK)
        return ENOSYS;

    char buf[512];
    int len = 0;
    if (g_shm) {
        cortex_telemetry_t *t = (cortex_telemetry_t *)&g_shm->telemetry;
        len = snprintf(buf, sizeof(buf),
            "{\"seq\":%llu,\"cbf_h\":%.4f,\"interventions\":%llu,\"raw\":[%.2f,%.2f,%.2f],\"safe\":[%.2f,%.2f,%.2f],\"clamped\":%u,\"eval_ns\":%llu,\"chaos\":%u}\n",
            (unsigned long long)t->seq,
            t->cbf_h,
            (unsigned long long)t->cbf_interventions,
            t->ai_raw.pos.x, t->ai_raw.pos.y, t->ai_raw.pos.z,
            t->safe_clamped.pos.x, t->safe_clamped.pos.y, t->safe_clamped.pos.z,
            t->safe_clamped.is_clamped,
            (unsigned long long)t->last_eval_ns,
            t->chaos_active);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"status\":\"cortex_gate_active\"}\n");
    }

    if (ocb->offset >= len) {
        _IO_SET_READ_NBYTES(ctp, 0);
        return _RESMGR_PTR(ctp, NULL, 0);
    }

    int to_copy = len - ocb->offset;
    if (to_copy > (int)msg->i.nbytes) to_copy = msg->i.nbytes;

    ocb->offset += to_copy;
    _IO_SET_READ_NBYTES(ctp, to_copy);
    return _RESMGR_PTR(ctp, buf + (ocb->offset - to_copy), to_copy);
}

/* io_write callback: accepts text chaos injection commands */
static int cortex_io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int status;
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK)
        return status;

    if ((msg->i.xtype & _IO_XFLAG_BLOCK) != 0 && (msg->i.xtype & _IO_XFLAG_BLOCK) != _IO_XFLAG_BLOCK)
        return ENOSYS;

    char cmd_buf[128];
    int nbytes = msg->i.nbytes;
    if (nbytes >= (int)sizeof(cmd_buf)) nbytes = sizeof(cmd_buf) - 1;

    resmgr_msgget(ctp, cmd_buf, nbytes, sizeof(msg->i));
    cmd_buf[nbytes] = '\0';

    if (g_shm) {
        if (strstr(cmd_buf, "CHAOS:H")) {
            g_shm->chaos_request = CHAOS_HALLUCINATE;
            g_shm->telemetry.chaos_active = CHAOS_HALLUCINATE;
        } else if (strstr(cmd_buf, "CHAOS:S")) {
            g_shm->chaos_request = CHAOS_CPU_STORM;
            g_shm->telemetry.chaos_active = CHAOS_CPU_STORM;
        } else if (strstr(cmd_buf, "CHAOS:K")) {
            g_shm->chaos_request = CHAOS_SEGFAULT;
            g_shm->telemetry.chaos_active = CHAOS_SEGFAULT;
        } else if (strstr(cmd_buf, "RESET")) {
            g_shm->chaos_request = CHAOS_NONE;
            g_shm->telemetry.chaos_active = CHAOS_NONE;
        }
    }

    _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
    return _RESMGR_DEFAULT;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("[CORTEX_GATE] Initializing QNX 8.0 Native Simplex Resource Manager...\n");

    /* Query CPU cycles per second from Neutrino syspage */
    g_cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    if (g_cps == 0) g_cps = 1000000000ULL;

    /* Initialize POSIX Shared Memory Ring */
    g_shm_fd = shm_open(CORTEX_SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (g_shm_fd < 0) {
        perror("[CORTEX_GATE] shm_open failed");
    } else {
        if (ftruncate(g_shm_fd, sizeof(cortex_shm_t)) == 0) {
            g_shm = (cortex_shm_t *)mmap(NULL, sizeof(cortex_shm_t),
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, g_shm_fd, 0);
            if (g_shm == MAP_FAILED) {
                perror("[CORTEX_GATE] mmap failed");
                g_shm = NULL;
            } else {
                g_shm->magic = CORTEX_MAGIC;
                g_shm->version = CORTEX_PROTOCOL_VERSION;
                g_shm->telemetry.pid_resmgr = getpid();
                strncpy(g_shm->telemetry.active_policy, "SCHED_FIFO/SPORADIC", sizeof(g_shm->telemetry.active_policy)-1);
                printf("[CORTEX_GATE] Shared memory /cortex_shm_ring mapped at %p\n", (void*)g_shm);
            }
        }
    }

    /* Allocate Dispatch Handle */
    dpp = dispatch_create();
    if (!dpp) {
        fprintf(stderr, "[CORTEX_GATE] Failed to create dispatch handle\n");
        return EXIT_FAILURE;
    }

    /* Initialize Resource Manager attributes & function tables */
    resmgr_attr_t resmgr_attr;
    memset(&resmgr_attr, 0, sizeof(resmgr_attr));
    resmgr_attr.nparts_max = 2;
    resmgr_attr.msg_max_size = 4096;

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_IO_NFUNCS, &io_funcs);
    iofunc_attr_init(&attr, S_IFCHR | 0666, 0, 0);

    /* Hook custom callbacks */
    io_funcs.read   = cortex_io_read;
    io_funcs.write  = cortex_io_write;
    io_funcs.devctl = cortex_io_devctl;

    /* Attach to pathname in QNX namespace: /dev/cortex_gate */
    g_resmgr_id = resmgr_attach(dpp, &resmgr_attr, CORTEX_GATE_DEVNAME,
                                _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr);
    if (g_resmgr_id < 0) {
        fprintf(stderr, "[CORTEX_GATE] resmgr_attach failed: %s (running in fallback IPC mode)\n",
                strerror(errno));
    } else {
        printf("[CORTEX_GATE] Successfully registered device node: %s\n", CORTEX_GATE_DEVNAME);
    }

    /* Allocate dispatch context */
    d_ctp = dispatch_context_alloc(dpp);
    if (!d_ctp) {
        fprintf(stderr, "[CORTEX_GATE] dispatch_context_alloc failed\n");
        return EXIT_FAILURE;
    }

    printf("[CORTEX_GATE] Simplex Safety Cage ready. Entering event loop (PID %d)...\n", getpid());

    /* Resource Manager Event Loop */
    while (1) {
        d_ctp = dispatch_block(d_ctp);
        if (!d_ctp) {
            fprintf(stderr, "[CORTEX_GATE] dispatch_block error\n");
            break;
        }
        dispatch_handler(d_ctp);
    }

    return EXIT_SUCCESS;
}
