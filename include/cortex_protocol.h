#ifndef CORTEX_PROTOCOL_H
#define CORTEX_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <devctl.h>
#include <sys/neutrino.h>

#ifndef SETIOV
#define SETIOV(__iov, __addr, __len) \
    ((__iov)->iov_base = (void *)(__addr), (__iov)->iov_len = (size_t)(__len))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Identifiers and IPC endpoints */
#define CORTEX_GATE_DEVNAME         "/dev/cortex_gate"
#define CORTEX_SHM_NAME             "/cortex_shm_ring"
#define CORTEX_MAGIC                0x434F5254U  /* 'CORT' */
#define CORTEX_PROTOCOL_VERSION     1

/* Control Barrier Function (CBF) & Physical Cage Limits */
#define CORTEX_SAFE_RADIUS          2.50       /* meters */
#define CORTEX_MAX_VELOCITY         1.80       /* meters / second */
#define CORTEX_MAX_TORQUE           50.0       /* Newton-meters */
#define CORTEX_CBF_ALPHA            0.85       /* Extended Class-K gain */

/* QNX Native Pulse Codes (_PULSE_CODE_MINAVAIL = 0 to 127) */
#define CORTEX_PULSE_HEARTBEAT          (_PULSE_CODE_MINAVAIL + 1)
#define CORTEX_PULSE_CBF_CLAMP          (_PULSE_CODE_MINAVAIL + 2)
#define CORTEX_PULSE_AI_FAULT           (_PULSE_CODE_MINAVAIL + 3)
#define CORTEX_PULSE_WATCHDOG_RESPAWN   (_PULSE_CODE_MINAVAIL + 4)
#define CORTEX_PULSE_CHAOS_TRIGGER      (_PULSE_CODE_MINAVAIL + 5)
#define CORTEX_PULSE_TIMER_TICK         (_PULSE_CODE_MINAVAIL + 6)

/* Chaos Injection Modes */
typedef enum {
    CHAOS_NONE         = 0,
    CHAOS_HALLUCINATE  = 1,   /* Wild torque/velocity spikes violating safe radius */
    CHAOS_CPU_STORM    = 2,   /* Heavy compute matrix multiplication threads */
    CHAOS_SEGFAULT     = 3    /* Intentional SIGSEGV to prove sub-ms self-healing */
} cortex_chaos_mode_t;

/* 3D Vector Representation */
typedef struct {
    double x;
    double y;
    double z;
} cortex_vec3_t;

/* 6-DOF Physical Kinematics State */
typedef struct {
    cortex_vec3_t pos;        /* meters */
    cortex_vec3_t vel;        /* meters / sec */
    cortex_vec3_t acc;        /* meters / sec^2 */
    double        torque_nm;  /* Total applied torque magnitude */
    double        cbf_h;      /* Barrier value h(x) */
    uint32_t      is_clamped; /* 1 if projected onto safe tangent cone */
    uint32_t      padding;
} cortex_kinematics_t;

/* Actuation Command sent from AI Planner */
typedef struct {
    uint64_t      seq;
    uint64_t      timestamp_ns;
    cortex_vec3_t target_pos;
    cortex_vec3_t target_vel;
    double        requested_torque;
    uint32_t      chaos_flag;
    uint32_t      reserved;
    uint64_t      nn_inference_ns;  /* Exact forward-pass latency of 3-layer MLP */
} cortex_command_t;

/* Chaos Command Injection payload */
typedef struct {
    uint32_t chaos_mode;      /* cortex_chaos_mode_t */
    uint32_t duration_ms;
    double   intensity;
} cortex_chaos_cmd_t;

/* Complete Telemetry Payload */
typedef struct {
    uint64_t            seq;
    uint64_t            timestamp_ns;
    cortex_kinematics_t ai_raw;
    cortex_kinematics_t safe_clamped;
    double              cbf_h;
    uint64_t            cbf_interventions;
    uint64_t            total_cycles;
    uint64_t            last_eval_ns;
    uint64_t            nn_inference_ns;      /* Real 3-layer MLP execution time */
    uint64_t            p50_jitter_ns;
    uint64_t            p99_jitter_ns;
    uint64_t            p999_jitter_ns;
    uint64_t            max_jitter_ns;
    pid_t               pid_resmgr;
    pid_t               pid_ai;
    pid_t               pid_safety;
    pid_t               pid_watchdog;
    uint32_t            respawn_count;
    uint64_t            last_respawn_latency_ns;
    uint32_t            ai_alive;
    uint32_t            safety_alive;
    uint32_t            chaos_active;
    uint32_t            has_adversarial_prompt;
    char                adversarial_prompt[128];
    char                active_policy[32];
} cortex_telemetry_t;

/* Shared Memory Layout (/cortex_shm_ring) */
typedef struct {
    uint32_t            magic;
    uint32_t            version;
    volatile uint64_t   seq_lock;             /* Sequence lock for atomic read */
    cortex_telemetry_t  telemetry;
    volatile uint32_t   chaos_request;        /* Live chaos trigger flag */
    volatile uint32_t   chaos_duration_ms;
    volatile double     chaos_intensity;
    volatile uint32_t   has_custom_target;    /* Prompt bar override active */
    volatile cortex_vec3_t custom_target_pos;
    volatile cortex_vec3_t custom_target_vel;
    volatile double     custom_target_torque;
    char                adversarial_prompt[128];
} cortex_shm_t;

/* Custom QNX devctl Commands */
#define DCMD_CORTEX_SUBMIT_ACTUATION     __DIOTF(_DCMD_MISC, 1, cortex_command_t)
#define DCMD_CORTEX_GET_TELEMETRY        __DIOF(_DCMD_MISC, 2, cortex_telemetry_t)
#define DCMD_CORTEX_INJECT_CHAOS         __DIOT(_DCMD_MISC, 3, cortex_chaos_cmd_t)
#define DCMD_CORTEX_INJECT_PROMPT_TARGET __DIOT(_DCMD_MISC, 4, cortex_command_t)

#ifdef __cplusplus
}
#endif

#endif /* CORTEX_PROTOCOL_H */
