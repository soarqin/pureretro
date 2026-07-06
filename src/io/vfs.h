#ifndef VFS_H
#define VFS_H

#include "libretro.h"

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vfs_file_handle {
    FILE *fp;
    char *path;
};

struct retro_vfs_interface *vfs_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* VFS_H */
