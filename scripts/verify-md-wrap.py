#!/usr/bin/env python3
"""Verifies that every paragraph of an agent document is one physical line.

Headings, code blocks, tables and HTML blocks are not paragraphs. Paragraphs in list items and
block quotes are. A paragraph has no length limit.

Usage: verify-md-wrap.py [root]
"""

import pathlib
import sys

import agent_docs


def verify(text):
    """Returns (line number, problem) pairs for the multi-line paragraphs of a document."""
    tokens, _ = agent_docs.parse(text)
    return [
        (token.map[0] + 1, "paragraph spans several lines, write it as one line")
        for token in tokens
        if token.type == "paragraph_open" and token.map[1] - token.map[0] > 1
    ]


def main(argv):
    if len(argv) > 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = pathlib.Path(argv[1] if len(argv) == 2 else ".")
    failed = False
    for path in agent_docs.agent_documents(agent_docs.tracked_files(root)):
        text = agent_docs.read_markdown(root, path)
        if text is None:
            continue
        for number, problem in verify(text):
            print(f"{path}:{number}: {problem}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
