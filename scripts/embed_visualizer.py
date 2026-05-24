from pathlib import Path

from embed_visualizer_lib import build_cpp_defines, write_embedded_header

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))

source_path = project_dir / "src" / "visualizer" / "index.html"
header_path = project_dir / "include" / "generated_visualizer_page.h"

env.Append(CPPDEFINES=build_cpp_defines(project_dir, env.subst("$PIOENV")))
write_embedded_header(source_path, header_path)