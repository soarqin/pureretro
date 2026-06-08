/*
 * PureRetro — Virtual File System wrapper around standard C stdio
 *
 * Provides a retro_vfs_interface implementation so cores can use
 * the frontend's file I/O when RETRO_ENVIRONMENT_GET_VFS_INTERFACE
 * is requested.
 */

#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#endif

#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <sys/types.h>
#  define fseeko _fseeki64
#  define ftello _ftelli64
#  if !defined(__MINGW32__) && !defined(_OFF_T_DEFINED)
     typedef long long off_t;
#    define _OFF_T_DEFINED
#  endif
#else
#  include <sys/types.h>
#endif

static const char *RETRO_CALLCONV vfs_get_path(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return NULL;
    return h->path;
}

static struct retro_vfs_file_handle *RETRO_CALLCONV vfs_open(const char *path,
                                                             unsigned mode,
                                                             unsigned hints)
{
    (void)hints;
    if (!path)
        return NULL;

    const char *mode_str = "rb";
    if (mode & RETRO_VFS_FILE_ACCESS_WRITE) {
        if (mode & RETRO_VFS_FILE_ACCESS_READ)
            mode_str = "w+b";
        else
            mode_str = "wb";
    } else if (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) {
        mode_str = "r+b";
    }

    FILE *fp = fopen(path, mode_str);
    if (!fp)
        return NULL;

    struct vfs_file_handle *h = calloc(1, sizeof(*h));
    if (!h) {
        fclose(fp);
        return NULL;
    }
    h->fp = fp;
    h->path = malloc(strlen(path) + 1);
    if (!h->path) {
        fclose(fp);
        free(h);
        return NULL;
    }
    strcpy(h->path, path);
    return (struct retro_vfs_file_handle *)h;
}

static int RETRO_CALLCONV vfs_close(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    int rc = fclose(h->fp);
    free(h->path);
    free(h);
    return rc;
}

/* vfs_size: M-9 fix — restore failure now propagates as -1 */
static int64_t RETRO_CALLCONV vfs_size(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    int64_t pos = ftello(h->fp);
    if (pos < 0)
        return -1;
    if (fseeko(h->fp, 0, SEEK_END) != 0)
        return -1;
    int64_t end = ftello(h->fp);
    if (end < 0)
        return -1;
    if (fseeko(h->fp, (off_t)pos, SEEK_SET) != 0)
        return -1;
    return end;
}

static int64_t RETRO_CALLCONV vfs_tell(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    return ftello(h->fp);
}

/* vfs_seek: C-3 fix — return new absolute offset */
static int64_t RETRO_CALLCONV vfs_seek(struct retro_vfs_file_handle *stream,
                                       int64_t offset, int whence)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    int std_whence = SEEK_SET;
    if (whence == RETRO_VFS_SEEK_POSITION_CURRENT)
        std_whence = SEEK_CUR;
    else if (whence == RETRO_VFS_SEEK_POSITION_END)
        std_whence = SEEK_END;
    if (fseeko(h->fp, (off_t)offset, std_whence) != 0)
        return -1;
    return ftello(h->fp);
}

/* vfs_read: C-4 fix — distinguish EOF from error via ferror */
static int64_t RETRO_CALLCONV vfs_read(struct retro_vfs_file_handle *stream,
                                       void *s, uint64_t len)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h || !s)
        return -1;
    if (len > SIZE_MAX)
        return -1;
    size_t n = fread(s, 1, (size_t)len, h->fp);
    if (n < (size_t)len && ferror(h->fp)) {
        clearerr(h->fp);
        return -1;
    }
    return (int64_t)n;
}

/* vfs_write: C-4 fix — distinguish short-write from error */
static int64_t RETRO_CALLCONV vfs_write(struct retro_vfs_file_handle *stream,
                                        const void *s, uint64_t len)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h || !s)
        return -1;
    if (len > SIZE_MAX)
        return -1;
    size_t n = fwrite(s, 1, (size_t)len, h->fp);
    if (n < (size_t)len && ferror(h->fp)) {
        clearerr(h->fp);
        return -1;
    }
    return (int64_t)n;
}

static int RETRO_CALLCONV vfs_flush(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    return fflush(h->fp);
}

static int RETRO_CALLCONV vfs_remove(const char *path)
{
    if (!path)
        return -1;
    return remove(path);
}

static int RETRO_CALLCONV vfs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path)
        return -1;
    return rename(old_path, new_path);
}

static struct retro_vfs_interface g_vfs_interface = {
    vfs_get_path,
    vfs_open,
    vfs_close,
    vfs_size,
    vfs_tell,
    vfs_seek,
    vfs_read,
    vfs_write,
    vfs_flush,
    vfs_remove,
    vfs_rename,
    /* VFS API v2+ not implemented */
    NULL, /* truncate */
    NULL, /* stat */
    NULL, /* mkdir */
    NULL, /* opendir */
    NULL, /* readdir */
    NULL, /* dirent_get_name */
    NULL, /* dirent_is_dir */
    NULL, /* closedir */
    NULL, /* stat_64 */
};

struct retro_vfs_interface *vfs_get_interface(void)
{
    return &g_vfs_interface;
}
