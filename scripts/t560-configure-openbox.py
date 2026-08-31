#!/usr/bin/python3

"""Route the physical Home keys through the screen-aware button handler."""

import os
import re
import shutil
import stat
import sys
import xml.etree.ElementTree as element_tree


HOME_KEYS = ("Home", "XF86HomePage")
HOME_COMMAND = "/home/vahac/.local/bin/t560-home-button"
KEYBIND_PATTERN = re.compile(
    r"^(?P<indent>[ \t]*)<keybind\b(?P<attributes>[^>]*)>.*?"
    r"</keybind>[ \t]*(?=\r?$)",
    re.MULTILINE | re.DOTALL,
)
KEY_ATTRIBUTE_PATTERN = re.compile(r"\bkey\s*=\s*(['\"])(?P<key>.*?)\1")


def keybinding(key, indent="  "):
    child_indent = indent + "  "
    return (
        f'{indent}<keybind key="{key}">\n'
        f'{child_indent}<action name="Execute">\n'
        f"{child_indent}  <command>{HOME_COMMAND}</command>\n"
        f"{child_indent}</action>\n"
        f"{indent}</keybind>"
    )


def configure(contents):
    found = set()

    def replace_binding(match):
        key_match = KEY_ATTRIBUTE_PATTERN.search(match.group("attributes"))
        if not key_match or key_match.group("key") not in HOME_KEYS:
            return match.group(0)

        key = key_match.group("key")
        found.add(key)
        return keybinding(key, match.group("indent"))

    configured = KEYBIND_PATTERN.sub(replace_binding, contents)
    missing = [key for key in HOME_KEYS if key not in found]
    if missing:
        closing_keyboard = configured.find("</keyboard>")
        if closing_keyboard < 0:
            raise ValueError("Openbox configuration has no </keyboard> element")
        addition = "\n".join(keybinding(key) for key in missing) + "\n"
        configured = (
            configured[:closing_keyboard] + addition + configured[closing_keyboard:]
        )

    element_tree.fromstring(configured)
    return configured


def main():
    path = os.path.expanduser(
        sys.argv[1] if len(sys.argv) > 1 else "~/.config/openbox/rc.xml"
    )
    with open(path, "r", encoding="utf-8") as source:
        contents = source.read()

    configured = configure(contents)
    if configured == contents:
        print(f"Openbox Home bindings already configured: {path}")
        return

    backup = path + ".before-t560-home-button"
    if not os.path.exists(backup):
        shutil.copy2(path, backup)

    mode = stat.S_IMODE(os.stat(path).st_mode)
    temporary = path + ".t560-new"
    with open(temporary, "w", encoding="utf-8", newline="") as destination:
        destination.write(configured)
    os.chmod(temporary, mode)
    os.replace(temporary, path)
    print(f"Configured Openbox Home bindings: {path}")


if __name__ == "__main__":
    main()
