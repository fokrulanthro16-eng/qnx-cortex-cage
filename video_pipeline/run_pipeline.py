"""
QNX-CortexCage: Master Video Pipeline Orchestrator
Automates voiceover generation, browser recording, and Ultra-HD video mastering.
"""

import os
import sys
import subprocess
import time
import requests

PIPELINE_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(PIPELINE_DIR, ".."))
FINAL_MP4 = os.path.join(PROJECT_ROOT, "QNX_CortexCage_Official_Demo.mp4")

def check_cockpit_server():
    print("[PRE-FLIGHT] Verifying QNX-CortexCage Cockpit on http://localhost:3080...", flush=True)
    try:
        r = requests.get("http://localhost:3080/api/status", timeout=3.0)
        if r.status_code == 200 and r.json().get("status") == "online":
            print("[OK] Cockpit backend is active and streaming QNX telemetry.", flush=True)
            return True
    except Exception as e:
        print(f"[ERROR] Could not reach Cockpit server: {e}", flush=True)
        return False
    return False

def run_step(script_name, description):
    print("\n" + "#" * 70, flush=True)
    print(f"[STEP] {description} ({script_name})", flush=True)
    print("#" * 70, flush=True)
    t0 = time.time()
    script_path = os.path.join(PIPELINE_DIR, script_name)
    res = subprocess.run([sys.executable, script_path], text=True)
    elapsed = time.time() - t0
    if res.returncode != 0:
        print(f"\n[FATAL] Step '{script_name}' failed with return code {res.returncode}!", flush=True)
        sys.exit(res.returncode)
    print(f"\n[STEP COMPLETE] Finished in {elapsed:.1f} seconds.\n", flush=True)

def main():
    print("=" * 70, flush=True)
    print("  QNX-CORTEXCAGE: AUTOMATED STUDIO-GRADE KEYNOTE VIDEO PIPELINE", flush=True)
    print("  Target: QNX_CortexCage_Official_Demo.mp4 (Ultra-HD / 60fps)", flush=True)
    print("=" * 70, flush=True)

    if not check_cockpit_server():
        print("[ERROR] Please make sure http://localhost:3080 is online.", flush=True)
        sys.exit(1)

    t_start = time.time()

    # Step 1: Voiceover Generation
    run_step("generate_voiceover.py", "1/3: Microsoft Neural Voiceover Synthesis (6 Scenes)")

    # Step 2: Live Browser Recording with Keynote HUD & Cursor Automation
    run_step("record_scenes.py", "2/3: Live High-DPI Browser Recording & Action Choreography")

    # Step 3: Ultra-HD Mastering & Concatenation
    run_step("master_video.py", "3/3: Video-Audio Alignment & Ultra-HD MP4 Concatenation")

    total_time = time.time() - t_start
    print("=" * 70, flush=True)
    print(f"[SUCCESS] MASTER PIPELINE COMPLETED IN {total_time:.1f}s ({total_time/60:.2f} mins)", flush=True)
    print(f"Official Keynote Video: {FINAL_MP4}", flush=True)
    if os.path.exists(FINAL_MP4):
        print(f"File Size: {os.path.getsize(FINAL_MP4):,} bytes ({os.path.getsize(FINAL_MP4)/(1024*1024):.2f} MB)", flush=True)
    print("=" * 70, flush=True)

if __name__ == "__main__":
    main()
