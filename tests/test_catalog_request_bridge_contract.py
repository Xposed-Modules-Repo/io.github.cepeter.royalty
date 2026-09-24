import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "app/src/main/java/io/github/cepeter/telegramhider/xposed/CatalogRequestBridge.java"


class CatalogRequestBridgeContractTests(unittest.TestCase):
    def setUp(self):
        self.source = BRIDGE.read_text()

    def test_authenticates_callback_creator_and_registers_exported_receiver(self):
        self.assertIn("getCreatorPackage()", self.source)
        self.assertIn("CatalogProtocol.MODULE_PACKAGE.equals", self.source)
        self.assertIn("Context.RECEIVER_EXPORTED", self.source)
        self.assertIn("CatalogProtocol.ACTION_REQUEST", self.source)

    def test_sends_bounded_account_status_and_completion_frames(self):
        self.assertIn("snapshot.accounts().entrySet()", self.source)
        self.assertIn("CatalogProtocol.TYPE_ACCOUNT", self.source)
        self.assertIn("CatalogProtocol.TYPE_STATUS", self.source)
        self.assertIn("CatalogProtocol.TYPE_COMPLETE", self.source)
        self.assertIn("PendingIntent.CanceledException", self.source)

    def test_does_not_use_package_visibility_sensitive_binding(self):
        self.assertNotIn("bindService", self.source)
        self.assertNotIn("ComponentName", self.source)


if __name__ == "__main__":
    unittest.main()
