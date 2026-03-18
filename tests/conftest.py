import pytest
import os
import subprocess
import ctypes
from pathlib import Path

# Try to use a ctypes extension if available for speed, fallback to a CLI tool or raise error
_lib = None
_rust_lib = None

def setup_library():
    global _lib, _rust_lib
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

        _lib.csa_compile.argtypes = [ctypes.c_char_p]
        _lib.csa_compile.restype = ctypes.c_void_p
        _lib.csa_free.argtypes = [ctypes.c_void_p]
        _lib.csa_free.restype = None
        _lib.csa_match_compiled.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        _lib.csa_match_compiled.restype = ctypes.c_int
    else:
        # We can implement a fallback using CLI if needed, but a ctypes library is preferred for speed
        raise RuntimeError(f"Could not find {lib_path}. Please build the test library first.")

    # Try to load the Rust library
    rust_lib_name = f"librust_benchmark{ext}"
    if sys.platform == "win32":
        rust_lib_name = f"rust_benchmark{ext}"

    rust_lib_path = Path(__file__).parent.parent / "rust_benchmark" / "target" / "release" / rust_lib_name
    if rust_lib_path.exists():
        _rust_lib = ctypes.CDLL(str(rust_lib_path))
        _rust_lib.rust_regex_match.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        _rust_lib.rust_regex_match.restype = ctypes.c_int

        _rust_lib.rust_regex_compile.argtypes = [ctypes.c_char_p]
        _rust_lib.rust_regex_compile.restype = ctypes.c_void_p
        _rust_lib.rust_regex_free.argtypes = [ctypes.c_void_p]
        _rust_lib.rust_regex_free.restype = None
        _rust_lib.rust_regex_match_compiled.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        _rust_lib.rust_regex_match_compiled.restype = ctypes.c_int

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
