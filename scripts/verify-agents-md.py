#!/usr/bin/env python3
"""Verifies the word limit and the routes of every AGENTS.md.

An AGENTS.md has at most 900 words, counted as `wc -w` counts them. Each of its links to a skill is
a route, "Before you X, follow [skill]", and X is the task in the description of the skill, "Use
when you X."

Usage: verify-agents-md.py [root]
"""

import pathlib
import posixpath
import re
import sys

import agent_docs

MAX_WORDS = 900
ROUTE = re.compile(r"Before you (.+), follow ")


def skill_of(document, href):
    """Returns the name of the skill whose SKILL.md a link of a document reaches, or None."""
    joined = posixpath.join(posixpath.dirname(document), href)
    target = pathlib.PurePosixPath(posixpath.normpath(joined))
    skills = pathlib.PurePosixPath(agent_docs.SKILLS)
    if target.parent.parent == skills and target.name == "SKILL.md":
        return target.parent.name
    return None


def verify_routes(root, path, text):
    """Returns (line number, problem) pairs for the links of an AGENTS.md to skills."""
    problems = []
    tokens, _ = agent_docs.parse(text)
    for token in tokens:
        if token.type != "inline":
            continue
        before = []
        for child in token.children:
            if child.type in ("text", "code_inline"):
                before.append(child.content)
            elif child.type == "softbreak":
                before.append(" ")
            elif child.type == "link_open":
                name = skill_of(path, child.attrGet("href"))
                if name is None:
                    continue
                line = token.map[0] + 1
                route = ROUTE.fullmatch("".join(before))
                if route is None:
                    problems.append((line, f"the link to the skill {name} is not a route"))
                    continue
                skill = agent_docs.read_text(root, f"{agent_docs.SKILLS}/{name}/SKILL.md")
                pairs = agent_docs.front_matter(skill) if skill is not None else None
                description = pairs.get("description") if pairs else None
                if description is not None and description != f"Use when you {route.group(1)}.":
                    problems.append((line, f"the route to the skill {name} differs from its description"))
    return problems


def verify(root, tracked):
    """Returns (path, line number, problem) tuples for the AGENTS.md files that break a rule."""
    problems = []
    for path in tracked:
        if pathlib.PurePosixPath(path).name != "AGENTS.md":
            continue
        text = agent_docs.read_text(root, path)
        if text is None:
            continue
        words = len(text.split())
        if words > MAX_WORDS:
            problems.append((path, 1, f"{words} words, more than {MAX_WORDS}"))
        problems.extend((path, line, problem) for line, problem in verify_routes(root, path, text))
    return problems


def main(argv):
    if len(argv) > 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = pathlib.Path(argv[1] if len(argv) == 2 else ".")
    problems = verify(root, agent_docs.tracked_files(root))
    for path, line, problem in problems:
        print(f"{path}:{line}: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
