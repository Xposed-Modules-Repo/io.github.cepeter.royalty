import importlib.util
import pathlib
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/compare_apk_payloads.py"


def load_module():
    spec = importlib.util.spec_from_file_location("compare_apk_payloads", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_zip(path, content, comment):
    with zipfile.ZipFile(path, "w") as archive:
        info = zipfile.ZipInfo("classes.dex", (2026, 1, 1, 0, 0, 0))
        archive.writestr(info, content)
        archive.comment = comment


class ReproducibleApkTests(unittest.TestCase):
    def test_ignores_archive_level_signing_bytes(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            first = pathlib.Path(directory) / "first.apk"
            second = pathlib.Path(directory) / "second.apk"
            write_zip(first, b"same payload", b"signature one")
            write_zip(second, b"same payload", b"signature two")
            self.assertEqual(module.compare_payloads(first, second), [("classes.dex", module.sha256(b"same payload"))])

    def test_rejects_changed_payload(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as directory:
            first = pathlib.Path(directory) / "first.apk"
            second = pathlib.Path(directory) / "second.apk"
            write_zip(first, b"first", b"")
            write_zip(second, b"second", b"")
            with self.assertRaisesRegex(ValueError, "classes.dex"):
                module.compare_payloads(first, second)


if __name__ == "__main__":
    unittest.main()
