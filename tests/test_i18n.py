import unittest
from unittest.mock import patch
from i18n import language, translate


class SystemLanguageTests(unittest.TestCase):
    def test_lc_all_takes_precedence(self):
        with patch.dict("os.environ", {"LC_ALL": "en_US.UTF-8", "LC_MESSAGES": "es_ES.UTF-8", "LANG": "es_MX.UTF-8"}):
            self.assertEqual(language(), "en")
            self.assertEqual(translate("Inicio"), "Home")

    def test_message_language_overrides_default(self):
        with patch.dict("os.environ", {"LC_ALL": "", "LC_MESSAGES": "es_MX.UTF-8", "LANG": "en_US.UTF-8"}):
            self.assertEqual(translate("Inicio"), "Inicio")

    def test_spanish_regional_variant(self):
        with patch.dict("os.environ", {"LC_ALL": "es-AR", "LC_MESSAGES": "", "LANG": "en_US.UTF-8"}):
            self.assertEqual(language(), "es")

    def test_c_locale_uses_english(self):
        with patch.dict("os.environ", {"LC_ALL": "C.UTF-8"}):
            self.assertEqual(translate("Temas"), "Themes")

    def test_unknown_authored_text_is_preserved(self):
        with patch.dict("os.environ", {"LC_ALL": "en_US.UTF-8"}):
            self.assertEqual(translate("Mi tema personal"), "Mi tema personal")
            self.assertEqual(translate("Falta %1. Dependencias: %2", "cmake", "packages"), "Missing cmake. Dependencies: packages")


if __name__ == "__main__":
    unittest.main()
