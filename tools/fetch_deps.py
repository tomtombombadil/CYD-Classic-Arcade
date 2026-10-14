# Downloads the firmware's outside source code into external/ (git-ignored):
#   Bluepad32  - Bluetooth game controller host (Apache 2.0)
#   BTstack    - the Bluetooth stack Bluepad32 runs on (free for
#                non-commercial use; see THIRD_PARTY_NOTICES.md)
#   LovyanGFX  - display + touch driver (FreeBSD)
# Each is pinned to a tag or commit and fetched as a GitHub zip, so no git
# and no submodules are needed - PlatformIO runs this before every build
# (extra_scripts = pre:...) on Windows and in CI alike, and it does nothing
# once the pinned versions are there. Can also be run by hand:
#   python tools/fetch_deps.py
import io
import os
import shutil
import sys
import urllib.request
import zipfile

# name -> (GitHub owner/repo, tag or commit)
DEPS = {
    "bluepad32": ("ricardoquesada/bluepad32", "5.0.0-beta0"),
    "btstack": ("bluekitchen/btstack", "v1.8.2"),
    "LovyanGFX": ("lovyan03/LovyanGFX", "1.2.31"),
}

STAMP = ".cyd_ref"


def fetch(root, name, repo, ref):
    dest = os.path.join(root, "external", name)
    stamp = os.path.join(dest, STAMP)
    try:
        with open(stamp, encoding="utf-8") as f:
            if f.read().strip() == ref:
                return
    except OSError:
        pass
    url = "https://codeload.github.com/%s/zip/%s" % (repo, ref)
    print("fetch_deps: downloading %s %s" % (name, ref))
    with urllib.request.urlopen(url, timeout=120) as r:
        data = r.read()
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for info in z.infolist():
            # strip the "<repo>-<ref>/" folder GitHub puts everything in
            parts = info.filename.split("/", 1)
            if len(parts) < 2 or not parts[1]:
                continue
            out = os.path.join(dest, parts[1])
            if info.is_dir():
                os.makedirs(out, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
            # keep scripts executable (zip external attributes, Unix part)
            mode = info.external_attr >> 16
            if mode & 0o111:
                os.chmod(out, 0o755)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(ref + "\n")


def main(root):
    for name, (repo, ref) in DEPS.items():
        fetch(root, name, repo, ref)


try:
    Import("env")  # noqa: F821 - provided when PlatformIO runs this
    main(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    if __name__ == "__main__":
        main(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        sys.exit(0)
