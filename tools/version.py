# PlatformIO pre-build script: the firmware version comes from the VERSION
# file at the repo root (semantic versioning: X.Y.Z - a fix bumps Z, a new
# feature bumps Y, a major revision bumps X). Writes main/version_gen.h
# (git-ignored) with
#   CYD_ARCADE_VERSION  "v0.1.0"
#   CYD_ARCADE_BUILD    the git commit ("1a2b3c4"), or "" outside a git checkout
# A header rather than -D flags: the ESP-IDF build compiles each component
# with its own flags. Runs inside PlatformIO's own Python, on Windows and in
# CI alike.
import os
import subprocess

Import("env")  # noqa: F821 - provided by PlatformIO

root = env.subst("$PROJECT_DIR")  # noqa: F821
with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
    version = "v" + f.read().strip()
# CI passes the release tag; a pre-release suffix (v1.2.0-beta.1) shows too
tag = os.environ.get("CYD_VERSION", "")
if tag == version or tag.startswith(version + "-"):
    version = tag
try:
    build = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=root,
                                    stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    build = ""
text = ('#pragma once\n#define CYD_ARCADE_VERSION "%s"\n#define CYD_ARCADE_BUILD "%s"\n'
        % (version, build))
path = os.path.join(root, "main", "version_gen.h")
try:
    with open(path, encoding="utf-8") as f:
        same = f.read() == text
except OSError:
    same = False
if not same:  # rewrite only on change, so nothing rebuilds needlessly
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
print("CYD Classic Arcade", version, build)
