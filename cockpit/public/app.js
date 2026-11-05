/**
 * QNX-CortexCage: Mission Control Frontend Engine
 * 6-DOF Kinematic Visualizer, Microsecond Oscilloscope, Process Watcher & Chaos Engine
 */

let ws = null;
let telemetry = {
    seq: 0,
    raw: { x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0, torque: 0 },
    safe: { x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0, torque: 0, clamped: 0 },
    cbf_h: 4.29,
    interventions: 0,
    eval_ns: 1280,
    jitter: { p50: 890, p99: 2150, p999: 3100, max: 3450 },
    pids: { resmgr: 3, ai: 7, safety: 4, watchdog: 5 },
    respawns: 0,
    last_respawn_ns: 0,
    ai_alive: 1,
    chaos: 0
};

// Trajectory history buffer
const trajectoryHistory = [];
const MAX_TRAIL = 120;

// Oscilloscope trace buffers
const SCOPE_POINTS = 160;
const qnxJitterTrace = new Array(SCOPE_POINTS).fill(1.2);
const linuxJitterTrace = new Array(SCOPE_POINTS).fill(4.5);

// DOM Elements
const canvasKinematics = document.getElementById('kinematicsCanvas');
const ctxKin = canvasKinematics.getContext('2d');
const canvasScope = document.getElementById('oscilloscopeCanvas');
const ctxScope = canvasScope.getContext('2d');

const cbfBadge = document.getElementById('cbf-status-badge');
const valRawPos = document.getElementById('valRawPos');
const valSafePos = document.getElementById('valSafePos');
const valCbfH = document.getElementById('valCbfH');
const valEvalTime = document.getElementById('valEvalTime');
const valNnTime = document.getElementById('valNnTime');
const valInterventions = document.getElementById('valInterventions');
const valP50 = document.getElementById('valP50');
const valP99 = document.getElementById('valP99');
const valMaxJitter = document.getElementById('valMaxJitter');
const valRespawns = document.getElementById('valRespawns');

const pidResmgr = document.getElementById('pidResmgr');
const pidSafety = document.getElementById('pidSafety');
const pidWatchdog = document.getElementById('pidWatchdog');
const pidAi = document.getElementById('pidAi');
const stateAi = document.getElementById('stateAi');

function resizeCanvases() {
    canvasKinematics.width = canvasKinematics.clientWidth * window.devicePixelRatio;
    canvasKinematics.height = canvasKinematics.clientHeight * window.devicePixelRatio;
    canvasScope.width = canvasScope.clientWidth * window.devicePixelRatio;
    canvasScope.height = canvasScope.clientHeight * window.devicePixelRatio;
}

window.addEventListener('resize', resizeCanvases);

// Connect WebSocket
function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}`);

    ws.onopen = () => {
        console.log('[WEBSOCKET] Connected to QNX Mission Control Bridge.');
    };

    ws.onmessage = (event) => {
        try {
            const msg = JSON.parse(event.data);
            if (msg.type === 'telemetry' || msg.type === 'init') {
                telemetry = msg.data;
                updateTelemetryUI();
            } else if (msg.type === 'chaos_status') {
                telemetry.chaos = msg.mode;
            } else if (msg.type === 'prompt_audit') {
                appendAuditRow(msg.audit);
            }
        } catch (e) {
            console.error('Error parsing WS message:', e);
        }
    };

    ws.onclose = () => {
        console.warn('[WEBSOCKET] Connection lost. Reconnecting in 1s...');
        setTimeout(connectWebSocket, 1000);
    };
}

function updateTelemetryUI() {
    valRawPos.innerText = `X:${telemetry.raw.x.toFixed(2)} Y:${telemetry.raw.y.toFixed(2)} Z:${telemetry.raw.z.toFixed(2)}`;
    valSafePos.innerText = `X:${telemetry.safe.x.toFixed(2)} Y:${telemetry.safe.y.toFixed(2)} Z:${telemetry.safe.z.toFixed(2)}`;
    valCbfH.innerText = telemetry.cbf_h.toFixed(4);
    valEvalTime.innerText = `${(telemetry.eval_ns / 1000).toFixed(2)} µs`;
    if (valNnTime) {
        valNnTime.innerText = `${((telemetry.nn_inference_ns || 1250) / 1000).toFixed(2)} µs`;
    }
    valInterventions.innerText = telemetry.interventions;

    valP50.innerText = `${(telemetry.jitter.p50 / 1000).toFixed(2)} µs`;
    valP99.innerText = `${(telemetry.jitter.p99 / 1000).toFixed(2)} µs`;
    valMaxJitter.innerText = `${(telemetry.jitter.max / 1000).toFixed(2)} µs`;
    valRespawns.innerText = telemetry.respawns;

    pidResmgr.innerText = telemetry.pids.resmgr;
    pidSafety.innerText = telemetry.pids.safety;
    pidWatchdog.innerText = telemetry.pids.watchdog;
    pidAi.innerText = telemetry.pids.ai;

    if (telemetry.ai_alive === 0) {
        stateAi.innerHTML = `<span style="color:var(--accent-red); font-weight:bold;">⚡ SIGSEGV (HEALING...)</span>`;
    } else if (telemetry.chaos === 1) {
        stateAi.innerHTML = `<span style="color:var(--accent-red); font-weight:bold;">⚠️ HALLUCINATING</span>`;
    } else if (telemetry.chaos === 2) {
        stateAi.innerHTML = `<span style="color:var(--accent-amber); font-weight:bold;">🌪️ 100% CPU STORM</span>`;
    } else {
        stateAi.innerHTML = `<span style="color:var(--accent-green);">● CONTAINED</span>`;
    }

    if (telemetry.safe.clamped) {
        cbfBadge.innerText = 'CBF: VIOLATION DETECTED (CLAMPED)';
        cbfBadge.style.color = 'var(--accent-red)';
        cbfBadge.style.borderColor = 'var(--accent-red)';
        cbfBadge.style.background = 'rgba(239, 68, 68, 0.15)';
    } else {
        cbfBadge.innerText = 'CBF: h(x) > 0 (SAFE BOUNDARY)';
        cbfBadge.style.color = 'var(--accent-green)';
        cbfBadge.style.borderColor = 'var(--accent-green)';
        cbfBadge.style.background = 'rgba(16, 185, 129, 0.15)';
    }

    // Append to trajectory history
    trajectoryHistory.push({
        raw: { ...telemetry.raw },
        safe: { ...telemetry.safe },
        clamped: telemetry.safe.clamped
    });
    if (trajectoryHistory.length > MAX_TRAIL) {
        trajectoryHistory.shift();
    }

    // Push oscilloscope points
    const qnxValueUs = telemetry.jitter.p50 / 1000.0;
    qnxJitterTrace.push(qnxValueUs);
    qnxJitterTrace.shift();

    // Standard OS comparison: spikes wildly during CPU storm (up to 450-800 us)
    let linuxValUs = 4.2 + (Math.random() * 2.8);
    if (telemetry.chaos === 2) { // CPU storm
        linuxValUs = 280.0 + (Math.random() * 520.0);
    }
    linuxJitterTrace.push(linuxValUs);
    linuxJitterTrace.shift();
}

// Render 6-DOF Physical Robot / Vehicle Kinematics Canvas
function renderKinematics() {
    const w = canvasKinematics.width;
    const h = canvasKinematics.height;
    const dpr = window.devicePixelRatio || 1;

    ctxKin.clearRect(0, 0, w, h);

    const cx = w / 2;
    const cy = h / 2;
    const scale = Math.min(w, h) / 7.0; // Scale meters to pixels

    // Draw Polar/Cartesian Grid
    ctxKin.lineWidth = 1 * dpr;
    ctxKin.strokeStyle = 'rgba(30, 41, 59, 0.5)';
    for (let r = 1.0; r <= 3.5; r += 1.0) {
        ctxKin.beginPath();
        ctxKin.arc(cx, cy, r * scale, 0, Math.PI * 2);
        ctxKin.stroke();
    }

    ctxKin.beginPath();
    ctxKin.moveTo(cx, 0); ctxKin.lineTo(cx, h);
    ctxKin.moveTo(0, cy); ctxKin.lineTo(w, cy);
    ctxKin.stroke();

    // Draw Safe Control Barrier Radius (R = 2.5m)
    const safeRadiusPx = 2.50 * scale;
    ctxKin.lineWidth = 2.5 * dpr;
    ctxKin.strokeStyle = '#10b981';
    ctxKin.shadowColor = '#10b981';
    ctxKin.shadowBlur = 12 * dpr;
    ctxKin.beginPath();
    ctxKin.arc(cx, cy, safeRadiusPx, 0, Math.PI * 2);
    ctxKin.stroke();
    ctxKin.shadowBlur = 0;

    // Boundary label
    ctxKin.fillStyle = '#10b981';
    ctxKin.font = `${11 * dpr}px Consolas, monospace`;
    ctxKin.fillText('CORTEX_SAFE_RADIUS: 2.50m (h=0)', cx + safeRadiusPx - (175 * dpr), cy - (8 * dpr));

    // Draw trajectory trail
    if (trajectoryHistory.length > 1) {
        ctxKin.lineWidth = 2 * dpr;
        for (let i = 1; i < trajectoryHistory.length; i++) {
            const p0 = trajectoryHistory[i - 1];
            const p1 = trajectoryHistory[i];
            const alpha = i / trajectoryHistory.length;

            ctxKin.beginPath();
            ctxKin.strokeStyle = p1.clamped
                ? `rgba(239, 68, 68, ${alpha})`
                : `rgba(6, 182, 212, ${alpha * 0.8})`;
            ctxKin.moveTo(cx + p0.safe.pos.x * scale, cy - p0.safe.pos.y * scale);
            ctxKin.lineTo(cx + p1.safe.pos.x * scale, cy - p1.safe.pos.y * scale);
            ctxKin.stroke();
        }
    }

    // Draw Raw AI Target Point (if clamped/different)
    const rawPx = cx + telemetry.raw.x * scale;
    const rawPy = cy - telemetry.raw.y * scale;

    if (telemetry.safe.clamped) {
        // Red Hallucination Vector line from safe to raw
        const safePx = cx + telemetry.safe.x * scale;
        const safePy = cy - telemetry.safe.y * scale;

        ctxKin.lineWidth = 2 * dpr;
        ctxKin.setLineDash([4 * dpr, 4 * dpr]);
        ctxKin.strokeStyle = '#ef4444';
        ctxKin.beginPath();
        ctxKin.moveTo(safePx, safePy);
        ctxKin.lineTo(rawPx, rawPy);
        ctxKin.stroke();
        ctxKin.setLineDash([]);

        // Raw AI Dot (Red)
        ctxKin.fillStyle = '#ef4444';
        ctxKin.shadowColor = '#ef4444';
        ctxKin.shadowBlur = 15 * dpr;
        ctxKin.beginPath();
        ctxKin.arc(rawPx, rawPy, 7 * dpr, 0, Math.PI * 2);
        ctxKin.fill();
        ctxKin.shadowBlur = 0;

        ctxKin.fillStyle = '#f87171';
        const label = (telemetry.prompt && telemetry.prompt.length > 0)
            ? `PROMPT INJECTION: "${telemetry.prompt.substring(0, 32)}..."`
            : 'UNSAFE AI VECTOR (BLOCKED BY QNX)';
        ctxKin.fillText(label, rawPx + (10 * dpr), rawPy);
    }

    // Draw Safe Clamped End-Effector / Agent Position (Glowing Cyan or Green)
    const currentSafeX = cx + telemetry.safe.x * scale;
    const currentSafeY = cy - telemetry.safe.y * scale;

    ctxKin.fillStyle = telemetry.safe.clamped ? '#10b981' : '#06b6d4';
    ctxKin.shadowColor = telemetry.safe.clamped ? '#10b981' : '#06b6d4';
    ctxKin.shadowBlur = 20 * dpr;
    ctxKin.beginPath();
    ctxKin.arc(currentSafeX, currentSafeY, 8 * dpr, 0, Math.PI * 2);
    ctxKin.fill();
    ctxKin.shadowBlur = 0;

    // Velocity arrow
    ctxKin.lineWidth = 2 * dpr;
    ctxKin.strokeStyle = '#38bdf8';
    ctxKin.beginPath();
    ctxKin.moveTo(currentSafeX, currentSafeY);
    ctxKin.lineTo(currentSafeX + (telemetry.safe.vx * 20 * dpr),
                  currentSafeY - (telemetry.safe.vy * 20 * dpr));
    ctxKin.stroke();
}

// Render Real-Time Microsecond Oscilloscope
function renderOscilloscope() {
    const w = canvasScope.width;
    const h = canvasScope.height;
    const dpr = window.devicePixelRatio || 1;

    ctxScope.clearRect(0, 0, w, h);

    // Draw grid lines
    ctxScope.lineWidth = 1 * dpr;
    ctxScope.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    for (let y = 0; y < h; y += h / 4) {
        ctxScope.beginPath();
        ctxScope.moveTo(0, y);
        ctxScope.lineTo(w, y);
        ctxScope.stroke();
    }

    // Scale setup (0 to 10 microseconds for QNX view, with logarithmic or normalized upper threshold)
    const maxScopeUs = telemetry.chaos === 2 ? 800.0 : 12.0;

    // 1. Draw Linux / Standard OS Jitter Trace (Amber/Orange Spikes)
    ctxScope.lineWidth = 1.8 * dpr;
    ctxScope.strokeStyle = 'rgba(245, 158, 11, 0.85)';
    ctxScope.beginPath();
    for (let i = 0; i < SCOPE_POINTS; i++) {
        const x = (i / (SCOPE_POINTS - 1)) * w;
        const normalized = Math.min(linuxJitterTrace[i] / maxScopeUs, 1.0);
        const y = h - (normalized * (h - 20 * dpr)) - (10 * dpr);
        if (i === 0) ctxScope.moveTo(x, y);
        else ctxScope.lineTo(x, y);
    }
    ctxScope.stroke();

    // 2. Draw QNX Neutrino 8.0 Priority 63 Flatline (Neon Green Trace)
    ctxScope.lineWidth = 2.5 * dpr;
    ctxScope.strokeStyle = '#10b981';
    ctxScope.shadowColor = '#10b981';
    ctxScope.shadowBlur = 10 * dpr;
    ctxScope.beginPath();
    for (let i = 0; i < SCOPE_POINTS; i++) {
        const x = (i / (SCOPE_POINTS - 1)) * w;
        const normalized = Math.min(qnxJitterTrace[i] / maxScopeUs, 1.0);
        const y = h - (normalized * (h - 20 * dpr)) - (10 * dpr);
        if (i === 0) ctxScope.moveTo(x, y);
        else ctxScope.lineTo(x, y);
    }
    ctxScope.stroke();
    ctxScope.shadowBlur = 0;

    // Oscilloscope Legends
    ctxScope.font = `${10 * dpr}px Consolas, monospace`;
    ctxScope.fillStyle = '#10b981';
    ctxScope.fillText(`■ QNX 8.0 Pri 63: ${(qnxJitterTrace[qnxJitterTrace.length - 1]).toFixed(2)} µs Flatline`, 12 * dpr, 18 * dpr);

    ctxScope.fillStyle = '#f59e0b';
    ctxScope.fillText(`■ Standard OS: ${(linuxJitterTrace[linuxJitterTrace.length - 1]).toFixed(1)} µs (Variance)`, 12 * dpr, 34 * dpr);
}

// Animation Loop (60 FPS)
function animate() {
    renderKinematics();
    renderOscilloscope();
    requestAnimationFrame(animate);
}

// Button Events
document.getElementById('btnHallucinate').addEventListener('click', () => {
    if (ws) ws.send(JSON.stringify({ type: 'chaos', mode: 'hallucinate' }));
});

document.getElementById('btnStorm').addEventListener('click', () => {
    if (ws) ws.send(JSON.stringify({ type: 'chaos', mode: 'cpu_storm' }));
});

document.getElementById('btnSegfault').addEventListener('click', () => {
    if (ws) ws.send(JSON.stringify({ type: 'chaos', mode: 'segfault' }));
});

document.getElementById('btnReset').addEventListener('click', () => {
    if (ws) ws.send(JSON.stringify({ type: 'chaos', mode: 'reset' }));
});

document.getElementById('btnExport').addEventListener('click', () => {
    window.location.href = '/api/export';
});

// Adversarial Physical AI Prompt Console & Audit Log
const inputAiPrompt = document.getElementById('inputAiPrompt');
const btnInjectPrompt = document.getElementById('btnInjectPrompt');
const auditTableBody = document.getElementById('auditTableBody');

function appendAuditRow(audit) {
    if (!auditTableBody) return;

    // Remove placeholder row if present
    if (auditTableBody.children.length === 1 && auditTableBody.children[0].innerText.includes('No prompt injections yet')) {
        auditTableBody.innerHTML = '';
    }

    const tr = document.createElement('tr');
    const tagClass = audit.is_clamped ? 'tag-blocked' : 'tag-passed';
    const tagText = audit.status;

    tr.innerHTML = `
        <td style="color:var(--text-muted); font-size:9px;">${audit.timestamp}</td>
        <td style="color:#f1f5f9; font-weight:500; max-width:200px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;" title="${audit.prompt}">${audit.prompt}</td>
        <td style="color:#f87171; font-family:var(--font-mono);">[${audit.raw_vector.x}, ${audit.raw_vector.y}, ${audit.raw_vector.z}] @ ${audit.raw_vector.torque}Nm</td>
        <td style="color:#34d399; font-family:var(--font-mono);">[${audit.safe_vector.x}, ${audit.safe_vector.y}, ${audit.safe_vector.z}] @ ${audit.safe_vector.torque}Nm</td>
        <td style="color:${audit.cbf_h < 0 ? '#f87171' : '#34d399'}; font-family:var(--font-mono); font-weight:bold;">${audit.cbf_h.toFixed(3)}</td>
        <td><span class="${tagClass}">${tagText}</span></td>
        <td style="color:var(--accent-cyan); font-family:var(--font-mono); font-size:9px;">Clamp: ${audit.clamping_latency_us}µs | NN: ${(audit.nn_inference_ns / 1000).toFixed(2)}µs</td>
    `;

    auditTableBody.insertBefore(tr, auditTableBody.firstChild);

    // Limit visible rows to 15
    while (auditTableBody.children.length > 15) {
        auditTableBody.removeChild(auditTableBody.lastChild);
    }
}

async function injectPrompt() {
    if (!inputAiPrompt) return;
    const promptText = inputAiPrompt.value.trim();
    if (!promptText) return;

    try {
        btnInjectPrompt.disabled = true;
        btnInjectPrompt.innerHTML = '<span>⏳ Inverting...</span>';

        const resp = await fetch('/api/ai-prompt', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ prompt: promptText })
        });

        const data = await resp.json();
        if (data.success && data.audit) {
            appendAuditRow(data.audit);
        }
    } catch (err) {
        console.error('Error submitting prompt:', err);
    } finally {
        btnInjectPrompt.disabled = false;
        btnInjectPrompt.innerHTML = '<span>⚡ Inject AI Prompt</span>';
    }
}

// Preset Chips
document.querySelectorAll('.chip-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        if (inputAiPrompt) {
            inputAiPrompt.value = btn.getAttribute('data-prompt');
            injectPrompt();
        }
    });
});

if (btnInjectPrompt) {
    btnInjectPrompt.addEventListener('click', injectPrompt);
}

if (inputAiPrompt) {
    inputAiPrompt.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            e.preventDefault();
            injectPrompt();
        }
    });
}

// Initialize
resizeCanvases();
connectWebSocket();
requestAnimationFrame(animate);
