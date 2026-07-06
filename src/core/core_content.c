/*
 * PureRetro — libretro content loading helpers
 */

#include "core_content.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

const char *core_content_extension(const char *path)
{
    if (!path)
        return "";

    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    const char *dot = NULL;
    for (const char *p = base; *p; ++p) {
        if (*p == '.')
            dot = p;
    }

    if (!dot || dot == base || dot[1] == '\0')
        return "";
    return dot + 1;
}

static bool extension_token_equals(const char *token, size_t token_len,
                                   const char *extension)
{
    size_t ext_len = strlen(extension);
    if (token_len != ext_len)
        return false;

    for (size_t i = 0; i < token_len; ++i) {
        unsigned char a = (unsigned char)token[i];
        unsigned char b = (unsigned char)extension[i];
        if (tolower(a) != tolower(b))
            return false;
    }
    return true;
}

bool core_content_extension_matches(const char *extensions,
                                    const char *extension)
{
    if (!extensions || !extension || extension[0] == '\0')
        return false;

    const char *token = extensions;
    while (*token) {
        while (*token == '|' || *token == ' ' || *token == '\t')
            ++token;

        const char *end = token;
        while (*end && *end != '|')
            ++end;

        const char *trimmed_end = end;
        while (trimmed_end > token &&
               (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t')) {
            --trimmed_end;
        }

        if (trimmed_end > token &&
            extension_token_equals(token, (size_t)(trimmed_end - token), extension)) {
            return true;
        }

        token = end;
    }

    return false;
}

void core_content_apply_overrides(const char *path,
    const struct content_info_override_storage *overrides,
    unsigned override_count,
    struct core_content_load_policy *policy)
{
    if (!policy || !overrides || override_count == 0)
        return;

    const char *extension = core_content_extension(path);
    if (extension[0] == '\0')
        return;

    for (unsigned i = 0; i < override_count; ++i) {
        if (core_content_extension_matches(overrides[i].extensions, extension)) {
            policy->need_fullpath = overrides[i].need_fullpath;
            /* The frontend keeps memory-backed content alive until shutdown,
             * so reporting persistent data is always truthful when data is
             * provided. Preserve false here only for callers that deliberately
             * choose a shorter lifetime in the future. */
            policy->persistent_data = overrides[i].persistent_data;
            return;
        }
    }
}
