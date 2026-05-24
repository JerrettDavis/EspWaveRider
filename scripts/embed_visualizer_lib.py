import os
import re
import subprocess
from pathlib import Path


def run_git(project_dir, *args):
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


def resolve_firmware_version(project_dir, env_vars=None, git_runner=None):
    env_vars = env_vars or os.environ
    git_runner = git_runner or (lambda *args: run_git(project_dir, *args))

    github_ref_name = env_vars.get("GITHUB_REF_NAME", "").strip()
    if re.match(r"^v?\d+\.\d+\.\d+([.-].*)?$", github_ref_name):
        return github_ref_name

    described = git_runner("describe", "--tags", "--dirty", "--always", "--match", "v[0-9]*")
    if described:
        return described

    short_sha = git_runner("rev-parse", "--short", "HEAD")
    if short_sha:
        return f"dev-{short_sha}"

    return "dev"


def resolve_git_sha(project_dir, git_runner=None):
    git_runner = git_runner or (lambda *args: run_git(project_dir, *args))
    return git_runner("rev-parse", "--short", "HEAD") or "unknown"


def build_cpp_defines(project_dir, pio_env, env_vars=None, git_runner=None):
    return [
        ("ESPWAVERIDER_FIRMWARE_VERSION", cpp_string(resolve_firmware_version(project_dir, env_vars, git_runner))),
        ("ESPWAVERIDER_BUILD_TARGET", cpp_string(pio_env)),
        ("ESPWAVERIDER_GIT_SHA", cpp_string(resolve_git_sha(project_dir, git_runner))),
    ]


def normalize_html_for_embedding(html):
    return html.replace("\r\n", "\n")


def render_header(html, delimiter="LBHTML"):
    if delimiter in html:
        raise RuntimeError("Embedded visualizer delimiter unexpectedly appears in index.html")

    return f'''#pragma once

#include <pgmspace.h>

static const char kEmbeddedVisualizerPage[] PROGMEM = R"{delimiter}(
{html}
){delimiter}";
'''


def write_embedded_header(source_path, header_path, delimiter="LBHTML"):
    html = normalize_html_for_embedding(Path(source_path).read_text(encoding="utf-8"))
    header = render_header(html, delimiter)
    Path(header_path).write_text(header, encoding="utf-8")
    return header