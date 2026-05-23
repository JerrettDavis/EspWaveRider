from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
source_path = project_dir / "src" / "visualizer" / "index.html"
header_path = project_dir / "include" / "generated_visualizer_page.h"

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