/*
 * PureRetro — Core option string parsers
 *
 * Pure-string helpers shared by core_variables.c. The "raw" value
 * passed to these functions is the value-string format documented in
 * libretro.h for retro_variable: "description; default|option1|...".
 */

#ifndef CORE_VARIABLES_PARSE_H
#define CORE_VARIABLES_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the default value (the first choice after the ';') into out.
 * On success out is NUL-terminated and the function returns true.
 * On failure out[0] is set to '\0' (when out_len > 0) so callers can
 * read out unconditionally. */
bool core_var_parse_default(const char *raw, char *out, size_t out_len);

/* Copy the description portion (text before ';') into out, trimmed of
 * trailing whitespace. If no ';' is present, out is set to empty. */
void core_var_parse_description(const char *raw, char *out, size_t out_len);

/* Return a pointer to the first character of the choices list (the
 * part after "; "), or NULL if there is no choices list. */
const char *core_var_choices_begin(const char *raw);

#ifdef __cplusplus
}
#endif

#endif /* CORE_VARIABLES_PARSE_H */
