import pytest
from .conftest import setup_library

@pytest.fixture(scope="session", autouse=True)
def init_library():
    setup_library()

import ctypes

class Engine:
    def compile(self, pattern: str):
        raise NotImplementedError
    def match(self, text: str) -> bool:
        raise NotImplementedError
    def free(self):
        raise NotImplementedError

class CSAEngine(Engine):
    def __init__(self):
        from . import conftest
        self.lib = conftest._lib
        self.ptr = None
        self.pattern = None

    def compile(self, pattern: str):
        self.pattern = pattern
        pattern_bytes = pattern.encode('utf-8')
        self.ptr = self.lib.csa_compile(pattern_bytes)
        if not self.ptr:
            raise ValueError(f"CSA regex error compiling pattern: {pattern}")

    def match(self, text: str) -> bool:
        text_bytes = text.encode('utf-8')
        result = self.lib.csa_match_compiled(self.ptr, text_bytes)
        if result < 0:
            raise ValueError(f"CSA match error on pattern: {self.pattern}")
        return bool(result)

    def free(self):
        if self.ptr:
            self.lib.csa_free(self.ptr)
            self.ptr = None

class RustEngine(Engine):
    def __init__(self):
        from . import conftest
        if conftest._rust_lib is None:
            pytest.skip("Rust library not built/found")
        self.lib = conftest._rust_lib
        self.ptr = None
        self.pattern = None

    def compile(self, pattern: str):
        self.pattern = pattern
        pattern_bytes = pattern.encode('utf-8')
        self.ptr = self.lib.rust_regex_compile(pattern_bytes)
        if not self.ptr:
            raise ValueError(f"Rust regex error compiling pattern: {pattern}")

    def match(self, text: str) -> bool:
        text_bytes = text.encode('utf-8')
        result = self.lib.rust_regex_match_compiled(self.ptr, text_bytes)
        if result < 0:
            raise ValueError(f"Rust match error on pattern: {self.pattern}")
        return bool(result)

    def free(self):
        if self.ptr:
            self.lib.rust_regex_free(self.ptr)
            self.ptr = None

@pytest.fixture(params=["csa", "rust"])
def engine(request):
    if request.param == "csa":
        eng = CSAEngine()
    else:
        eng = RustEngine()
    yield eng
    eng.free()


def run_benchmark(benchmark, engine, pattern, text):
    engine.compile(pattern)
    return benchmark(engine.match, text)

@pytest.mark.benchmark
class TestPerformance:
    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_exact(self, benchmark, length, engine):
        # Match a long string with exact bounded repetition
        pattern = f"^a{{{length}}}$"
        text = "a" * length
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_range(self, benchmark, length, engine):
        # e.g., a{half_length,length} on a string of `length` 'a's
        half = length // 2
        pattern = f"^a{{{half},{length}}}$"
        text = "a" * length
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_mismatch(self, benchmark, length, engine):
        # Mismatch after a long repetition
        pattern = f"^a{{{length}}}b$"
        text = "a" * length + "c"
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is False

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_nested(self, benchmark, length, engine):
        # Nested structures (if supported by library) or complex groupings
        pattern = f"^(ab){{{length}}}$"
        text = "ab" * length
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 500]) # 10000 is probably too much memory for overlapping optional
    def test_perf_overlapping_counters(self, benchmark, length, engine):
        # Overlapping optional counters
        pattern = f"^a{{0,{length}}}a{{0,{length}}}$"
        text = "a" * (length * 2)
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_large_range(self, benchmark, length, engine):
        # Large repetition range, to see if state explosion happens
        upper = int(length * 1.5)
        pattern = f"^a{{0,{upper}}}$"
        text = "a" * length
        result = run_benchmark(benchmark, engine, pattern, text)
        assert result is True
