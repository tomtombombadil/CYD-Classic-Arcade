"""Assemble the web-flasher site for GitHub Pages.

Runs in CI after the firmware builds. Reads the board list from
platformio.ini (envs that set custom_firmware_name), copies each board's
merged firmware into the site, and writes one ESP Web Tools manifest per
board plus boards.json for the page. Adapted from CYD Classic Games.

The ROM page (web/roms/) needs roms/games.json, every game's ROM set: it is
`arcade_host --list-json`, built here with tools/host/build.sh (g++), or
taken from --games-json.

Usage:
    python tools/make_site.py --bins <dir with TTB-CYD-CA_*.bin> --version <v> --out <site dir>
                              [--games-json <file>]
"""
import argparse
import configparser
import datetime
import json
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def boards_from_ini():
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(ROOT / "platformio.ini", encoding="utf-8")
    boards = []
    for section in cfg.sections():
        if not section.startswith("env:"):
            continue
        env = cfg[section]
        name = env.get("custom_firmware_name")
        if not name:
            continue
        boards.append({
            "env": section[4:],
            "file": f"{name}.bin",
            "name": name,
            "title": env.get("custom_board_title", name),
            "hint": env.get("custom_board_hint", ""),
            "tested": env.get("custom_board_tested", "yes").strip().lower() != "no",
            "chip": env.get("custom_chip", "esp32").strip().lower(),
        })
    return boards


def games_json():
    """The game list with ROM sets, from the host runner (emu/games.cpp)."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["sh", str(ROOT / "tools" / "host" / "build.sh"), tmp], check=True,
                       stdout=subprocess.DEVNULL)
        out = subprocess.run([str(pathlib.Path(tmp) / "arcade_host"), "--list-json"], check=True,
                             capture_output=True, text=True).stdout
    json.loads(out)     # must be valid
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bins", required=True, type=pathlib.Path)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--games-json", type=pathlib.Path, help="arcade_host --list-json output (else built here)")
    args = ap.parse_args()

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    (out / "firmware").mkdir(parents=True)
    (out / "manifests").mkdir()
    shutil.copytree(ROOT / "web", out, dirs_exist_ok=True)
    games = args.games_json.read_text(encoding="utf-8") if args.games_json else games_json()
    (out / "roms" / "games.json").write_text(games, encoding="utf-8")
    print(f"added roms/games.json ({len(json.loads(games))} games)")

    # Every build gets its own stamp in its file links: the file names stay
    # the same from version to version, and browsers kept flashing a cached
    # older file (Tom's 4.0" got v0.2.0 from the v0.3.0 page)
    now = datetime.datetime.now(datetime.timezone.utc)
    stamp = args.version + "-" + now.strftime("%Y%m%d%H%M%S")
    published = []
    for b in boards_from_ini():
        src = args.bins / b["file"]
        if not src.exists():
            print(f"skip {b['env']}: {src.name} not built")
            continue
        shutil.copy2(src, out / "firmware" / b["file"])
        manifest = {
            "name": f"CYD Classic Arcade - {b['title']}",
            "version": args.version,
            "new_install_prompt_erase": True,
            "builds": [{
                "chipFamily": {"esp32": "ESP32", "esp32s3": "ESP32-S3"}[b["chip"]],
                # Merged image: bootloader + partitions + app, from 0x0
                "parts": [{"path": f"../firmware/{b['file']}?v={stamp}", "offset": 0}],
            }],
        }
        (out / "manifests" / f"{b['name']}.json").write_text(
            json.dumps(manifest, indent=2), encoding="utf-8")
        b["size"] = src.stat().st_size
        published.append(b)
        print(f"added {b['file']}")

    if not published:
        raise SystemExit("no firmware found - nothing to publish")

    (out / "boards.json").write_text(json.dumps({
        "version": args.version,
        "built": now.strftime("%Y-%m-%d %H:%M UTC"),
        "stamp": stamp,
        "boards": published,
    }, indent=2), encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")


if __name__ == "__main__":
    main()
