import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as element_tree


SCRIPT = Path(__file__).parents[1] / "scripts" / "t560-configure-openbox.py"
SPEC = importlib.util.spec_from_file_location("configure_openbox", SCRIPT)
CONFIGURE_OPENBOX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONFIGURE_OPENBOX)


class ConfigureOpenboxTest(unittest.TestCase):
    def test_replaces_existing_home_bindings(self):
        source = """<?xml version="1.0"?>
<openbox_config>
<keyboard>
  <keybind key="Home">
    <action name="ToggleShowDesktop" />
  </keybind>
  <keybind key="XF86HomePage">
    <action name="Execute"><command>old-command</command></action>
  </keybind>
</keyboard>
</openbox_config>
"""

        configured = CONFIGURE_OPENBOX.configure(source)

        self.assertEqual(configured.count(CONFIGURE_OPENBOX.HOME_COMMAND), 2)
        self.assertNotIn("ToggleShowDesktop", configured)
        self.assertNotIn("old-command", configured)
        element_tree.fromstring(configured)

    def test_adds_missing_home_bindings_and_is_idempotent(self):
        source = """<openbox_config>
<keyboard>
  <keybind key="A-F4"><action name="Close" /></keybind>
</keyboard>
</openbox_config>
"""

        configured = CONFIGURE_OPENBOX.configure(source)

        self.assertEqual(configured.count(CONFIGURE_OPENBOX.HOME_COMMAND), 2)
        self.assertIn('keybind key="A-F4"', configured)
        self.assertEqual(CONFIGURE_OPENBOX.configure(configured), configured)


if __name__ == "__main__":
    unittest.main()
