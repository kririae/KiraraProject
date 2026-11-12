import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / "verify-commit-message.py"
spec = importlib.util.spec_from_file_location("verify_commit_message", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

SUBJECT = "feat(flux): add a thing"


def problems(text):
    return [problem for _, problem in module.verify(text)]


class VerifyCommitMessageTest(unittest.TestCase):
    def test_subject_only_is_valid(self):
        self.assertEqual(module.verify(SUBJECT + "\n"), [])

    def test_subject_with_body_is_valid(self):
        self.assertEqual(module.verify(f"{SUBJECT}\n\nOne line.\nTwo lines.\nThree lines.\n"), [])

    def test_body_of_four_lines_is_rejected(self):
        self.assertEqual(
            module.verify(f"{SUBJECT}\n\na\nb\nc\nd\n"), [(6, "body is longer than 3 lines")]
        )

    def test_second_paragraph_is_rejected(self):
        self.assertEqual(
            problems(f"{SUBJECT}\n\nFirst.\n\nSecond.\n"), ["text after the body paragraph"]
        )

    def test_line_2_must_be_blank(self):
        self.assertEqual(problems(f"{SUBJECT}\nBody.\n"), ["line 2 must be blank"])
        self.assertEqual(problems(f"{SUBJECT}\n  \nBody.\n"), [])

    def test_semicolon_is_rejected(self):
        self.assertEqual(problems(f"{SUBJECT}\n\nOne; two.\n"), ["line contains a semicolon"])

    def test_line_over_100_characters_is_rejected(self):
        body = "a" * 100 + "\n" + "b" * 101
        self.assertEqual(
            module.verify(f"{SUBJECT}\n\n{body}\n"), [(4, "line is longer than 100 characters")]
        )

    def test_trailer_is_rejected(self):
        self.assertEqual(
            problems(f"{SUBJECT}\n\nCo-authored-by: A <a@b.c>\n"), ["trailers are not allowed"]
        )
        self.assertEqual(
            problems(f"{SUBJECT}\n\nBody.\nSigned-off-by: A\n"), ["trailers are not allowed"]
        )
        self.assertEqual(
            problems(f"{SUBJECT}\n\nBody.\n\nReviewed-by: A\nTested-by: B\n"),
            ["trailers are not allowed"],
        )

    def test_body_with_one_trailer_like_line_is_valid(self):
        self.assertEqual(module.verify(f"{SUBJECT}\n\nNote: this.\nMore text.\n"), [])

    def test_comments_and_scissors_are_ignored(self):
        self.assertEqual(module.verify(f"{SUBJECT}\n# a; comment\n\nBody.\n"), [])
        text = f"{SUBJECT}\n\n{module.SCISSORS}\nanything; goes\nTested-by: B\n"
        self.assertEqual(module.verify(text), [])

    def test_autosquash_subjects_are_exempt(self):
        for prefix in ("fixup! ", "squash! ", "amend! "):
            self.assertEqual(module.verify(f"{prefix}{SUBJECT}; x\n\nx\ny\nz\nw\n"), [])


if __name__ == "__main__":
    unittest.main()
