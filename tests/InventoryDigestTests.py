"""Check inventory fingerprints using real files with different checkout endings."""
import hashlib
from pathlib import Path
import runpy
import tempfile
import unittest


digest = runpy.run_path(str(Path(__file__).resolve().parent.parent /
                           "tools/inventory_lexer_baseline.py"))["digest"]


class InventoryDigestTests(unittest.TestCase):
    def fingerprint(self, contents):
        with tempfile.TemporaryDirectory(prefix="multipad-inventory-digest-") as directory:
            path = Path(directory) / "source.txt"
            path.write_bytes(contents)
            return digest(path)

    def test_lf_and_crlf_have_the_same_canonical_hash(self):
        canonical = b"first\nsecond\n"
        expected = hashlib.sha256(canonical).hexdigest()
        self.assertEqual(self.fingerprint(canonical), expected)
        self.assertEqual(self.fingerprint(b"first\r\nsecond\r\n"), expected)

    def test_bom_is_preserved(self):
        contents = b"\xef\xbb\xbffirst\nsecond\n"
        self.assertEqual(self.fingerprint(contents.replace(b"\n", b"\r\n")),
                         hashlib.sha256(contents).hexdigest())
        self.assertNotEqual(self.fingerprint(contents), self.fingerprint(contents[3:]))

    def test_lone_cr_and_non_text_bytes_are_preserved(self):
        contents = b"\xff\x80\0a\rb\r\r\nc\n"
        canonical = b"\xff\x80\0a\rb\r\nc\n"
        self.assertEqual(self.fingerprint(contents), hashlib.sha256(canonical).hexdigest())
        self.assertNotEqual(self.fingerprint(b"a\rb"), self.fingerprint(b"a\nb"))

    def test_content_and_final_newline_changes_are_detected(self):
        self.assertNotEqual(self.fingerprint(b"one\n"), self.fingerprint(b"two\n"))
        self.assertNotEqual(self.fingerprint(b"one\n"), self.fingerprint(b"one"))


if __name__ == "__main__":
    unittest.main()
