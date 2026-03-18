import pytest
from .conftest import setup_library

@pytest.fixture(scope="session", autouse=True)
def init_library():
    setup_library()

def raw_match(pattern: str, text: str) -> bool:
    from . import conftest
    pattern_bytes = pattern.encode('utf-8')
    text_bytes = text.encode('utf-8')
    result = conftest._lib.csa_match(pattern_bytes, text_bytes)
    if result < 0:
        raise ValueError(f"Error compiling pattern: {pattern}")
    return bool(result)

@pytest.mark.benchmark
class TestPerformance:
    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_exact(self, benchmark, length):
        # Match a long string with exact bounded repetition
        pattern = f"^a{{{length}}}$"
        text = "a" * length
        result = benchmark(raw_match, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_range(self, benchmark, length):
        # e.g., a{half_length,length} on a string of `length` 'a's
        half = length // 2
        pattern = f"^a{{{half},{length}}}$"
        text = "a" * length
        result = benchmark(raw_match, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_mismatch(self, benchmark, length):
        # Mismatch after a long repetition
        pattern = f"^a{{{length}}}b$"
        text = "a" * length + "c"
        result = benchmark(raw_match, pattern, text)
        assert result is False

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_counters_nested(self, benchmark, length):
        # Nested structures (if supported by library) or complex groupings
        pattern = f"^(ab){{{length}}}$"
        text = "ab" * length
        result = benchmark(raw_match, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 500]) # 10000 is probably too much memory for overlapping optional
    def test_perf_overlapping_counters(self, benchmark, length):
        # Overlapping optional counters
        pattern = f"^a{{0,{length}}}a{{0,{length}}}$"
        text = "a" * (length * 2)
        result = benchmark(raw_match, pattern, text)
        assert result is True

    @pytest.mark.parametrize("length", [10, 100, 1000, 10000])
    def test_perf_large_range(self, benchmark, length):
        # Large repetition range, to see if state explosion happens
        upper = int(length * 1.5)
        pattern = f"^a{{0,{upper}}}$"
        text = "a" * length
        result = benchmark(raw_match, pattern, text)
        assert result is True
