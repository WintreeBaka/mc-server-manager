#!/usr/bin/env python3
"""Probes every upstream API the manager depends on.

Run this when a download or version list suddenly fails: it tells you which
service changed or is unreachable.

    python tools/check-endpoints.py
"""

from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request

UA = {"User-Agent": "McServerManager/1.0 (+https://localhost)"}

ENDPOINTS = [
    ("mojang manifest", "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"),
    ("paper (v3 projects)", "https://fill.papermc.io/v3/projects/paper"),
    ("paper (v3 builds)", "https://fill.papermc.io/v3/projects/paper/versions/1.21.8/builds"),
    ("paper (legacy v2)", "https://api.papermc.io/v2/projects/paper"),
    ("purpur projects", "https://api.purpurmc.org/v2/purpur"),
    ("purpur latest", "https://api.purpurmc.org/v2/purpur/1.21.1/latest"),
    ("fabric game", "https://meta.fabricmc.net/v2/versions/game"),
    ("fabric loader", "https://meta.fabricmc.net/v2/versions/loader"),
    ("fabric installer", "https://meta.fabricmc.net/v2/versions/installer"),
    ("modrinth search", "https://api.modrinth.com/v2/search?query=essentials&limit=3"
                        "&facets=%5B%5B%22project_type%3Aplugin%22%5D%5D"),
    ("hangar projects", "https://hangar.papermc.io/api/v1/projects?limit=2"),
    ("spiget search", "https://api.spiget.org/v2/search/resources/vault?field=name&size=2"),
]


def summarize(name: str, body: bytes) -> str:
    try:
        data = json.loads(body.decode("utf-8", "replace"))
    except json.JSONDecodeError:
        return f"{len(body)} bytes (non JSON)"
    if name.startswith("mojang"):
        return f"{len(data.get('versions', []))} versions, latest {data.get('latest', {}).get('release')}"
    if name.startswith("paper (v3 projects)"):
        return f"families: {', '.join(list(data.get('versions', {}))[:6])}"
    if name.startswith("paper (v3 builds)"):
        first = data[0] if isinstance(data, list) and data else {}
        key = next(iter(first.get("downloads", {})), "?")
        return f"{len(data)} builds, newest id={first.get('id')} channel={first.get('channel')} key={key}"
    if name.startswith("purpur projects"):
        return f"{len(data.get('versions', []))} versions"
    if name.startswith("fabric game"):
        return f"{len(data)} entries"
    if name.startswith("modrinth"):
        return f"{data.get('total_hits')} hits"
    if name.startswith("hangar"):
        return f"{len(data.get('result', []))} projects"
    if name.startswith("spiget"):
        return f"{len(data)} resources"
    return "ok"


def main() -> int:
    failures = 0
    for name, url in ENDPOINTS:
        try:
            request = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(request, timeout=25) as response:
                body = response.read()
                status = response.status
            detail = summarize(name, body)
            print(f"[ OK ] {name:24s} {status}  {detail}")
        except urllib.error.HTTPError as error:
            failures += 1
            print(f"[FAIL] {name:24s} HTTP {error.code} {error.reason}")
        except Exception as error:  # noqa: BLE001
            failures += 1
            print(f"[FAIL] {name:24s} {type(error).__name__}: {error}")
    print()
    print(f"{len(ENDPOINTS) - failures}/{len(ENDPOINTS)} endpoints reachable")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
