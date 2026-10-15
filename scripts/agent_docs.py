"""Shared by the agent document verifiers: which files are agent documents, and how to parse them.

An agent document is a tracked file that is `AGENTS.md` in any directory, a Markdown file under a
`docs` directory at the root or in a subproject, or a Markdown file under `.agents/skills`.
README files are not agent documents.
"""

import pathlib
import subprocess

from markdown_it import MarkdownIt

ROOT_DOCUMENT = "AGENTS.md"


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
    return in_docs or parts[:2] == (".agents", "skills")


def agent_documents(tracked):
    """Returns the agent documents among the tracked files."""
    return [path for path in tracked if is_agent_document(path)]


def read_markdown(root, path):
    """Returns the text of a document, or None when the file is gone from the disk.

    A leading front matter block is blanked, so that it is not parsed as Markdown and the line
    numbers stay the same.
    """
    try:
        text = (pathlib.Path(root) / path).read_text(encoding="utf-8", errors="replace")
    except OSError:
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
