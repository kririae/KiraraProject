import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(SCRIPTS))
import agent_docs

spec = importlib.util.spec_from_file_location("verify_skills", SCRIPTS / "verify-skills.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

SKILL = """---
name: a
description: Use when testing.
---

# A

Mental model.

## Steps

- One.

## Bad things

- Two.
"""


def problems(files):
    """Returns (path, line, problem) for a temporary repository holding the given files."""
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True, timeout=60)
        subprocess.run(["git", "add", *files], cwd=root, check=True, timeout=60)
        return module.verify(root, agent_docs.tracked_files(root))


def skill_problems(text):
    return problems({".agents/skills/a/SKILL.md": text})


class VerifySkillsTest(unittest.TestCase):
    def test_skill_of_the_shape_passes(self):
        self.assertEqual(skill_problems(SKILL), [])

    def test_directory_without_skill_file_is_rejected(self):
        self.assertEqual(
            problems({".agents/skills/a/examples.md": "x\n"}),
            [(".agents/skills/a", 1, "the skill does not hold SKILL.md")],
        )

    def test_missing_front_matter_is_rejected(self):
        text = SKILL.split("---\n", 2)[2]
        self.assertEqual(
            skill_problems(text),
            [(".agents/skills/a/SKILL.md", 1, "SKILL.md does not open with a front matter block")],
        )

    def test_name_that_is_not_the_directory_name_is_rejected(self):
        self.assertEqual(
            skill_problems(SKILL.replace("name: a", "name: b")),
            [(".agents/skills/a/SKILL.md", 1, "front matter name is not the directory name a")],
        )

    def test_empty_description_is_rejected(self):
        self.assertEqual(
            skill_problems(SKILL.replace("description: Use when testing.", "description:")),
            [(".agents/skills/a/SKILL.md", 1, "front matter description is empty")],
        )

    def test_first_heading_must_be_level_one(self):
        self.assertEqual(
            skill_problems(SKILL.replace("# A", "## A")),
            [(".agents/skills/a/SKILL.md", 6, "the first heading is not a level-1 heading")],
        )

    def test_paragraph_must_follow_the_first_heading(self):
        self.assertEqual(
            skill_problems(SKILL.replace("Mental model.", "- A list.")),
            [(".agents/skills/a/SKILL.md", 6, "no paragraph follows the first heading")],
        )

    def test_skill_without_level_two_heading_is_rejected(self):
        self.assertEqual(
            skill_problems(SKILL.split("## Steps")[0]),
            [(".agents/skills/a/SKILL.md", 6, "no level-2 heading starts with Bad")],
        )

    def test_skill_must_end_with_bad_heading(self):
        self.assertEqual(
            skill_problems(SKILL + "\n## Notes\n\n- Three.\n"),
            [(".agents/skills/a/SKILL.md", 18, "the last level-2 heading does not start with Bad")],
        )


if __name__ == "__main__":
    unittest.main()
