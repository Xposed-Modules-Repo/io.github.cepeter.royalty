import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "zygisk/src/telegram_hider.c").read_text()
MODULE_HEADER = (ROOT / "zygisk/src/module.h").read_text()


class LifecycleContractTests(unittest.TestCase):
    def test_registers_before_module_directory_access(self):
        entry = SOURCE[SOURCE.index("void zygisk_module_entry"):]
        self.assertLess(entry.index("register_module"), entry.index("resolve_module_dir"))
        resolver = SOURCE[SOURCE.index("static void resolve_module_dir"):SOURCE.index("void zygisk_module_entry")]
        self.assertIn("get_module_dir(api->impl)", resolver)

    def test_target_process_is_selected_from_specialize_args(self):
        pre = SOURCE[SOURCE.index("my_pre_app_specialize"):SOURCE.index("my_post_app_specialize")]
        self.assertIn("app_specialize_args_v5", pre)
        self.assertIn("nice_name", pre)
        self.assertIn("tch_is_target_process", pre)
        self.assertNotIn("/proc/self/cmdline", SOURCE)

    def test_legacy_art_hooks_are_disabled_by_default(self):
        self.assertIn("experimental_art_hooks", SOURCE)
        self.assertRegex(
            SOURCE,
            re.compile(
                r"if\s*\(\s*!g_cfg\.experimental_art_hooks\s*\).*?return\s*;.*?register_telegram_hooks",
                re.DOTALL,
            ),
        )

    def test_meowzygisk_v5_api_declarations_match_upstream(self):
        self.assertIn("struct app_specialize_args_v5", MODULE_HEADER)
        self.assertIn("uint32_t (*get_flags)(void);", MODULE_HEADER)
        self.assertIn("int (*get_module_dir)(void *);", MODULE_HEADER)


class SocketAndWebUIContractTests(unittest.TestCase):
    def setUp(self):
        self.webui = (ROOT / "module/webroot/js/app.js").read_text()

    def test_socket_is_bounded_and_sends_immediately(self):
        self.assertIn("listen(srv, 8)", SOURCE)
        self.assertIn("SO_RCVTIMEO", SOURCE)
        self.assertIn("SO_SNDTIMEO", SOURCE)
        self.assertIn("CATALOG_MAX", SOURCE)
        self.assertIn("tch_send_all", SOURCE)
        server = SOURCE[SOURCE.index("socket_server_thread"):SOURCE.index("start_socket_server")]
        self.assertNotIn("read(client", server)

    def test_webui_caps_catalog_at_50_kib(self):
        self.assertIn("head -c 51200", self.webui)
        self.assertNotIn("head -c 1048576", self.webui)

    def test_webui_saves_atomically_and_verifies_readback(self):
        self.assertIn("umask 077", self.webui)
        self.assertIn(".tmp.'$$", self.webui)
        self.assertIn("mv -f", self.webui)
        self.assertIn("stdout === json", self.webui)


if __name__ == "__main__":
    unittest.main()
