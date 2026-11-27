#include "test_framework.h"
#include "git_native.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>

static void write_bytes_file(const wchar_t *path, const void *data, size_t len) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(f, data, (DWORD)len, &w, NULL);
        CloseHandle(f);
    }
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void write_synthetic_dirc_v2(const wchar_t *idx_path, const char *rel_path, uint32_t file_size) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "DIRC", 4);
    put_be32(buf + 4, 2); /* version 2 */
    put_be32(buf + 8, 1); /* 1 entry */

    size_t off = 12;
    /* mtime_s = 0 (size-only match in synthetic test), size = file_size */
    put_be32(buf + off + 36, file_size);
    size_t nlen = strlen(rel_path);
    buf[off + 60] = 0;
    buf[off + 61] = (uint8_t)(nlen & 0xFF);
    memcpy(buf + off + 62, rel_path, nlen + 1);

    size_t entry_len = 62 + nlen + 1;
    size_t pad = (8 - (entry_len & 7)) & 7;
    size_t total = off + entry_len + pad;
    write_bytes_file(idx_path, buf, total);
}

TEST_CASE(test_git_native_clean_dirty_untracked_ignored_detached_perf) {
    wchar_t tmp[MAX_PATH], repo[MAX_PATH], gitdir[MAX_PATH], sub[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(repo, MAX_PATH, L"%lsplumbus_git_test", tmp);
    swprintf(gitdir, MAX_PATH, L"%ls\\.git", repo);
    swprintf(sub, MAX_PATH, L"%ls\\src", repo);

    CreateDirectoryW(repo, NULL);
    CreateDirectoryW(gitdir, NULL);
    CreateDirectoryW(sub, NULL);

    wchar_t head_p[MAX_PATH], idx_p[MAX_PATH], tracked_p[MAX_PATH], ig_p[MAX_PATH], ign_file[MAX_PATH], untracked_p[MAX_PATH];
    swprintf(head_p, MAX_PATH, L"%ls\\HEAD", gitdir);
    swprintf(idx_p, MAX_PATH, L"%ls\\index", gitdir);
    swprintf(tracked_p, MAX_PATH, L"%ls\\readme.txt", repo);
    swprintf(ig_p, MAX_PATH, L"%ls\\.gitignore", repo);
    swprintf(ign_file, MAX_PATH, L"%ls\\app.log", repo);
    swprintf(untracked_p, MAX_PATH, L"%ls\\new.txt", sub);

    DeleteFileW(untracked_p);

    const char *head_main = "ref: refs/heads/main\n";
    write_bytes_file(head_p, head_main, strlen(head_main));

    const char *tracked_data = "hello";
    write_bytes_file(tracked_p, tracked_data, 5);
    write_synthetic_dirc_v2(idx_p, "readme.txt", 5);

    /* Ignore .gitignore and *.log */
    const char *ig_data = ".gitignore\n*.log\n";
    write_bytes_file(ig_p, ig_data, strlen(ig_data));
    write_bytes_file(ign_file, "log", 3);

    /* 1. Clean repo + nested subdir discovery + < 1 ms perf */
    git_status_t st;
    ASSERT_TRUE(git_native_inspect(sub, &st)); /* warm OS page cache after file creation */

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (int r = 0; r < 10; r++) {
        ASSERT_TRUE(git_native_inspect(sub, &st));
    }
    QueryPerformanceCounter(&t1);
    double elapsed_ms = (((double)(t1.QuadPart - t0.QuadPart) * 1000.0) / (double)freq.QuadPart) / 10.0;
    wprintf(L"  [PERF] git_native_inspect avg (10 runs): %.3f ms\n", elapsed_ms);
    ASSERT_TRUE(elapsed_ms < 2.0);

    ASSERT_TRUE(st.in_repo);
    ASSERT_TRUE(!st.is_detached);
    ASSERT_EQ_WSTR(L"main", st.branch);
    ASSERT_TRUE(!st.is_dirty);

    /* 2. Untracked non-ignored file -> dirty */
    write_bytes_file(untracked_p, "x", 1);
    ASSERT_TRUE(git_native_inspect(repo, &st));
    ASSERT_TRUE(st.has_untracked);
    ASSERT_TRUE(st.is_dirty);
    DeleteFileW(untracked_p);

    /* 3. Modified tracked file -> dirty */
    write_bytes_file(tracked_p, "hello world!", 12);
    ASSERT_TRUE(git_native_inspect(repo, &st));
    ASSERT_TRUE(st.is_dirty);

    /* 4. Detached HEAD */
    const char *detached = "0123456789abcdef0123456789abcdef01234567\n";
    write_bytes_file(head_p, detached, strlen(detached));
    ASSERT_TRUE(git_native_inspect(repo, &st));
    ASSERT_TRUE(st.is_detached);
    ASSERT_EQ_WSTR(L"0123456", st.branch);

    /* Cleanup */
    DeleteFileW(ign_file);
    DeleteFileW(ig_p);
    DeleteFileW(tracked_p);
    DeleteFileW(idx_p);
    DeleteFileW(head_p);
    RemoveDirectoryW(sub);
    RemoveDirectoryW(gitdir);
    RemoveDirectoryW(repo);
}

int wmain(void) {
    RUN_TEST(test_git_native_clean_dirty_untracked_ignored_detached_perf);
    TEST_SUMMARY();
}
