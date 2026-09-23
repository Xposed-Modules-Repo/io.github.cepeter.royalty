import pathlib
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"


class AndroidPackageContractTests(unittest.TestCase):
    def test_gradle_project_is_pinned_for_android(self):
        settings = (ROOT / "settings.gradle.kts").read_text()
        app_gradle = (ROOT / "app/build.gradle.kts").read_text()
        self.assertIn('com.android.application") version "8.7.3"', settings)
        self.assertIn("compileSdk = 35", app_gradle)
        self.assertIn("minSdk = 27", app_gradle)
        self.assertIn("targetSdk = 35", app_gradle)
        self.assertIn('versionName = "2.0.0"', app_gradle)
        self.assertIn("versionCode = 7", app_gradle)

    def test_xposed_api_is_compile_only(self):
        app_gradle = (ROOT / "app/build.gradle.kts").read_text()
        self.assertIn('compileOnly("de.robv.android.xposed:api:82")', app_gradle)
        self.assertNotIn('implementation("de.robv.android.xposed:api:', app_gradle)

    def test_manifest_declares_vector_compatible_legacy_module(self):
        manifest_path = ROOT / "app/src/main/AndroidManifest.xml"
        root = ET.parse(manifest_path).getroot()
        application = root.find("application")
        if application is None:
            self.fail("manifest must contain an application element")
        metadata = {
            node.attrib[ANDROID_NS + "name"]: node.attrib.get(ANDROID_NS + "value", "")
            for node in application.findall("meta-data")
        }
        self.assertEqual("true", metadata["xposedmodule"])
        self.assertEqual("93", metadata["xposedminversion"])
        self.assertEqual("true", metadata["xposedsharedprefs"])
        self.assertEqual("org.telegram.messenger", metadata["xposedscope"])

    def test_entrypoint_asset_is_exact(self):
        entrypoint = (ROOT / "app/src/main/assets/xposed_init").read_text()
        self.assertEqual(
            "io.github.cepeter.telegramhider.xposed.TelegramHook\n",
            entrypoint,
        )

    def test_aidl_generation_is_enabled(self):
        app_gradle = (ROOT / "app/build.gradle.kts").read_text()
        self.assertIn("aidl = true", app_gradle)


if __name__ == "__main__":
    unittest.main()
