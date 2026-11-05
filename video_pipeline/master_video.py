"""
QNX-CortexCage: Ultra-HD Video & Audio Mastering Engine
Combines all 6 recorded high-DPI video clips with neural voiceover audio tracks,
applies H.264/AAC mastering, and generates the final polished official keynote MP4.
"""

import os
import sys
import json
import subprocess
import shutil

PIPELINE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(PIPELINE_DIR, "raw_videos")
AUDIO_DIR = os.path.join(PIPELINE_DIR, "audio")
PROCESSED_DIR = os.path.join(PIPELINE_DIR, "processed_scenes")
MANIFEST_PATH = os.path.join(PIPELINE_DIR, "scenes_manifest.json")
OUTPUT_MP4 = os.path.abspath(os.path.join(PIPELINE_DIR, "..", "QNX_CortexCage_Official_Demo.mp4"))
os.makedirs(PROCESSED_DIR, exist_ok=True)

def find_ffmpeg():
    candidates = [
        r"C:\Users\WALTON\.gemini\antigravity\bin\ffmpeg.exe",
        r"C:\Users\WALTON\AppData\Roaming\npm\ffmpeg.exe",
        "ffmpeg.exe",
        "ffmpeg"
    ]
    for c in candidates:
        if os.path.exists(c) or shutil.which(c):
            return c
    return "ffmpeg"

FFMPEG = find_ffmpeg()

def master_all_scenes():
    print("=" * 70)
    print(f"[MASTERING] Using FFmpeg: {FFMPEG}")
    print(f"[TARGET] {OUTPUT_MP4}")
    print("=" * 70)

    if not os.path.exists(MANIFEST_PATH):
        print(f"[ERROR] Manifest {MANIFEST_PATH} not found.")
        sys.exit(1)

    with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    mastered_clips = []

    for scene in manifest:
        scene_id = scene["id"]
        raw_video = os.path.join(RAW_DIR, f"scene_{scene_id}.webm")
        audio_file = os.path.join(AUDIO_DIR, f"scene_{scene_id}.mp3")
        mastered_clip = os.path.join(PROCESSED_DIR, f"scene_{scene_id}_master.mp4")

        if not os.path.exists(raw_video):
            print(f"[ERROR] Video {raw_video} not found for Scene {scene_id}!")
            sys.exit(1)
        if not os.path.exists(audio_file):
            print(f"[ERROR] Audio {audio_file} not found for Scene {scene_id}!")
            sys.exit(1)

        print(f"\n[ENCODING SCENE {scene_id}/6] {scene['title']}...")

        # Encode video with audio, cutting exactly at audio duration with high-DPI mastering
        cmd = [
            FFMPEG, "-y",
            "-i", raw_video,
            "-i", audio_file,
            "-c:v", "libx264",
            "-preset", "fast",
            "-crf", "18",
            "-r", "60",
            "-pix_fmt", "yuv420p",
            "-c:a", "aac",
            "-b:a", "192k",
            "-ar", "48000",
            "-ac", "2",
            "-shortest",
            mastered_clip
        ]

        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            print(f"[ERROR] FFmpeg failed on scene {scene_id}:\n{res.stderr}")
            sys.exit(1)

        size = os.path.getsize(mastered_clip)
        print(f"[OK] Scene {scene_id} mastered -> {mastered_clip} ({size:,} bytes)")
        mastered_clips.append(mastered_clip)

    # Concatenate all 6 mastered scenes
    concat_list_file = os.path.join(PROCESSED_DIR, "concat_list.txt")
    with open(concat_list_file, "w", encoding="utf-8") as f:
        for clip in mastered_clips:
            # Escape path for ffmpeg concat demuxer
            safe_path = clip.replace("\\", "/")
            f.write(f"file '{safe_path}'\n")

    print("\n" + "=" * 70)
    print(f"[CONCATENATING] Merging 6 scenes into master keynote MP4...")
    print("=" * 70)

    concat_cmd = [
        FFMPEG, "-y",
        "-f", "concat",
        "-safe", "0",
        "-i", concat_list_file,
        "-c", "copy",
        "-movflags", "+faststart",
        OUTPUT_MP4
    ]

    res = subprocess.run(concat_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Concatenation failed:\n{res.stderr}")
        sys.exit(1)

    # Inspect final file
    inspect_cmd = [FFMPEG, "-i", OUTPUT_MP4]
    inspect_res = subprocess.run(inspect_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    print("\n" + "=" * 70)
    print("[SUCCESS] ULTRA-HD KEYNOTE DEMO MASTERED SUCCESSFULLY!")
    print("=" * 70)
    print(f"File Path:   {OUTPUT_MP4}")
    print(f"File Size:   {os.path.getsize(OUTPUT_MP4):,} bytes ({os.path.getsize(OUTPUT_MP4)/(1024*1024):.2f} MB)")

    # Print FFmpeg stream details
    for line in inspect_res.stderr.splitlines():
        if "Duration:" in line or "Stream #" in line:
            print(f"  {line.strip()}")
    print("=" * 70)

if __name__ == "__main__":
    master_all_scenes()
