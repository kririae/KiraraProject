#!/usr/bin/env python3
"""Verifies the shape of a commit message.

Line 1 is the subject. Anything more is a blank line and then one paragraph of one to three
lines. No line holds a semicolon or more than 100 characters. There are no trailers.

Lines starting with `#` are git comments, and so is everything from the scissors line on.
A subject starting with `fixup! `, `squash! ` or `amend! ` is exempt, as for
conventional-pre-commit.

Usage: verify-commit-message.py <message-file>
"""

import re
import sys

MAX_BODY_LINES = 3
MAX_LINE_LENGTH = 100
SCISSORS = "# ------------------------ >8 ------------------------"
EXEMPT_PREFIXES = ("fixup! ", "squash! ", "amend! ")
TRAILER = re.compile(r"^[A-Za-z][A-Za-z0-9-]*:\s+\S")
KNOWN_TRAILER = re.compile(r"^(co-authored-by|signed-off-by):", re.IGNORECASE)


def meaningful_lines(text):
    """Returns (line number, line) pairs without comments."""
    lines = []
    for number, line in enumerate(text.splitlines(), start=1):
        if line == SCISSORS:
            break
        if not line.startswith("#"):
            lines.append((number, line))
    return lines


def paragraphs(lines):
    """Splits lines into paragraphs separated by blank lines."""
    result = []
    current = []
    for number, line in lines:
        if line.strip():
            current.append((number, line))
        elif current:
            result.append(current)
            current = []
    if current:
        result.append(current)
    return result


def verify(text):
    """Returns (line number, problem) pairs for a commit message."""
    lines = meaningful_lines(text)
    if not lines or lines[0][1].startswith(EXEMPT_PREFIXES):
        return []

    problems = []
    for number, line in lines:
        if ";" in line:
            problems.append((number, "line contains a semicolon"))
        if len(line) > MAX_LINE_LENGTH:
            problems.append((number, f"line is longer than {MAX_LINE_LENGTH} characters"))

    if len(lines) > 1 and lines[1][1].strip():
        problems.append((lines[1][0], "line 2 must be blank"))

    for index, paragraph in enumerate(paragraphs(lines[1:])):
        is_trailer = all(TRAILER.match(line) for _, line in paragraph)
        if is_trailer or any(KNOWN_TRAILER.match(line) for _, line in paragraph):
            problems.append((paragraph[0][0], "trailers are not allowed"))
        elif index > 0:
            problems.append((paragraph[0][0], "text after the body paragraph"))
        elif len(paragraph) > MAX_BODY_LINES:
            problems.append(
                (paragraph[MAX_BODY_LINES][0], f"body is longer than {MAX_BODY_LINES} lines")
            )
    return sorted(problems)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8", errors="replace") as file:
        problems = verify(file.read())
    for number, problem in problems:
        print(f"{argv[1]}:{number}: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
