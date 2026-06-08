/*
 * PureRetro — Core option variable table
 *
 * core_options_table stores structured core options declared by the core
 * via SET_VARIABLES, preserving declaration order for iteration.
 * A parallel sorted_index gives O(log N) lookup by key.
 *
 * variable_table stores a sorted array of (key, value) pairs used for
 * CLI and disk overrides. Lookup uses bsearch.
 */

#ifndef CORE_VARIABLES_H
#define CORE_VARIABLES_H

#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

struct core_option {
    char *key;
    char *desc;
    char *info;
    char **values;       /* NULL-terminated array */
    char *default_value;
    char *current_value;
    bool visible;
};

struct core_options_table {
    struct core_option *options;
    size_t count;
    size_t capacity;
    size_t *sorted_index;
    size_t index_capacity;
};

void core_options_table_clear(struct core_options_table *t);
bool core_options_table_add(struct core_options_table *t,
                            const char *key,
                            const char *desc,
                            const char *info,
                            const char *const *values,
                            const char *default_value);
const struct core_option *core_options_table_get(
        const struct core_options_table *t, const char *key);
const struct core_option *core_options_table_at(
        const struct core_options_table *t, size_t i);
size_t core_options_table_count(const struct core_options_table *t);
bool core_options_table_set_value(struct core_options_table *t,
                                  const char *key,
                                  const char *value);

/* Set the visibility flag for an option (SET_CORE_OPTIONS_DISPLAY).
 * Returns false if the key does not exist. */
bool core_options_table_set_visible(struct core_options_table *t,
                                    const char *key,
                                    bool visible);

struct variable_table {
    struct retro_variable *items;          /* sorted by key */
    size_t count;
    size_t capacity;
};

/* In-place update if key exists; otherwise append and re-sort by key.
 * Returns false only on allocation failure. Both key and value are copied. */
bool variable_table_set(struct variable_table *t,
                        const char *key, const char *value);

/* O(log N) key lookup via bsearch. Returns NULL if absent. The returned
 * pointer is owned by the table and remains valid until the next mutation. */
const char *variable_table_get(const struct variable_table *t,
                               const char *key);

/* Free items and reset counts/capacities to 0.
 * Safe to call on an already-empty table. */
void variable_table_clear(struct variable_table *t);

/* Number of (key,value) pairs in the table. */
size_t variable_table_count(const struct variable_table *t);

/* Read-only access by index in declaration/sorted order. Returns NULL if i is
 * out of range. */
const struct retro_variable *variable_table_at(
        const struct variable_table *t, size_t i);

/* Override a core option variable (used by --variable CLI flag).
 * CLI overrides take priority over disk-persisted values and are
 * never written back to .opt. */
void core_variable_override(const char *key, const char *value);

/* Compute the per-core options file path, given the core shared
 * object path and a base directory (typically
 * g_frontend.system_directory). Returns a heap-allocated string the
 * caller must free, or NULL on allocation failure / NULL base_dir. */
char *core_variables_path(const char *core_path, const char *base_dir);

/* Load persisted overrides from path into g_frontend.disk_overrides.
 * Missing files are not an error. Returns true on success. */
bool core_variables_load(const char *path);

/* Save the current disk overrides to path. CLI-only overrides are
 * intentionally excluded. */
bool core_variables_save(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CORE_VARIABLES_H */
