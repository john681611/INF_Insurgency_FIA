"""Project-wide structural checks, not a substitute for the Enforce compiler."""
from pathlib import Path
from collections import Counter
import re
import unittest

from source_contracts import mask_literals, method_body


SCRIPTS = Path(__file__).resolve().parents[1] / "Scripts" / "Game"
METHOD = re.compile(
    r"^[ \t]*(?:(?:override|protected|private|static|proto|native|event)\s+)*"
    r"\w+(?:<[^>\n]+>)?\s+(\w+)\s*\([^;{}]*\)\s*\{",
    re.MULTILINE,
)


class EnforceStructure(unittest.TestCase):
    def test_capture_tick_does_not_shadow_method_locals_in_nested_blocks(self):
        source = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
        body = mask_literals(method_body(source, "protected void CaptureTick("))
        declarations = re.findall(
            r"^[ \t]*(?:array<[^>\n]+>|bool|int|float|vector|INF_BlockPresence)\s+(\w+)\b",
            body, re.MULTILINE)
        repeated = [name for name, count in Counter(declarations).items() if count > 1]
        self.assertEqual(repeated, [], "Enforce rejects nested locals shadowing method locals")

    def test_all_script_delimiters_balance(self):
        opening = {"}": "{", ")": "(", "]": "["}
        for path in sorted(SCRIPTS.glob("*.c")):
            with self.subTest(script=path.name):
                stack = []
                for char in mask_literals(path.read_text()):
                    if char in "{([":
                        stack.append(char)
                    elif char in "})]":
                        self.assertTrue(stack, f"{path.name}: unexpected {char}")
                        self.assertEqual(stack.pop(), opening[char], path.name)
                self.assertEqual(stack, [], path.name)

    def test_methods_are_declared_at_class_scope(self):
        checked = 0
        for path in sorted(SCRIPTS.glob("*.c")):
            source = mask_literals(path.read_text())
            for method in METHOD.finditer(source):
                if method.group(1) in ("if", "for", "foreach", "while", "switch"):
                    continue
                with self.subTest(script=path.name, method=method.group(1)):
                    preceding = source[:method.start()]
                    depth = preceding.count("{") - preceding.count("}")
                    self.assertEqual(depth, 1, "Method must not be nested in another method")
                    checked += 1
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
