from __future__ import annotations

import argparse
import json
import random
import sys
import time
import urllib.parse
import urllib.request


def get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=15) as response:
        return json.loads(response.read().decode("utf-8"))


def call(server: str, path: str, query: dict | None = None) -> dict:
    qs = f"?{urllib.parse.urlencode(query or {})}" if query else ""
    url = server.rstrip("/") + path + qs
    print("GET", url)
    data = get_json(url)
    print(json.dumps(data, indent=2)[:1600])
    return data


def random_item(server: str, kind: str) -> dict | None:
    page = call(server, f"/api/library/{kind}", {"page": 1})
    items = page.get("items") or []
    return random.choice(items) if items else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Exercise Nexus backend playback endpoints with random indexed files.")
    parser.add_argument("--server", default="http://127.0.0.1:8765")
    parser.add_argument("--all", action="store_true", help="Run audio, synced video, graphics video, and seek checks")
    parser.add_argument("--pause", type=float, default=3)
    args = parser.parse_args(argv)

    call(args.server, "/health")

    audio = random_item(args.server, "audio")
    if audio:
        call(args.server, f"/api/audio/{audio['id']}/play", {"start": 0})
        time.sleep(args.pause)
        call(args.server, "/api/playback/seek", {"direction": "forward"})
        time.sleep(args.pause)
    else:
        print("No audio files indexed")

    video = random_item(args.server, "videos")
    if video:
        call(args.server, f"/api/video/{video['id']}/play", {"audio": "true", "aspect": "square"})
        time.sleep(args.pause)
        call(args.server, "/api/playback/seek", {"direction": "forward"})
        time.sleep(args.pause)
        call(args.server, f"/api/video/{video['id']}/play", {"audio": "false", "aspect": "fit"})
        time.sleep(args.pause)
    else:
        print("No video files indexed")

    call(args.server, "/api/playback/stop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
