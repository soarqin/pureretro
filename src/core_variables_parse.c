/*
 * PureRetro — Core option string parsers (see core_variables_parse.h).
 */

#include "core_variables_parse.h"

#include <string.h>

bool core_var_parse_default(const char *raw, char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return false;
    out[0] = '\0';
    if (!raw)
        return false;

    const char *def = strchr(raw, ';');
    if (!def) {
        /* v0 spec: "desc" with no ';' means no default. */
        return false;
    }
    ++def;
    while (*def == ' ')
        ++def;

    if (*def == '\0')
        return false;

    size_t i = 0;
    while (*def && *def != '|' && i < out_len - 1)
        out[i++] = *def++;
    out[i] = '\0';
    return i > 0;
}

void core_var_parse_description(const char *raw, char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return;
    out[0] = '\0';
    if (!raw)
        return;

    const char *semi = strchr(raw, ';');
    size_t n = semi ? (size_t)(semi - raw) : 0;
    if (n >= out_len)
        n = out_len - 1;
    memcpy(out, raw, n);
    out[n] = '\0';

    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'))
        out[--n] = '\0';
}

const char *core_var_choices_begin(const char *raw)
{
    if (!raw)
        return NULL;
    const char *p = strchr(raw, ';');
    if (!p)
        return raw;
    ++p;
    while (*p == ' ')
        ++p;
    return *p ? p : NULL;
}
