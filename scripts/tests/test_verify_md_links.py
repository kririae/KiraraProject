import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(SCRIPTS))
import agent_docs

spec = importlib.util.spec_from_file_location("verify_md_links", SCRIPTS / "verify-md-links.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def problems(files, untracked=()):
    """Returns (path, line, problem) for a temporary repository holding the given files."""
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        for name, text in {**files, **dict.fromkeys(untracked, "")}.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True, timeout=60)
        subprocess.run(["git", "add", *files], cwd=root, check=True, timeout=60)
        tracked = agent_docs.tracked_files(root)
        result = module.verify(root, tracked, agent_docs.agent_documents(tracked))
        return [(path, line, problem) for path, line, _, problem in result]


class VerifyMdLinksTest(unittest.TestCase):
    def test_missing_target_is_rejected(self):
        self.assertEqual(
            problems({"AGENTS.md": "x\n\n[a](nope.md)\n"}),
            [("AGENTS.md", 3, "target is not a tracked file or directory")],
        )

    def test_untracked_target_is_rejected(self):
        self.assertEqual(
            problems({"AGENTS.md": "[a](local.txt)\n"}, untracked=["local.txt"]),
            [("AGENTS.md", 1, "target is not a tracked file or directory")],
        )

    def test_missing_anchor_is_rejected(self):
        self.assertEqual(
            problems({"AGENTS.md": "# Real Heading\n\n[a](#real-heading) [b](#other)\n"}),
            [("AGENTS.md", 3, "no such anchor in the target")],
        )

    def test_repeated_heading_gets_numbered_anchors(self):
        text = "# Same\n\n# Same\n\n[a](#same-1)\n\n[b](#same-2)\n"
        self.assertEqual(
            problems({"AGENTS.md": text}),
            [("AGENTS.md", 7, "no such anchor in the target")],
        )

    def test_target_resolves_against_the_source_directory(self):
        files = {
            "AGENTS.md": "[a](docs/a.md)\n",
            "docs/a.md": "[up](../AGENTS.md) [side](b.md)\n",
            "docs/b.md": "x\n",
        }
        self.assertEqual(problems(files), [])

    def test_path_and_fragment_are_percent_decoded(self):
        files = {"AGENTS.md": "[a](a%20b.md#my%2Dhead)\n", "a b.md": "# My Head\n"}
        self.assertEqual(problems(files), [])

    def test_scheme_urls_are_skipped(self):
        text = "[a](https://example.com/x.md#y) [b](mailto:a@b.c) [c](//host/x) [d](/abs/x)\n"
        self.assertEqual(problems({"AGENTS.md": text}), [])

    def test_reference_definition_is_checked(self):
        self.assertEqual(
            problems({"AGENTS.md": "x\n\n[a]: nope.md\n"}),
            [("AGENTS.md", 3, "target is not a tracked file or directory")],
        )

    def test_document_not_linked_from_root_is_rejected(self):
        files = {"AGENTS.md": "[a](docs/linked.md) [b](docs)\n", "docs/linked.md": "x\n", "docs/lone.md": "y\n"}
        self.assertEqual(
            problems(files),
            [("docs/lone.md", 1, "not reachable from the root AGENTS.md")],
        )

    def test_skill_links_only_to_skills(self):
        files = {
            "AGENTS.md": "[a](.agents/skills/a/examples.md)\n",
            ".agents/skills/a/examples.md": (
                "[b](../b/SKILL.md) [c](../../../AGENTS.md)\n\n[d](/AGENTS.md) [e](https://llvm.org)\n"
            ),
            ".agents/skills/b/SKILL.md": "x\n",
        }
        self.assertEqual(
            problems(files),
            [
                (".agents/skills/a/examples.md", 1, "a skill links only to skills"),
                (".agents/skills/a/examples.md", 3, "a skill links only to skills"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
