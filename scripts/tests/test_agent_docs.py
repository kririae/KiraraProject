import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
import agent_docs


class AgentDocumentsTest(unittest.TestCase):
    def test_scope(self):
        included = [
            "AGENTS.md",
            "flux/AGENTS.md",
            "docs/design.md",
            "flux/docs/design.md",
            ".agents/skills/docs/SKILL.md",
        ]
        excluded = [
            "README.md",
            "flux/README.md",
            "docs/README.md",
            "docs/image.png",
            "a/b/c/docs/x.md",
            "flux/x.md",
        ]
        self.assertEqual([p for p in included if not agent_docs.is_agent_document(p)], [])
        self.assertEqual([p for p in excluded if agent_docs.is_agent_document(p)], [])


if __name__ == "__main__":
    unittest.main()
