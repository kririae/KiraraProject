"""Shared by the agent document verifiers: which files are agent documents, and how to parse them.

An agent document is a tracked file that is `AGENTS.md` in any directory, a Markdown file under a
`docs` directory at the root or in a subproject, or a Markdown file under `.agents/skills`.
README files are not agent documents.
"""

import pathlib
import subprocess

from markdown_it import MarkdownIt

ROOT_DOCUMENT = "AGENTS.md"
SKILLS = ".agents/skills"


def tracked_files(root):
    """Returns the sorted repository-relative POSIX paths that git tracks under root."""
    output = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
        timeout=60,
    ).stdout.decode("utf-8")
    return sorted(path for path in output.split("\0") if path)


def is_agent_document(path):
    """Tells whether a repository-relative POSIX path is an agent document."""
    parts = pathlib.PurePosixPath(path).parts
    if parts[-1] == "AGENTS.md":
        return True
    if not path.endswith(".md") or parts[-1] == "README.md":
        return False
    in_docs = parts[0] == "docs" or (len(parts) > 2 and parts[1] == "docs")
    return in_docs or is_in_skills(path)


def is_in_skills(path):
    """Tells whether a repository-relative POSIX path is under the skills."""
    return path.startswith(SKILLS + "/")


def agent_documents(tracked):
    """Returns the agent documents among the tracked files."""
    return [path for path in tracked if is_agent_document(path)]


def read_text(root, path):
    """Returns the text of a file, or None when the file is gone from the disk."""
    try:
        return (pathlib.Path(root) / path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def unquoted(value):
    """Returns a YAML scalar without its enclosing quotes."""
    if len(value) > 1 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def front_matter(text):
    """Returns the key: value pairs of the leading front matter block, or None when it is absent."""
    lines = text.split("\n")
    if lines[0] != "---" or "---" not in lines[1:]:
        return None
    pairs = {}
    for line in lines[1 : lines.index("---", 1)]:
        key, separator, value = line.partition(":")
        if separator:
            pairs[key.strip()] = unquoted(value.strip())
    return pairs


def read_markdown(root, path):
    """Returns the text of a document, or None when the file is gone from the disk.

    A leading front matter block is blanked, so that it is not parsed as Markdown and the line
    numbers stay the same.
    """
    text = read_text(root, path)
    if text is None:
        return None
    lines = text.split("\n")
    if lines[0] == "---" and "---" in lines[1:]:
        closing = lines.index("---", 1)
        lines[: closing + 1] = [""] * (closing + 1)
    return "\n".join(lines)


def parse(text):
    """Parses CommonMark with tables. Returns (tokens, env), env holds the reference definitions."""
    env = {}
    tokens = MarkdownIt("commonmark").enable("table").parse(text, env)
    return tokens, env


def rendered(inline):
    """Returns the text a reader sees from an inline token."""
    parts = []
    for child in inline.children or []:
        if child.type in ("text", "code_inline"):
            parts.append(child.content)
        elif child.type == "image":
            parts.append(child.content)
        elif child.type in ("softbreak", "hardbreak"):
            parts.append(" ")
    return "".join(parts)
