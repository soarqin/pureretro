/*
 * PureRetro — libretro content loading helpers
 *
 * Pure functions for deciding whether content should be passed as a
 * full path or as a frontend-owned memory buffer.
 */

#ifndef CORE_CONTENT_H
#define CORE_CONTENT_H

#include "frontend.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct core_content_load_policy
{
    bool need_fullpath;
    bool persistent_data;
};

/* Return the extension portion of path without the dot, or an empty string. */
const char *core_content_extension(const char *path);

/* Match extension against a pipe-delimited libretro extension list. */
bool core_content_extension_matches(const char *extensions,
                                    const char *extension);

/* Apply any matching SET_CONTENT_INFO_OVERRIDE entry to the default policy. */
void core_content_apply_overrides(const char *path,
    const struct content_info_override_storage *overrides,
    unsigned override_count,
    struct core_content_load_policy *policy);

#ifdef __cplusplus
}
#endif

#endif /* CORE_CONTENT_H */
