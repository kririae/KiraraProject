import importlib.util
import pathlib
import sys
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(SCRIPTS))
import agent_docs

spec = importlib.util.spec_from_file_location("verify_md_wrap", SCRIPTS / "verify-md-wrap.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def lines(text):
    return [number for number, _ in module.verify(text)]


class VerifyMdWrapTest(unittest.TestCase):
    def test_paragraph_on_several_lines_is_rejected(self):
        self.assertEqual(lines("# Title\n\nOne\ntwo\n"), [3])

    def test_paragraph_in_list_item_on_several_lines_is_rejected(self):
        self.assertEqual(lines("- one\n  two\n- three\n"), [1])

    def test_headings_code_blocks_and_tables_are_exempt(self):
        text = "Title\n=====\n\n```\na\nb\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n"
        self.assertEqual(lines(text), [])

    def test_front_matter_is_skipped_and_keeps_line_numbers(self):
        text = "---\nname: a\nlist:\n  - b\n---\n\nOne\ntwo\n"
        with tempfile.TemporaryDirectory() as root:
            (pathlib.Path(root) / "SKILL.md").write_text(text)
            self.assertEqual(lines(agent_docs.read_markdown(root, "SKILL.md")), [7])


if __name__ == "__main__":
    unittest.main()
