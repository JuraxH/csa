import pytest
from .conftest import check_match

@pytest.mark.basic
class TestBasicMatching:
    def test_exact_match(self):
        check_match("abc", "abc", True)
        check_match("abc", "ab", False)

    def test_partial_match(self):
        # Unless anchored, matches anywhere in the string
        check_match("abc", "abcd", True)
        check_match("abc", "xabcd", True)

    def test_anchors(self):
        check_match("^abc$", "abc", True)
        check_match("^abc$", "abcd", False)
        check_match("^abc$", "xabc", False)
        check_match("^abc", "abcd", True)
        check_match("abc$", "xabc", True)

    def test_dot(self):
        check_match("a.c", "abc", True)
        check_match("a.c", "adc", True)
        check_match("a.c", "ac", False)

@pytest.mark.character_classes
class TestCharacterClasses:
    def test_simple_class(self):
        check_match("[abc]", "a", True)
        check_match("[abc]", "b", True)
        check_match("[abc]", "d", False)

    def test_negated_class(self):
        check_match("[^abc]", "d", True)
        check_match("[^abc]", "a", False)

    def test_ranges(self):
        check_match("[a-z]", "m", True)
        check_match("[a-z]", "A", False)
        check_match("[a-z0-9]", "5", True)

    def test_predefined_classes(self):
        check_match(r"\d", "5", True)
        check_match(r"\d", "a", False)
        check_match(r"\w", "a", True)
        check_match(r"\w", "_", True)
        check_match(r"\s", " ", True)
        check_match(r"\s", "a", False)

@pytest.mark.alternation
class TestAlternation:
    def test_simple_alternation(self):
        check_match("a|b", "a", True)
        check_match("a|b", "b", True)
        check_match("a|b", "c", False)

    def test_grouped_alternation(self):
        check_match("x(a|b)y", "xay", True)
        check_match("x(a|b)y", "xby", True)
        check_match("x(a|b)y", "xcy", False)

    def test_multiple_alternation(self):
        check_match("a|b|c", "a", True)
        check_match("a|b|c", "b", True)
        check_match("a|b|c", "c", True)
        check_match("a|b|c", "d", False)

@pytest.mark.repetition
class TestRepetition:
    def test_star(self):
        check_match("ab*c", "ac", True)
        check_match("ab*c", "abc", True)
        check_match("ab*c", "abbbc", True)

    def test_plus(self):
        check_match("ab+c", "ac", False)
        check_match("ab+c", "abc", True)
        check_match("ab+c", "abbbc", True)

    def test_question(self):
        check_match("ab?c", "ac", True)
        check_match("ab?c", "abc", True)
        check_match("ab?c", "abbc", False)

@pytest.mark.counters
class TestCounters:
    def test_exact_count(self):
        check_match("a{3}", "aaa", True)
        check_match("a{3}", "aa", False)
        check_match("^a{3}$", "aaaa", False)
        check_match("a{3}", "aaaa", True)

    def test_min_count(self):
        check_match("a{2,}", "a", False)
        check_match("a{2,}", "aa", True)
        check_match("a{2,}", "aaa", True)

    def test_range_count(self):
        check_match("^a{2,4}$", "a", False)
        check_match("^a{2,4}$", "aa", True)
        check_match("^a{2,4}$", "aaa", True)
        check_match("^a{2,4}$", "aaaa", True)
        check_match("^a{2,4}$", "aaaaa", False)

    def test_counters_with_groups(self):
        check_match("^(ab){2,3}$", "abab", True)
        check_match("^(ab){2,3}$", "ababab", True)
        check_match("^(ab){2,3}$", "ab", False)
        check_match("^(ab){2,3}$", "abababab", False)

    def test_overlapping_counters(self):
        check_match("a{1,2}a{1,2}", "a", False)
        check_match("a{1,2}a{1,2}", "aa", True)
        check_match("a{1,2}a{1,2}", "aaa", True)
        check_match("a{1,2}a{1,2}", "aaaa", True)

@pytest.mark.unicode
class TestUnicode:
    def test_unicode_characters(self):
        check_match("š", "š", True)
        check_match("š", "s", False)

    def test_unicode_counters(self):
        check_match("^š{2,3}$", "šš", True)
        check_match("^š{2,3}$", "ššš", True)
        check_match("^š{2,3}$", "š", False)
        check_match("^š{2,3}$", "šššš", False)

    def test_unicode_classes(self):
        check_match("^[α-ω]+$", "αβγ", True)
        check_match("^[α-ω]+$", "αβaγ", False)

@pytest.mark.complex
class TestComplex:
    def test_email_like(self):
        pattern = r"^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$"
        check_match(pattern, "test@example.com", True)
        check_match(pattern, "invalid-email", False)
        check_match(pattern, "a@b.co", True)

    def test_date_like(self):
        pattern = r"^\d{4}-\d{2}-\d{2}$"
        check_match(pattern, "2023-10-24", True)
        check_match(pattern, "2023-1-24", False)
        check_match(pattern, "2023-10-244", False)
