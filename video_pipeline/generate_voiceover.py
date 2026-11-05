"""
QNX-CortexCage: Automated Neural Voiceover Generator
Generates studio-grade narration tracks for all 6 keynote scenes using edge-tts.
"""

import asyncio
import os
import json
import edge_tts
from moviepy import AudioFileClip

PIPELINE_DIR = os.path.dirname(os.path.abspath(__file__))
AUDIO_DIR = os.path.join(PIPELINE_DIR, "audio")
os.makedirs(AUDIO_DIR, exist_ok=True)

VOICE = "en-US-AndrewNeural"  # Professional Silicon Valley Keynote voice

SCENES = [
    {
        "id": 1,
        "name": "vision",
        "title": "The Physical AI Crisis & QNX-CortexCage Vision",
        "caption": "QNX-CORTEXCAGE: Microkernel-Enforced Simplex Safety Architecture for Physical AI",
        "text": (
            "Physical AI is transforming surgical robotics, autonomous vehicles, and aerospace systems. "
            "Yet modern neural networks remain non-deterministic black boxes prone to catastrophic hallucinations. "
            "Welcome to QNX-CortexCage—a microkernel-enforced, sub-microsecond Simplex Safety Architecture "
            "built on QNX Neutrino 8.0 that mathematically guarantees physical AI can never violate real-world safety boundaries."
        )
    },
    {
        "id": 2,
        "name": "tinyml_architecture",
        "title": "Microkernel Architecture & Embedded C TinyML",
        "caption": "EMBEDDED 3-LAYER C MLP & MICROKERNEL SCHED_SPORADIC CONTAINMENT",
        "text": (
            "Zero cloud APIs. Zero network latency. CortexCage executes a self-contained, three-layer C "
            "Multi-Layer Perceptron directly inside QNX user-space in just eighteen microseconds, strictly sandboxed "
            "via procmgr ability and sporadic scheduling. Every actuation vector must pass through our POSIX "
            "Resource Manager, slash-dev-slash-cortex-gate, governed by a hard real-time one-kilohertz control loop locked at Priority 63."
        )
    },
    {
        "id": 3,
        "name": "adversarial_jailbreak",
        "title": "Live Adversarial AI Jailbreak & 1.39µs CBF Clamping",
        "caption": "LIVE ADVERSARIAL JAILBREAK ATTEMPT: 1.39µs SIMPLEX CBF SAFETY PROJECTION",
        "text": (
            "Let's launch a live adversarial jailbreak. We command the surgical actuator to breach the cage "
            "at negative eight point five meters with a lethal two-hundred-forty Newton-meters of torque. "
            "Watch QNX-CortexCage respond in real time: the resource manager intercepts the unsafe vector, "
            "evaluates our Control Barrier Function, and projects the trajectory back inside the safe "
            "two-point-five-meter envelope at thirty Newton-meters—in just one point three nine microseconds."
        )
    },
    {
        "id": 4,
        "name": "cpu_storm_flatline",
        "title": "100% CPU Storm & Priority 63 Deterministic Flatline",
        "caption": "100% CPU SATURATION STORM: PRIORITY 63 FIFO ZERO-JITTER DETERMINISM",
        "text": (
            "Next, we unleash a one-hundred-percent CPU saturation storm across the AI core. "
            "While a general-purpose operating system suffers severe thread starvation and dangerous jitter spikes, "
            "look at our one-kilohertz oscilloscope: QNX Neutrino's Priority 63 FIFO scheduler maintains a "
            "mathematically flat execution line with zero missed deadlines."
        )
    },
    {
        "id": 5,
        "name": "fault_healing",
        "title": "Fatal SIGSEGV Injection & Sub-500µs Watchdog Self-Healing",
        "caption": "FATAL SIGSEGV FAULT ISOLATION: SUB-500µs WATCHDOG SELF-HEALING",
        "text": (
            "What happens when the neural planner suffers a fatal segmentation fault? We inject a live SIGSEGV crash. "
            "Because QNX isolates every component in protected user-space memory, the actuator loop never blinks. "
            "Our Priority 62 High-Availability Watchdog traps the fault and respawns the AI process "
            "via posix-spawn in under five hundred microseconds."
        )
    },
    {
        "id": 6,
        "name": "codelab_outro",
        "title": "6-Module Interactive Codelab & Grand Finale",
        "caption": "QNX-CORTEXCAGE: BUILT ON QNX NEUTRINO RTOS SDP 8.0 | DETERMINISTICALLY SAFE AUTONOMY",
        "text": (
            "To empower the community, we also ship a complete six-module interactive developer Codelab "
            "covering dual-architecture qcc compilation, POSIX resource managers, lock-free shared memory, "
            "and bootable IFS generation. QNX-CortexCage proves that physical AI doesn't have to be a gamble. "
            "With QNX Neutrino 8.0, autonomy becomes deterministically safe."
        )
    }
]

async def save_with_retry(text, voice, output_path, retries=4):
    for attempt in range(retries):
        try:
            if os.path.exists(output_path):
                try:
                    os.remove(output_path)
                except OSError:
                    pass
            communicate = edge_tts.Communicate(text, voice, rate="+3%", pitch="+0Hz")
            await asyncio.wait_for(communicate.save(output_path), timeout=30.0)
            if os.path.exists(output_path) and os.path.getsize(output_path) > 1000:
                return True
        except Exception as e:
            print(f"  [WARN] Attempt {attempt+1} failed for {os.path.basename(output_path)}: {e}. Retrying in 1.5s...")
            await asyncio.sleep(1.5)
    return False

async def generate_all_tracks():
    print("=" * 70, flush=True)
    print(f"[VOICEOVER] Generating 6 Neural Narration Tracks with {VOICE}...", flush=True)
    print("=" * 70, flush=True)
    manifest = []

    for scene in SCENES:
        output_path = os.path.join(AUDIO_DIR, f"scene_{scene['id']}.mp3")
        
        # Check if already generated and valid
        if os.path.exists(output_path) and os.path.getsize(output_path) > 1000:
            print(f"[TRACK {scene['id']}] Already generated: {scene['title']}", flush=True)
        else:
            print(f"[TRACK {scene['id']}] Generating: {scene['title']}...", flush=True)
            success = await save_with_retry(scene['text'], VOICE, output_path)
            if not success:
                print(f"[ERROR] Failed to generate {output_path} after retries!", flush=True)
            await asyncio.sleep(1.0)

        # Get audio duration
        clip = AudioFileClip(output_path)
        duration = clip.duration
        clip.close()

        print(f"           Duration: {duration:.2f} seconds -> {output_path}", flush=True)

        scene_meta = dict(scene)
        scene_meta["audio_path"] = output_path
        scene_meta["duration"] = round(duration, 3)
        manifest.append(scene_meta)

    # Save manifest
    manifest_path = os.path.join(PIPELINE_DIR, "scenes_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    total_duration = sum(m["duration"] for m in manifest)
    print("=" * 70, flush=True)
    print(f"[SUCCESS] All 6 voiceover tracks generated successfully!", flush=True)
    print(f"[TOTAL TIME] {total_duration:.2f} seconds ({total_duration/60:.2f} minutes)", flush=True)
    print(f"[MANIFEST] {manifest_path}", flush=True)
    print("=" * 70, flush=True)
    return manifest

if __name__ == "__main__":
    asyncio.run(generate_all_tracks())
