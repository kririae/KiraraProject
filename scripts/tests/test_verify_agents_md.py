import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(SCRIPTS))
import agent_docs

spec = importlib.util.spec_from_file_location("verify_agents_md", SCRIPTS / "verify-agents-md.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


SKILL = """---
name: a
description: Use when you test.
---

# A
"""


def problems(files):
    """Returns the problems of a temporary repository holding the given files."""
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True, timeout=60)
        subprocess.run(["git", "add", *files], cwd=root, check=True, timeout=60)
        return module.verify(root, agent_docs.tracked_files(root))


class VerifyAgentsMdTest(unittest.TestCase):
    def test_every_agents_md_over_the_word_limit_is_rejected(self):
        files = {"AGENTS.md": "word\n" * 900, "sub/AGENTS.md": "word " * 901}
        self.assertEqual(problems(files), [("sub/AGENTS.md", 1, "901 words, more than 900")])

    def test_route_with_the_task_of_the_description_is_accepted(self):
        files = {
            ".agents/skills/a/SKILL.md": SKILL,
            "AGENTS.md": "- Before you test, follow [a](.agents/skills/a/SKILL.md).\n",
        }
        self.assertEqual(problems(files), [])

    def test_route_that_differs_from_the_description_is_rejected(self):
        files = {
            ".agents/skills/a/SKILL.md": SKILL,
            "sub/AGENTS.md": "# Sub\n\n- Before you run tests, follow [a](../.agents/skills/a/SKILL.md).\n",
        }
        self.assertEqual(
            problems(files),
            [("sub/AGENTS.md", 3, "the route to the skill a differs from its description")],
        )

    def test_link_to_a_skill_that_is_not_a_route_is_rejected(self):
        files = {
            ".agents/skills/a/SKILL.md": SKILL,
            "AGENTS.md": "See [a](.agents/skills/a/SKILL.md).\n",
        }
        self.assertEqual(problems(files), [("AGENTS.md", 1, "the link to the skill a is not a route")])


if __name__ == "__main__":
    unittest.main()
