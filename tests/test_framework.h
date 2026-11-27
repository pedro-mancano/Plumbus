#ifndef PLUMBUS_TEST_FRAMEWORK_H
#define PLUMBUS_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>

static int g_tf_total = 0;
static int g_tf_failed = 0;
static int g_tf_cur_failed = 0;

#define TEST_CASE(name) static void name(void)

#define RUN_TEST(fn) do { \
    g_tf_total++; \
    g_tf_cur_failed = 0; \
    fn(); \
    if (g_tf_cur_failed) { \
        g_tf_failed++; \
        fwprintf(stderr, L"[FAIL] %hs\n", #fn); \
    } else { \
        wprintf(L"[PASS] %hs\n", #fn); \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fwprintf(stderr, L"  ASSERT_TRUE failed at %hs:%d: %hs\n", __FILE__, __LINE__, #cond); \
        g_tf_cur_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        fwprintf(stderr, L"  ASSERT_EQ_INT failed at %hs:%d: expected %lld, got %lld\n", __FILE__, __LINE__, _e, _a); \
        g_tf_cur_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_WSTR(expected, actual) do { \
    const wchar_t *_e = (expected); \
    const wchar_t *_a = (actual); \
    if (!_e || !_a || wcscmp(_e, _a) != 0) { \
        fwprintf(stderr, L"  ASSERT_EQ_WSTR failed at %hs:%d: expected \"%ls\", got \"%ls\"\n", \
            __FILE__, __LINE__, _e ? _e : L"(null)", _a ? _a : L"(null)"); \
        g_tf_cur_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_CONTAINS_WSTR(haystack, needle) do { \
    const wchar_t *_h = (haystack); \
    const wchar_t *_n = (needle); \
    if (!_h || !_n || wcsstr(_h, _n) == NULL) { \
        fwprintf(stderr, L"  ASSERT_CONTAINS_WSTR failed at %hs:%d: \"%ls\" not found in \"%ls\"\n", \
            __FILE__, __LINE__, _n ? _n : L"(null)", _h ? _h : L"(null)"); \
        g_tf_cur_failed = 1; \
        return; \
    } \
} while (0)

#define TEST_SUMMARY() do { \
    wprintf(L"Tests: %d passed, %d failed (total %d)\n", g_tf_total - g_tf_failed, g_tf_failed, g_tf_total); \
    return g_tf_failed ? 1 : 0; \
} while (0)

#endif /* PLUMBUS_TEST_FRAMEWORK_H */
