import os
import re
import subprocess
from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))


def run_git(*args):
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=project_dir,
            capture_output=True,
            check=True,
            text=True,
        )
        return completed.stdout.strip()
    except Exception:
        return ""


def cpp_string(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


def resolve_firmware_version():
    github_ref_name = os.environ.get("GITHUB_REF_NAME", "").strip()
    if re.match(r"^v?\d+\.\d+\.\d+([.-].*)?$", github_ref_name):
        return github_ref_name

    described = run_git("describe", "--tags", "--dirty", "--always", "--match", "v[0-9]*")
    if described:
        return described

    short_sha = run_git("rev-parse", "--short", "HEAD")
    if short_sha:
        return f"dev-{short_sha}"

    return "dev"


def resolve_git_sha():
    return run_git("rev-parse", "--short", "HEAD") or "unknown"

source_path = project_dir / "src" / "visualizer" / "index.html"
header_path = project_dir / "include" / "generated_visualizer_page.h"

env.Append(
    CPPDEFINES=[
        ("ESPWAVERIDER_FIRMWARE_VERSION", cpp_string(resolve_firmware_version())),
        ("ESPWAVERIDER_BUILD_TARGET", cpp_string(env.subst("$PIOENV"))),
        ("ESPWAVERIDER_GIT_SHA", cpp_string(resolve_git_sha())),
    ]
)

html = source_path.read_text(encoding="utf-8").replace("\r\n", "\n")
delimiter = "LBHTML"

if delimiter in html:
    raise RuntimeError("Embedded visualizer delimiter unexpectedly appears in index.html")

header = f'''#pragma once

#include <pgmspace.h>

static const char kEmbeddedVisualizerPage[] PROGMEM = R"{delimiter}(
{html}
){delimiter}";
'''

header_path.write_text(header, encoding="utf-8")