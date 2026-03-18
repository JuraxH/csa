# Rust Benchmark Implementation

This directory contains a small Rust library that uses the `regex` crate to benchmark counter/repetition matching performance against the main project.

## Building

To build the shared library used by the Python test suite, you must have Rust and Cargo installed.

Run the following command from this directory:

```bash
cargo build --release
```

This will produce a dynamic library in `target/release/` (e.g., `librust_benchmark.so` on Linux, `.dylib` on macOS, `.dll` on Windows).

## Running tests

Once built, the Python `pytest-benchmark` test suite automatically discovers this dynamic library.
Just run the performance tests from the repository root:

```bash
cd ..
pytest tests/test_performance.py
```
