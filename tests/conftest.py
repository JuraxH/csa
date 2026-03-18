import pytest
import os
import subprocess
import ctypes
from pathlib import Path

# Try to use a ctypes extension if available for speed, fallback to a CLI tool or raise error
_lib = None

def setup_library():
    global _lib
    if _lib is not None:
        return

    import sys

    ext = ".so"
    if sys.platform == "darwin":
        ext = ".dylib"
    elif sys.platform == "win32":
        ext = ".dll"

    lib_path = Path(__file__).parent.parent / "build" / f"libcsa_test{ext}"
    if lib_path.exists():
        _lib = ctypes.CDLL(str(lib_path))
        _lib.csa_match.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        _lib.csa_match.restype = ctypes.c_int
    else:
        # We can implement a fallback using CLI if needed, but a ctypes library is preferred for speed
        raise RuntimeError(f"Could not find {lib_path}. Please build the test library first.")

def check_match(pattern: str, text: str, expected_result: bool):
    """
    Implementation-agnostic check function.
    Tests if `text` matches `pattern`.
    """
    setup_library()

    # encode to bytes for C++ interface
    pattern_bytes = pattern.encode('utf-8')
    text_bytes = text.encode('utf-8')

    result = _lib.csa_match(pattern_bytes, text_bytes)

    if result < 0:
        raise ValueError(f"Error compiling pattern: {pattern}")

    actual_result = bool(result)
    assert actual_result == expected_result, f"Expected {expected_result} for pattern '{pattern}' on text '{text}', but got {actual_result}"
