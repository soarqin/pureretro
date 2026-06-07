/*
 * PureRetro — Virtual File System wrapper around standard C stdio
 *
 * Provides a retro_vfs_interface implementation so cores can use
 * the frontend's file I/O when RETRO_ENVIRONMENT_GET_VFS_INTERFACE
 * is requested.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vfs.h"

static const char *vfs_get_path(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return NULL;
    return h->path;
}

static struct retro_vfs_file_handle *vfs_open(const char *path,
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

static int vfs_close(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    int rc = fclose(h->fp);
    free(h->path);
    free(h);
    return rc;
}

static int64_t vfs_size(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    long pos = ftell(h->fp);
    if (pos < 0)
        return -1;
    if (fseek(h->fp, 0, SEEK_END) != 0)
        return -1;
    long end = ftell(h->fp);
    fseek(h->fp, pos, SEEK_SET);
    return end;
}

static int64_t vfs_tell(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    return ftell(h->fp);
}

static int64_t vfs_seek(struct retro_vfs_file_handle *stream,
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
    return fseek(h->fp, (long)offset, std_whence);
}

static int64_t vfs_read(struct retro_vfs_file_handle *stream,
                        void *s, uint64_t len)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h || !s)
        return -1;
    return (int64_t)fread(s, 1, (size_t)len, h->fp);
}

static int64_t vfs_write(struct retro_vfs_file_handle *stream,
                         const void *s, uint64_t len)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h || !s)
        return -1;
    return (int64_t)fwrite(s, 1, (size_t)len, h->fp);
}

static int vfs_flush(struct retro_vfs_file_handle *stream)
{
    struct vfs_file_handle *h = (struct vfs_file_handle *)stream;
    if (!h)
        return -1;
    return fflush(h->fp);
}

static int vfs_remove(const char *path)
{
    if (!path)
        return -1;
    return remove(path);
}

static int vfs_rename(const char *old_path, const char *new_path)
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
