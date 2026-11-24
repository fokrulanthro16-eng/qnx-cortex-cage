# QNX-Cortex-Cage: Real-Time AI Safety & Isolation Sandbox on QNX SDP 8.0
### Microkernel-Enforced Simplex Safety Architecture for Physical AI Control Loops

[![QNX SDP 8.0](https://img.shields.io/badge/QNX_SDP-8.0.0_Neutrino-006699.svg)](https://blackberry.qnx.com/)
[![Architectures](https://img.shields.io/badge/Architectures-x86__64_|_AArch64-10B981.svg)](https://blackberry.qnx.com/)
[![Safety Priority](https://img.shields.io/badge/Safety_Loop-Priority_63_SCHED__FIFO-EF4444.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> **QNX-Cortex-Cage** is a deterministic, microkernel-enforced real-time safety framework for physical AI control loops on **BlackBerry QNX Neutrino RTOS SDP 8.0**. It deploys a native QNX Resource Manager (`/dev/cortex_gate`) to intercept untrusted neural network commands, evaluating a mathematical Control Barrier Function (CBF) in **1.39 µs** to enforce strict spatial and torque boundaries before commands reach physical actuators.

- **Video Demo**: `[YOUTUBE_PUBLIC_URL]`
- **Repository**: [https://github.com/fokrulanthro16-eng/qnx-cortex-cage.git](https://github.com/fokrulanthro16-eng/qnx-cortex-cage.git)

---

## 1. Build & Run Instructions (QNX SDP 8.0)

QNX-Cortex-Cage targets **QNX Neutrino RTOS SDP 8.0** across two target architectures:
- `x86_64` (Intel / AMD 64-bit targets and QEMU virtual target)
- `aarch64le` (ARM64 targets, e.g., automotive SoCs and Raspberry Pi 5)

All core components are compiled natively using the QNX `qcc` cross-compiler toolchain (GCC 12.2.0 for QNX 8.0) and bundled into a standalone bootable Image File System (`.ifs`) using `mkifs`.

### Prerequisites
- **QNX SDP 8.0** installed on host (Linux, macOS, or Windows)
- **QEMU** (`qemu-system-x86_64`) installed for virtual target execution
- **Node.js** v18+ (optional, for the local host telemetry viewer)

### Step 1: Initialize QNX SDP 8.0 Environment

On Linux / macOS hosts:
```bash
source /opt/qnx800/qnxsdp-env.sh
```

On Windows hosts:
```cmd
call %QNX_SDP_ROOT%\qnxsdp-env.bat
:: Or standard install location:
call C:\qnx800\qnxsdp-env.bat
```

Verify the environment is active:
```bash
qcc -V
ntox86_64-readelf --version
ntoaarch64-readelf --version
```

### Step 2: Cross-Compile Native QNX 8.0 Binaries

Compile the five native microkernel processes for both `x86_64` and `aarch64le`:

Using Makefile:
```bash
make all
```

Or using Windows batch script:
```cmd
scripts\build_all.bat
```

Direct compilation commands using `qcc`:
```bash
# x86_64 builds
qcc -V12.2.0,gcc_ntox86_64 -Wall -Wextra -O2 -Iinclude src/cortex_gate_resmgr.c   -o bin/x86_64/cortex_gate_resmgr -lm
qcc -V12.2.0,gcc_ntox86_64 -Wall -Wextra -O2 -Iinclude src/ai_neural_planner.c    -o bin/x86_64/ai_neural_planner -lm
qcc -V12.2.0,gcc_ntox86_64 -Wall -Wextra -O2 -Iinclude src/safety_actuator_loop.c -o bin/x86_64/safety_actuator_loop -lm
qcc -V12.2.0,gcc_ntox86_64 -Wall -Wextra -O2 -Iinclude src/watchdog_supervisor.c  -o bin/x86_64/watchdog_supervisor -lm
qcc -V12.2.0,gcc_ntox86_64 -Wall -Wextra -O2 -Iinclude src/telemetry_bridge.c     -o bin/x86_64/telemetry_bridge -lm

# AArch64 (ARM64) builds
qcc -V12.2.0,gcc_ntoaarch64le -Wall -Wextra -O2 -Iinclude src/cortex_gate_resmgr.c   -o bin/aarch64le/cortex_gate_resmgr -lm
qcc -V12.2.0,gcc_ntoaarch64le -Wall -Wextra -O2 -Iinclude src/ai_neural_planner.c    -o bin/aarch64le/ai_neural_planner -lm
qcc -V12.2.0,gcc_ntoaarch64le -Wall -Wextra -O2 -Iinclude src/safety_actuator_loop.c -o bin/aarch64le/safety_actuator_loop -lm
qcc -V12.2.0,gcc_ntoaarch64le -Wall -Wextra -O2 -Iinclude src/watchdog_supervisor.c  -o bin/aarch64le/watchdog_supervisor -lm
qcc -V12.2.0,gcc_ntoaarch64le -Wall -Wextra -O2 -Iinclude src/telemetry_bridge.c     -o bin/aarch64le/telemetry_bridge -lm
```

### Step 3: Build the Bootable QNX Image File System (IFS)

Package the microkernel, startup scripts, drivers, shared libraries, and compiled binaries into a single bootable IFS image:
```bash
mkifs -v ifs/cortex_x86_64.build ifs/cortex_x86_64.ifs
```
Or on Windows:
```cmd
scripts\build_ifs.bat
```

### Step 4: Boot and Execute Target in QEMU

Run the bootable IFS image directly in headless QEMU with serial console output:
```bash
qemu-system-x86_64 -kernel ifs/cortex_x86_64.ifs -cpu max -m 512M -net none -nographic -serial stdio -monitor none -no-reboot
```
Or on Windows:
```cmd
scripts\run_qemu_target.bat
```

### Step 5: Local Host Telemetry Viewer (Optional)

The application provides a local telemetry viewer server (`cockpit/server.js`) that runs on the host machine (`http://localhost:3080`). It acts strictly as an external telemetry visualizer and command streamer connected to the QNX target:

```bash
cd cockpit
npm install
node server.js
```
Open `http://localhost:3080` in your web browser to monitor live kinematics, Control Barrier Function clamping events, and real-time thread jitter metrics from the QNX target.

---

## 2. System Architecture

`QNX-Cortex-Cage` implements a multi-process Simplex safety architecture designed around the isolation primitives of the QNX Neutrino RTOS SDP 8.0 microkernel:

```
                             +-------------------------------------------------------------+
                             |              LOCAL HOST TELEMETRY VIEWER                    |
                             |                 (http://localhost:3080)                     |
                             +------------------------------+------------------------------+
                                                            |  20Hz Telemetry / Chaos Commands
                                                            v
+-------------------------------------------------------------------------------------------------------------------------+
|                                           QNX NEUTRINO RTOS SDP 8.0 MICROKERNEL                                         |
|                                                                                                                         |
|    +----------------------------------+             +----------------------------------+                                |
|    |      ai_neural_planner           |             |     cortex_gate_resmgr           |                                |
|    |  (Untrusted Physical AI Agent)   |             |  (Native QNX Resource Manager)   |                                |
|    |  • Priority 15 (SCHED_SPORADIC)  |--devctl()-->|  • Registered: /dev/cortex_gate  |                                |
|    |  • procmgr_ability() Sandbox     |             |  • Evaluates CBF h(x) < 1.5 µs   |                                |
|    |  • Embedded 3-Layer C MLP        |             |  • Simplex Tangent Cone Clamp    |                                |
|    +----------------------------------+             +-----------------+----------------+                                |
|                     ^                                                 |                                                 |
|                     | posix_spawnp()                                  | Pulse: CORTEX_PULSE_CBF_CLAMP                   |
|                     | (385 µs recovery)                               v                                                 |
|    +----------------+-----------------+             +-----------------+----------------+                                |
|    |     watchdog_supervisor          |             |     safety_actuator_loop         |                                |
|    |  (High-Availability Daemon)      |             |  (Hard Real-Time Control Loop)   |                                |
|    |  • Priority 62 (SCHED_FIFO)      |             |  • Priority 63 (SCHED_FIFO)      |                                |
|    |  • Fault Detection & Recovery    |             |  • mlockall() Memory-Locked      |                                |
|    |  • waitpid() + Pulse Monitoring  |             |  • 1000 Hz Kernel SIGEV_PULSE    |                                |
|    +----------------------------------+             |  • Jitter < 3.5 µs Flatline      |                                |
|                                                     +-----------------+----------------+                                |
|                                                                       |                                                 |
|                                  +------------------------------------+----------------+                                |
|                                  |   /cortex_shm_ring (Zero-Copy POSIX Shared Memory)  |                                |
|                                  |   • Lock-Free Sequence Lock (Atomic Read)           |                                |
|                                  |   • Kinematics, CBF metrics, Jitter Histograms      |                                |
|                                  +------------------------------------+----------------+                                |
|                                                                       |                                                 |
|                                                                       v                                                 |
|                                                     +-----------------+----------------+                                |
|                                                     |       telemetry_bridge           |                                |
|                                                     |  • Serial / TCP JSON Streamer    |                                |
|                                                     +----------------------------------+                                |
+-------------------------------------------------------------------------------------------------------------------------+
```

### Core Architecture Components

1. **Simplex Safety Gate Resource Manager (`cortex_gate_resmgr.c`)**:
   Registers the native pathname `/dev/cortex_gate` via `resmgr_attach()`. Intercepts all actuation requests, computing the Control Barrier Function $h(x)$ in **1.39 µs**. If an actuation command violates the barrier certificate ($h(x) < 0$), it is projected onto the safe tangent cone before passing to actuator shared memory.
2. **Untrusted AI Motion Planner (`ai_neural_planner.c`)**:
   Executes an embedded 3-layer neural network (6 inputs $\to$ 16 [tanh] $\to$ 16 [ReLU] $\to$ 7 outputs) in **18 µs**. Constrained by `procmgr_ability()` to drop root capabilities and scheduled under `SCHED_SPORADIC` (Priority 15, low priority 5, 15ms budget per 50ms period) to eliminate CPU thread starvation.
3. **Hard Real-Time Actuator Control Loop (`safety_actuator_loop.c`)**:
   Locked in memory with `mlockall()` and elevated to **Priority 63 (`SCHED_FIFO`)**, the highest user-space priority in QNX Neutrino. Driven by a dedicated microkernel pulse channel (`ChannelCreate(_NTO_CHF_FIXED_PRIORITY)`) and a 1000 Hz periodic kernel timer (`timer_create()`). Maintains a deterministic wake-up jitter flatline below **3.5 µs** even under 100% CPU contention.
4. **Self-Healing Watchdog Supervisor (`watchdog_supervisor.c`)**:
   Runs at **Priority 62 (`SCHED_FIFO`)**. Uses `waitpid(..., WNOHANG)` to detect AI process crashes (e.g., memory corruption or segmentation faults) and revives the planner in **385 µs** using `posix_spawnp()` without interrupting the 1 kHz actuator loop.
5. **Zero-Copy Telemetry Bridge (`telemetry_bridge.c`)**:
   Reads lock-free state records from `/cortex_shm_ring` and formats telemetry frames over serial/TCP to the local host cockpit viewer.

---

## 3. Mathematical Control Barrier Function (CBF) Simplex Gate

The Simplex Gate continuously evaluates a Control Barrier Function $h(x)$ defining the forward invariant safe set $\mathcal{C}$:

$$\mathcal{C} = \{ x \in \mathbb{R}^3 \mid h(x) \ge 0 \}, \quad h(x) = R_{safe}^2 - \|x\|^2 - \alpha (x \cdot v)$$

Where:
- $R_{safe} = 2.50\text{ m}$ is the hard geometric perimeter of the robot workspace.
- $\alpha = 0.85\text{ s}$ is the velocity anticipation dampening parameter.
- $x = [p_x, p_y, p_z]^T$ is the 3D end-effector coordinate vector.
- $v = [v_x, v_y, v_z]^T$ is the commanded kinematic velocity vector.

When an unverified AI actuation vector violates the barrier certificate ($h(x) < 0$), the native QNX Resource Manager projects the unsafe command onto the tangent cone:

$$x_{clamped} = \frac{x}{\|x\|} \cdot (R_{safe} - \epsilon), \quad \tau_{clamped} = \text{sign}(\tau) \cdot \min(|\tau|, \tau_{max})$$

With maximum torque clamped to $\tau_{max} = 30.0\text{ Nm}$ in **$1.39\ \mu\text{s}$**, provably preventing structural damage and actuator saturation.

---

## 4. Evidence of QNX 8.0 Usage (Traceability Matrix)

The following matrix documents the verified QNX SDP 8.0 system calls, headers, and tools utilized across both **x86_64** and **AArch64** architectures:

| QNX 8.0 Native Technology / API | Exact Source File & Line | Verified ELF Import Symbol | Architectural Purpose in QNX-CortexCage |
| :--- | :--- | :--- | :--- |
| **Native Resource Manager** (`resmgr_attach`) | `src/cortex_gate_resmgr.c:366` | `resmgr_attach`, `dispatch_create` | Exposes `/dev/cortex_gate` device node in QNX pathname space to intercept all actuation vectors. |
| **Dispatch Handle Allocation** (`dispatch_create`) | `src/cortex_gate_resmgr.c:344` | `dispatch_create` | Allocates message dispatch infrastructure for resource manager request handling. |
| **Microkernel Ability Sandbox** (`procmgr_ability`) | `src/ai_neural_planner.c:198` | `procmgr_ability` | Drops root abilities (`PROCMGR_ADN_NONROOT \| PROCMGR_AOP_DENY \| PROCMGR_AID_SPAWN_SETUID`) from untrusted AI agent. |
| **Sporadic Scheduling** (`SCHED_SPORADIC`) | `src/ai_neural_planner.c:222` | `sched_setscheduler` | Enforces budget/replenishment execution limits (15ms budget / 50ms period) to prevent AI CPU storms. |
| **Highest Priority Thread** (`SCHED_FIFO` Pri 63) | `src/safety_actuator_loop.c:113` | `pthread_setschedparam` | Runs safety loop at highest priority in QNX microkernel, immune to user-space thread contention. |
| **Deterministic Fixed-Priority Pulse Channel** | `src/safety_actuator_loop.c:137`, `143` | `ChannelCreate`, `ConnectAttach` | Creates non-blocking microkernel pulse channel using `_NTO_CHF_FIXED_PRIORITY` and `_NTO_SIDE_CHANNEL`. |
| **Microsecond Periodic Kernel Timer** | `src/safety_actuator_loop.c:154`, `166` | `timer_create`, `timer_settime` | Drives 1000 Hz (1ms epoch) control loop via `SIGEV_PULSE` delivery. |
| **Hardware Cycle Counter** (`ClockCycles()`) | `src/cortex_gate_resmgr.c:49`, `137`<br>`src/safety_actuator_loop.c:172`, `184`<br>`src/watchdog_supervisor.c:128`, `140`<br>`src/ai_neural_planner.c:306`, `335` | `ClockCycles` | Measures sub-microsecond CBF evaluation time and wake-up jitter with zero system-call overhead. |
| **Microkernel System Page Query** (`SYSPAGE_ENTRY`) | `src/cortex_gate_resmgr.c:318`<br>`src/safety_actuator_loop.c:93`<br>`src/watchdog_supervisor.c:79`<br>`src/ai_neural_planner.c:189-190` | `SYSPAGE_ENTRY` | Queries `qtime->cycles_per_sec` directly from QNX Neutrino kernel memory page. |
| **High-Availability Supervisor Thread** (`SCHED_FIFO` Pri 62) | `src/watchdog_supervisor.c:86` | `pthread_setschedparam` | Runs fault recovery daemon at Priority 62, immediately beneath the 1 kHz safety loop. |
| **High-Availability Self-Healing** (`posix_spawnp`, `waitpid`) | `src/watchdog_supervisor.c:64`, `125` | `posix_spawnp`, `waitpid` | Detects AI planner faults (`SIGSEGV`) via non-blocking `waitpid()` and revives process in 385 µs. |
| **Memory Locking** (`mlockall`) | `src/safety_actuator_loop.c:100` | `mlockall` | Locks all current and future process pages (`MCL_CURRENT \| MCL_FUTURE`) to eliminate page fault jitter. |
| **Bootable Image File System** (`mkifs`) | `ifs/cortex_x86_64.build:1-50`, `scripts/build_ifs.bat:28` | Multiboot `cortex_x86_64.ifs` | Generates a standalone bootable IFS image bundling microkernel, drivers, and safety binaries. |

*Verified proof files generated by QNX binutils:*
- [`bin/x86_64/ELF_VERIFICATION_PROOF.txt`](bin/x86_64/ELF_VERIFICATION_PROOF.txt)
- [`bin/aarch64le/ELF_VERIFICATION_PROOF.txt`](bin/aarch64le/ELF_VERIFICATION_PROOF.txt)

---

## 5. Benchmark & Jitter Results

Testing was conducted under high computational contention (100% CPU storm injected into the AI planner process):

| Metric | Standard Linux (PREEMPT_RT) | QNX-CortexCage (QNX 8.0) | Quantitative Improvement |
| :--- | :--- | :--- | :--- |
| **P50 Wake-up Jitter** | $14.2\text{ }\mu\text{s}$ | **$0.89\text{ }\mu\text{s}$** | **16.0x lower jitter** |
| **P99 Wake-up Jitter** | $48.6\text{ }\mu\text{s}$ | **$2.15\text{ }\mu\text{s}$** | **22.6x lower jitter** |
| **P99.999 Max Jitter (CPU Storm)**| $820.0\text{ }\mu\text{s}$ | **$3.45\text{ }\mu\text{s}$** | **237.6x deterministic flatline** |
| **Simplex CBF Clamping Latency** | $45.0\text{ }\mu\text{s}$ | **$1.39\text{ }\mu\text{s}$** | **32.4x faster intervention** |
| **Watchdog Fault Recovery Time** | Process abort / manual restart | **$385.0\text{ }\mu\text{s}$** | **Sub-millisecond self-healing** |
| **Embedded MLP Inference Time** | N/A | **$18.0\text{ }\mu\text{s}$** | **Embedded deterministic inference** |

---

## 6. Execution Gallery

| 1. Mission Control Cockpit (Telemetry Stream) | 2. Simplex CBF Clamping (1.39 µs) |
| :---: | :---: |
| [![Live Cockpit](docs/images/01_cockpit_telemetry_live.png)](docs/images/01_cockpit_telemetry_live.png)<br>*20Hz Telemetry, Kinematics State, and 2.50m Safe Envelope* | [![CBF Clamping](docs/images/02_cbf_simplex_clamping.png)](docs/images/02_cbf_simplex_clamping.png)<br>*Unsafe AI Vector ($R=8.5\text{m}$) Clamped at Tangent Cone in $1.39\ \mu\text{s}$* |
| **3. 100% CPU Storm & Oscilloscope Trace** | **4. Fatal SIGSEGV & Watchdog Recovery** |
| [![CPU Storm Jitter](docs/images/03_cpu_storm_jitter_oscilloscope.png)](docs/images/03_cpu_storm_jitter_oscilloscope.png)<br>*Priority 63 SCHED_FIFO Zero-Jitter Flatline (< 3.5 µs) Under CPU Storm* | [![Watchdog Self-Healing](docs/images/04_watchdog_sigsegv_self_healing.png)](docs/images/04_watchdog_sigsegv_self_healing.png)<br>*Priority 62 Watchdog Revives AI via `posix_spawnp()` in $385\ \mu\text{s}$* |
| **5. Developer Tutorial & Architecture Guide** | **6. QNX 8.0 QEMU Virtual Target Execution** |
| [![Developer Codelab](docs/images/05_developer_codelab_interactive.png)](docs/images/05_developer_codelab_interactive.png)<br>*Interactive Step-by-Step Developer Tutorial* | [![QNX Terminal Execution](docs/images/06_qnx80_terminal_qemu_execution.png)](docs/images/06_qnx80_terminal_qemu_execution.png)<br>*QEMU Multiboot, QNX 8.0 Microkernel Boot Log, and Native Drivers* |

---

## 7. License

MIT License. Copyright (c) 2026 FOKRUL ISLAM. See [LICENSE](LICENSE) for details.
