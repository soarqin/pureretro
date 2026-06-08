#include <unity.h>

#ifdef _WIN32

#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    printf("test_vfs: skipped on Windows\n");
    return 0;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "vfs.h"

static char g_tmpdir[256];
static char g_tmpfile[512];

void setUp(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir),
             "/tmp/pureretro_test_vfs_%d", (int)getpid());
    snprintf(g_tmpfile, sizeof(g_tmpfile), "%s/data.bin", g_tmpdir);
    mkdir(g_tmpdir, 0700);
}

void tearDown(void) {
    remove(g_tmpfile);
    rmdir(g_tmpdir);
}

static void write_file(const char *path, const void *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(len, fwrite(data, 1, len, fp));
    fclose(fp);
}

/* C-3 regression: seek must return the new absolute byte offset, not 0/status. */
static void test_seek_returns_new_position(void) {
    struct retro_vfs_interface *vfs = vfs_get_interface();
    TEST_ASSERT_NOT_NULL(vfs);

    write_file(g_tmpfile, "0123456789", 10);

    struct retro_vfs_file_handle *h =
        vfs->open(g_tmpfile, RETRO_VFS_FILE_ACCESS_READ, 0);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_EQUAL_INT64(5, vfs->seek(h, 5, RETRO_VFS_SEEK_POSITION_START));
    TEST_ASSERT_EQUAL_INT64(8, vfs->seek(h, 3, RETRO_VFS_SEEK_POSITION_CURRENT));
    TEST_ASSERT_EQUAL_INT64(8, vfs->seek(h, -2, RETRO_VFS_SEEK_POSITION_END));

    vfs->close(h);
}

/* C-4 regression: short read at EOF must NOT be reported as an error. */
static void test_read_distinguishes_eof_from_error(void) {
    struct retro_vfs_interface *vfs = vfs_get_interface();
    TEST_ASSERT_NOT_NULL(vfs);

    write_file(g_tmpfile, "hello", 5);

    struct retro_vfs_file_handle *h =
        vfs->open(g_tmpfile, RETRO_VFS_FILE_ACCESS_READ, 0);
    TEST_ASSERT_NOT_NULL(h);

    char buf[32];
    TEST_ASSERT_EQUAL_INT64(5, vfs->read(h, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT64(0, vfs->read(h, buf, sizeof(buf)));

    vfs->close(h);
}

/* M-9 regression: size() must restore stream position. */
static void test_size_preserves_stream_position(void) {
    struct retro_vfs_interface *vfs = vfs_get_interface();
    TEST_ASSERT_NOT_NULL(vfs);

    write_file(g_tmpfile, "0123456789", 10);

    struct retro_vfs_file_handle *h =
        vfs->open(g_tmpfile, RETRO_VFS_FILE_ACCESS_READ, 0);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_EQUAL_INT64(3, vfs->seek(h, 3, RETRO_VFS_SEEK_POSITION_START));
    TEST_ASSERT_EQUAL_INT64(10, vfs->size(h));
    TEST_ASSERT_EQUAL_INT64(3, vfs->tell(h));

    vfs->close(h);
}

/* Sanity: write-then-read roundtrip via the VFS interface itself. */
static void test_write_then_read_roundtrip(void) {
    struct retro_vfs_interface *vfs = vfs_get_interface();
    TEST_ASSERT_NOT_NULL(vfs);

    struct retro_vfs_file_handle *h =
        vfs->open(g_tmpfile, RETRO_VFS_FILE_ACCESS_WRITE, 0);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_INT64(5, vfs->write(h, "hello", 5));
    vfs->close(h);

    h = vfs->open(g_tmpfile, RETRO_VFS_FILE_ACCESS_READ, 0);
    TEST_ASSERT_NOT_NULL(h);
    char buf[6] = {0};
    TEST_ASSERT_EQUAL_INT64(5, vfs->read(h, buf, 5));
    TEST_ASSERT_EQUAL_STRING("hello", buf);
    vfs->close(h);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_seek_returns_new_position);
    RUN_TEST(test_read_distinguishes_eof_from_error);
    RUN_TEST(test_size_preserves_stream_position);
    RUN_TEST(test_write_then_read_roundtrip);
    return UNITY_END();
}

#endif /* !_WIN32 */
