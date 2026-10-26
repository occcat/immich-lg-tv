#!/usr/bin/env python3
"""Download public demo photography once; generate local video and thumbnails.

These are test fixtures, never uploaded to an Immich account. The app package
does not contain the fixtures or a household's server address.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import subprocess
import urllib.request
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
MEDIA = ROOT / "fixtures" / "media"
PHOTO_IDS = [1018, 1015, 1016, 1036, 1040, 1056, 1067, 1039, 1043, 1044, 1050, 1051, 1061, 1065, 1074, 1084, 1080, 1025, 1024, 1020]

def prepare(item):
    index, photo = item
    full = MEDIA / f"photo-{index:02d}.jpg"
    if not full.exists():
        url = f"https://picsum.photos/id/{photo}/1920/1080"
        try:
            with urllib.request.urlopen(url, timeout=40) as response: full.write_bytes(response.read())
        except Exception:
            # Deterministic offline fallback; clearly identified as a test image.
            image = Image.new("RGB", (1920, 1080), (22 + index * 3, 45, 70 + index * 4))
            draw = ImageDraw.Draw(image)
            draw.polygon([(0,1080),(740,210),(1300,1080)], fill=(70,105,115))
            draw.polygon([(700,1080),(1400,330),(1920,1080)], fill=(90,130,140))
            draw.text((60,60), f"IMMICH TV / SYNTHETIC FIXTURE {index+1:02d}", fill="white")
            image.save(full, quality=90)
    with Image.open(full) as image:
        image = image.convert("RGB")
        image.thumbnail((1920, 1080)); image.save(full, quality=90)
        image.thumbnail((480, 320)); image.save(MEDIA / f"thumb-{index:02d}.jpg", quality=85)
        image.save(MEDIA / f"thumb-{index:02d}.webp", quality=85)
    return {"file": full.name, "source": f"https://picsum.photos/id/{photo}/info", "provider": "Lorem Picsum / Unsplash"}

def main():
    MEDIA.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool: credits = list(pool.map(prepare, enumerate(PHOTO_IDS)))
    (MEDIA / "credits.json").write_text(json.dumps(credits, indent=2) + "\n")
    video = MEDIA / "sample.mp4"
    if not video.exists():
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-loop", "1", "-i", str(MEDIA / "photo-00.jpg"), "-t", "12", "-vf", "scale=1600:900,crop=1280:720:x='(in_w-out_w)*t/12':y='(in_h-out_h)/2',format=yuv420p", "-r", "25", "-c:v", "libx264", "-preset", "fast", "-crf", "23", "-movflags", "+faststart", str(video)], check=True)
    audio_video = MEDIA / "sample-audio.mp4"
    if not audio_video.exists():
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(video), "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo", "-c:v", "copy", "-c:a", "aac", "-shortest", "-movflags", "+faststart", str(audio_video)], check=True)
    print(f"Ready: {len(PHOTO_IDS)} demo photographs, thumbnails, and one H.264 video in {MEDIA}")

if __name__ == "__main__": main()
