/*
 * PureRetro — Core SRAM and savestate persistence
 */

#include "core.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SRAM / Savestate persistence                                       */
/* Extract "name" from "<dir>/name.ext". Returns heap-allocated string. */
static char *content_basename_noext(const char *content_path)
{
    if (!content_path)
        return NULL;

    const char *last_sep = content_path;
    for (const char *p = content_path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            last_sep = p + 1;
    }

    const char *dot = NULL;
    for (const char *p = last_sep; *p; ++p) {
        if (*p == '.')
            dot = p;
    }

    size_t len = dot ? (size_t)(dot - last_sep) : strlen(last_sep);
    if (len == 0)
        return NULL;

    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, last_sep, len);
    out[len] = '\0';
    return out;
}

char *core_sram_path(const char *save_dir, const char *content_path)
{
    if (!save_dir || !content_path)
        return NULL;

    char *base = content_basename_noext(content_path);
    if (!base)
        return NULL;

    size_t dl = strlen(save_dir);
    bool need_sep = dl > 0 && save_dir[dl - 1] != '/' && save_dir[dl - 1] != '\\';
    size_t total = dl + (need_sep ? 1 : 0) + strlen(base) + 4 + 1;
    char *out = malloc(total);
    if (!out) {
        free(base);
        return NULL;
    }
    snprintf(out, total, "%s%s%s.srm", save_dir, need_sep ? "/" : "", base);
    free(base);
    return out;
}

bool core_sram_load(const char *path)
{
    if (!path || !g_core.retro_get_memory_data || !g_core.retro_get_memory_size)
        return false;

    void *dst = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t dst_size = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!dst || dst_size == 0)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return true; /* missing is fine */

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    if ((size_t)size > dst_size) {
        LOG_ERROR("SRAM file %s is %ld bytes but core slot is only %zu bytes; "
                  "refusing to truncate (would corrupt the save)", path, size, dst_size);
        fclose(fp);
        return false;
    }

    size_t read_size = (size_t)size;
    size_t got = fread(dst, 1, read_size, fp);
    fclose(fp);

    if (got != read_size) {
        LOG_WARN("SRAM read incomplete: got %zu of %zu bytes from %s",
                 got, read_size, path);
        return false;
    }
    if ((size_t)size != dst_size) {
        LOG_INFO("Loaded %zu bytes of SRAM from %s "
                 "(core slot is %zu bytes)",
                 read_size, path, dst_size);
    } else {
        LOG_INFO("Loaded SRAM (%zu bytes) from %s", read_size, path);
    }
    return true;
}

bool core_sram_save(const char *path)
{
    if (!path || !g_core.retro_get_memory_data || !g_core.retro_get_memory_size)
        return false;

    const void *src = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!src || size == 0)
        return false;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open SRAM path for writing: %s", path);
        return false;
    }

    size_t wrote = fwrite(src, 1, size, fp);
    fclose(fp);

    if (wrote != size) {
        LOG_ERROR("SRAM write incomplete: wrote %zu of %zu bytes to %s",
                  wrote, size, path);
        return false;
    }
    LOG_INFO("Saved SRAM (%zu bytes) to %s", size, path);
    return true;
}

bool core_savestate_load(const char *path)
{
    if (!path || !g_core.retro_unserialize) {
        LOG_WARN("Savestate load skipped: core does not export retro_unserialize");
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Failed to open savestate for reading: %s", path);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        LOG_ERROR("Refusing to load empty savestate: %s", path);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    void *buf = malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        LOG_ERROR("Savestate read incomplete: %s", path);
        return false;
    }

    bool ok = g_core.retro_unserialize(buf, (size_t)size);
    free(buf);
    if (!ok) {
        LOG_ERROR("retro_unserialize rejected %s (%ld bytes)", path, size);
        return false;
    }
    LOG_INFO("Loaded savestate (%ld bytes) from %s", size, path);
    return true;
}

bool core_savestate_save(const char *path)
{
    if (!path || !g_core.retro_serialize || !g_core.retro_serialize_size) {
        LOG_WARN("Savestate save skipped: core does not export retro_serialize");
        return false;
    }

    size_t size = g_core.retro_serialize_size();
    if (size == 0) {
        LOG_ERROR("retro_serialize_size returned 0; nothing to save");
        return false;
    }

    void *buf = malloc(size);
    if (!buf)
        return false;

    if (!g_core.retro_serialize(buf, size)) {
        free(buf);
        LOG_ERROR("retro_serialize failed for %s", path);
        return false;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        LOG_ERROR("Failed to open savestate for writing: %s", path);
        return false;
    }
    size_t wrote = fwrite(buf, 1, size, fp);
    fclose(fp);
    free(buf);

    if (wrote != size) {
        LOG_ERROR("Savestate write incomplete: wrote %zu of %zu bytes to %s",
                  wrote, size, path);
        return false;
    }
    LOG_INFO("Saved savestate (%zu bytes) to %s", size, path);
    return true;
}

/* ------------------------------------------------------------------ */
