#!/usr/bin/env python3
"""Verifies the shape of the skills.

A skill is a directory directly under `.agents/skills` that holds a tracked file. It holds
`SKILL.md`. `SKILL.md` opens with a front matter block whose `name` is the directory name and whose
`description` is not empty. Its first heading is a level-1 heading, followed directly by a
paragraph. Its last level-2 heading starts with `Bad `. That each skill links only to skills and
outside sources is verified by verify-md-links.py.

Usage: verify-skills.py [root]
"""

import pathlib
import sys

import agent_docs


def skill_names(tracked):
    """Returns the sorted names of the directories directly under the skills that hold a file."""
    names = set()
    for path in tracked:
        if agent_docs.is_in_skills(path):
            parts = pathlib.PurePosixPath(path).relative_to(agent_docs.SKILLS).parts
            if len(parts) > 1:
                names.add(parts[0])
    return sorted(names)


def verify_front_matter(name, text):
    """Returns (line number, problem) pairs for the front matter of a skill."""
    pairs = agent_docs.front_matter(text)
    if pairs is None:
        return [(1, "SKILL.md does not open with a front matter block")]
    problems = []
    if pairs.get("name") != name:
        problems.append((1, f"front matter name is not the directory name {name}"))
    if not pairs.get("description"):
        problems.append((1, "front matter description is empty"))
    return problems


def verify_headings(text):
    """Returns (line number, problem) pairs for the headings of a skill."""
    tokens, _ = agent_docs.parse(text)
    headings = [
        (index, token.tag, agent_docs.rendered(tokens[index + 1]), token.map[0] + 1)
        for index, token in enumerate(tokens)
        if token.type == "heading_open"
    ]
    if not headings:
        return [(1, "SKILL.md has no heading")]
    problems = []
    first, tag, _, line = headings[0]
    if tag != "h1":
        problems.append((line, "the first heading is not a level-1 heading"))
    after = first + 3
    if after >= len(tokens) or tokens[after].type != "paragraph_open":
        problems.append((line, "no paragraph follows the first heading"))
    level_2 = [(text, line) for _, tag, text, line in headings if tag == "h2"]
    if not level_2:
        problems.append((line, "no level-2 heading starts with Bad"))
    elif not level_2[-1][0].startswith("Bad "):
        problems.append((level_2[-1][1], "the last level-2 heading does not start with Bad"))
    return problems


def verify(root, tracked):
    """Returns (path, line, problem) tuples for the skills that do not have the shape."""
    problems = []
    tracked_set = set(tracked)
    for name in skill_names(tracked):
        path = f"{agent_docs.SKILLS}/{name}/SKILL.md"
        body = agent_docs.read_markdown(root, path) if path in tracked_set else None
        if body is None:
            problems.append((f"{agent_docs.SKILLS}/{name}", 1, "the skill does not hold SKILL.md"))
            continue
        found = verify_front_matter(name, agent_docs.read_text(root, path))
        found += verify_headings(body)
        problems.extend((path, line, problem) for line, problem in found)
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
