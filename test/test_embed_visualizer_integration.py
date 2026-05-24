import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from embed_visualizer_lib import normalize_html_for_embedding, write_embedded_header  # noqa: E402


class EmbedVisualizerIntegrationTests(unittest.TestCase):
    def test_normalize_html_for_embedding_rewrites_windows_line_endings(self):
        self.assertEqual(normalize_html_for_embedding("a\r\nb\r\n"), "a\nb\n")

    def test_write_embedded_header_generates_progmem_wrapper(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            source_path = temp_path / "index.html"
            header_path = temp_path / "generated_visualizer_page.h"
            source_path.write_text("<div>hello</div>\r\n<span>world</span>\r\n", encoding="utf-8")

            header = write_embedded_header(source_path, header_path)

            self.assertIn("#pragma once", header)
            self.assertIn("PROGMEM", header)
            self.assertIn("<div>hello</div>", header)
            self.assertIn("<span>world</span>", header)
            self.assertEqual(header_path.read_text(encoding="utf-8"), header)


if __name__ == "__main__":
    unittest.main()