"""Shared Enforce source inspection for offline contract tests."""
import re


def mask_literals(source):
    return re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        source,
    )


RENAMED_METHODS = {
    "override protected void TickProximityCheck(": "bool INF_StepProximity(",
    "protected void CaptureTick(": "protected INF_BlockPresence StepCapture(",
}


def method_body(source, signature, strip_literals=False):
    # The proximity and capture ticks were folded into the shared zone loop but keep their bodies.
    signature = RENAMED_METHODS.get(signature, signature)
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
