#!/usr/bin/env python3
"""Verifies the links of the agent documents.

A link, an image or a reference definition must resolve to a tracked file or to a directory that
holds tracked files. A fragment on a Markdown target must name a heading of it. Scheme URLs,
`//host` and `/absolute` targets are skipped. Every agent document must be reachable from the
root AGENTS.md by following links to agent documents. A link to a directory reaches nothing.

Usage: verify-md-links.py [root]
"""

import pathlib
import posixpath
import re
import sys
import unicodedata
from urllib.parse import unquote

import agent_docs

SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


def is_skipped(url):
    """Tells whether a target is outside the checks: a scheme URL, `//host` or `/absolute`."""
    return url.startswith("/") or SCHEME.match(url) is not None


def slug(heading):
    """Returns the GitHub anchor of the first heading with this rendered text."""
    kept = (c for c in heading.lower() if unicodedata.category(c)[0] in "LN" or c in "_ -")
    return "".join(kept).replace(" ", "-")


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


def anchors(text):
    """Returns the heading anchors of a document, with -1, -2 for repeated headings."""
    tokens, _ = agent_docs.parse(text)
    result = set()
    occurrences = {}
    for index, token in enumerate(tokens):
        if token.type != "heading_open":
            continue
        base = slug(rendered(tokens[index + 1]))
        name = base
        bump = occurrences.get(base, 0)
        while name in result:
            bump += 1
            name = f"{base}-{bump}"
        occurrences[base] = bump
        result.add(name)
    return result


def links(text):
    """Returns (line number, url) pairs for links, images and reference definitions."""
    tokens, env = agent_docs.parse(text)
    found = []
    for token in tokens:
        if token.type != "inline":
            continue
        line = token.map[0] + 1
        for child in token.children:
            if child.type in ("softbreak", "hardbreak"):
                line += 1
            elif child.type == "link_open":
                found.append((line, child.attrs["href"]))
            elif child.type == "image":
                found.append((line, child.attrs["src"]))
    for reference in env.get("references", {}).values():
        found.append((reference["map"][0] + 1, reference["href"]))
    return sorted(found)


def split(url):
    """Returns the decoded path and the decoded fragment (None when absent) of a target."""
    path_end = min((i for i in (url.find("#"), url.find("?")) if i >= 0), default=len(url))
    fragment = None
    if "#" in url:
        fragment = unquote(url.split("#", 1)[1].split("?", 1)[0])
    return unquote(url[:path_end]), fragment


def resolve(source, path):
    """Returns the repository-relative target of a link."""
    if path == "":
        return source
    target = posixpath.normpath(posixpath.join(posixpath.dirname(source), path))
    return "" if target == "." else target


def verify(root, tracked, documents):
    """Returns (path, line, url, problem) tuples for the broken links and unreachable documents."""
    tracked_set = set(tracked)
    anchor_cache = {}
    problems = []
    edges = {}

    def anchors_of(path):
        if path not in anchor_cache:
            anchor_cache[path] = anchors(agent_docs.read_markdown(root, path) or "")
        return anchor_cache[path]

    def is_directory(path):
        return path == "" or any(t.startswith(path + "/") for t in tracked)

    for source in documents:
        text = agent_docs.read_markdown(root, source)
        edges[source] = set()
        if text is None:
            continue
        for line, url in links(text):
            if is_skipped(url):
                continue
            path, fragment = split(url)
            target = resolve(source, path)
            if not (target in tracked_set or is_directory(target)):
                problems.append((source, line, url, "target is not a tracked file or directory"))
                continue
            if target in tracked_set:
                edges[source].add(target)
                if fragment is not None and target.endswith(".md"):
                    if fragment not in anchors_of(target):
                        problems.append((source, line, url, "no such anchor in the target"))

    reached = set()
    pending = [agent_docs.ROOT_DOCUMENT] if agent_docs.ROOT_DOCUMENT in edges else []
    while pending:
        document = pending.pop()
        if document not in reached:
            reached.add(document)
            pending.extend(t for t in edges.get(document, ()) if t in edges)
    for document in documents:
        if document not in reached:
            problems.append((document, 1, document, "not reachable from the root AGENTS.md"))
    return problems


def main(argv):
    if len(argv) > 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = pathlib.Path(argv[1] if len(argv) == 2 else ".")
    tracked = agent_docs.tracked_files(root)
    problems = verify(root, tracked, agent_docs.agent_documents(tracked))
    for path, line, url, problem in problems:
        print(f"{path}:{line}: {url}: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
