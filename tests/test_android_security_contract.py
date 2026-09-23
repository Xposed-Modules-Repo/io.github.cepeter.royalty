import pathlib
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"


class CatalogSecurityContractTests(unittest.TestCase):
    def test_exported_service_authenticates_binder_uid(self):
        manifest = ET.parse(ROOT / "app/src/main/AndroidManifest.xml").getroot()
        application = manifest.find("application")
        if application is None:
            self.fail("application missing")
        service = next(
            node
            for node in application.findall("service")
            if node.attrib.get(ANDROID_NS + "name") == ".catalog.CatalogService"
        )
        self.assertEqual("true", service.attrib[ANDROID_NS + "exported"])

        source = (ROOT / "app/src/main/java/io/github/cepeter/telegramhider/catalog/CatalogService.java").read_text()
        self.assertIn("Binder.getCallingUid()", source)
        self.assertIn('"org.telegram.messenger"', source)
        self.assertIn("SecurityException", source)

    def test_config_uses_framework_redirected_shared_preferences(self):
        source = (ROOT / "app/src/main/java/io/github/cepeter/telegramhider/config/ConfigStore.java").read_text()
        self.assertIn("Context.MODE_WORLD_READABLE", source)
        self.assertIn("putStringSet", source)
        self.assertIn("commit()", source)

    def test_aidl_surface_is_bounded(self):
        aidl = (ROOT / "app/src/main/aidl/io/github/cepeter/telegramhider/ICatalogService.aidl").read_text()
        self.assertIn("void submit(int account, in long[] ids, in String[] titles);", aidl)
        self.assertIn("void reportStatus(String hook, String status, String detail);", aidl)


if __name__ == "__main__":
    unittest.main()
