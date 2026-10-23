"""Shared Enforce source inspection for offline contract tests."""
import re


def mask_literals(source):
    return re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        source,
    )


def method_body(source, signature, strip_literals=False):
    start = source.index("{", source.index(signature))
    tail = source[start:]
    masked = mask_literals(tail)
    depth = 0
    for index, char in enumerate(masked):
        depth += (char == "{") - (char == "}")
        if depth == 0:
            result = tail[1:index]
            if strip_literals:
                return re.sub(
                    r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                    '""',
                    result,
                )
            return result
    raise AssertionError("Unbalanced method: " + signature)
