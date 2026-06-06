/*
 * PureRetro — Core option variable table
 *
 * A variable_table stores a list of (key, value) pairs in the order
 * they were inserted (i.e. the order the core declared them via
 * SET_VARIABLES). A parallel sorted_index gives O(log N) lookup by
 * key. Iteration via variable_table_at() returns items in declaration
 * order so the .opt file and any future settings UI match the core's
 * intended layout.
 *
 * The struct is used for three roles on g_frontend:
 *   - variables       : the table the core declared via SET_VARIABLES;
 *                       order is meaningful.
 *   - disk_overrides  : values persisted to <core>.opt across runs.
 *   - cli_overrides   : transient values from --variable CLI flags.
 * The two override tables use the same type for API uniformity; their
 * insertion order has no semantic meaning.
 */

#ifndef CORE_VARIABLES_H
#define CORE_VARIABLES_H

#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

struct variable_table {
    struct retro_variable *items;          /* declaration order */
    size_t                 count;
    size_t                 capacity;

    /* Sorted index for O(log N) key lookup. sorted_index[k] is an
     * index into items[]; items[sorted_index[*]] is monotonically
     * increasing by strcmp(key). Rebuilt on insertions that add new
     * keys; in-place value updates leave the index alone. */
    size_t                *sorted_index;
    size_t                 index_capacity;
};

/* In-place update if key exists; otherwise append to items and insert
 * into sorted_index at the correct position. Returns false only on
 * allocation failure. Both key and value are copied. */
bool variable_table_set(struct variable_table *t,
                        const char *key, const char *value);

/* O(log N) key lookup. Returns NULL if absent. The returned pointer
 * is owned by the table and remains valid until the next mutation. */
const char *variable_table_get(const struct variable_table *t,
                               const char *key);

/* Free items, sorted_index, and reset counts/capacities to 0.
 * Safe to call on an already-empty table. */
void variable_table_clear(struct variable_table *t);

/* Number of (key,value) pairs in the table. */
size_t variable_table_count(const struct variable_table *t);

/* Read-only access by declaration-order index. Returns NULL if i is
 * out of range. */
const struct retro_variable *variable_table_at(
        const struct variable_table *t, size_t i);

#ifdef __cplusplus
}
#endif

#endif /* CORE_VARIABLES_H */
