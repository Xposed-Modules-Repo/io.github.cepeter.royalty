import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIVITY = ROOT / "app/src/main/java/io/github/cepeter/telegramhider/MainActivity.java"


class ConfigurationUiContractTests(unittest.TestCase):
    def setUp(self):
        self.source = ACTIVITY.read_text()

    def test_ui_shows_hook_status_and_multiple_choice_catalog(self):
        self.assertIn("loadHookStatuses", self.source)
        self.assertIn("CHOICE_MODE_MULTIPLE", self.source)
        self.assertIn("loadCatalog", self.source)
        self.assertIn("Account ", self.source)

    def test_ui_saves_hidden_keys_and_notification_setting(self):
        self.assertIn("ConfigStore.save", self.source)
        self.assertIn("setSuppressNotifications", self.source)
        self.assertIn("SecurityException", self.source)
        self.assertIn("Changes saved", self.source)

    def test_ui_explains_supported_scope_and_refresh(self):
        strings = (ROOT / "app/src/main/res/values/strings.xml").read_text()
        self.assertIn("Main dialog list", strings)
        self.assertIn("new-message notifications", strings)
        self.assertIn("Search, share picker, and new-group screens are not hidden", strings)
        self.assertIn("Open Telegram", strings)

    def test_missing_selected_dialogs_remain_manageable(self):
        self.assertIn("addMissingSelections", self.source)
        self.assertIn("Unavailable from current catalog", self.source)


if __name__ == "__main__":
    unittest.main()
