/*
 * PureRetro — Core option variable table implementation.
 */

#include <stdlib.h>
#include <string.h>
#include "core_variables.h"

/* Find sorted_index slot whose item->key equals key, or return
 * (size_t)-1 if not found. Uses bsearch semantics (O(log N)). */
static size_t sorted_index_find(const struct variable_table *t, const char *key)
{
    size_t lo = 0, hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(t->items[t->sorted_index[mid]].key, key);
        if (cmp == 0)
            return mid;
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (size_t)-1;
}

/* Find sorted_index insertion point for key (assumes key not present).
 * Returns a value in [0, count]. */
static size_t sorted_index_lower_bound(const struct variable_table *t,
                                       const char *key)
{
    size_t lo = 0, hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(t->items[t->sorted_index[mid]].key, key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static bool items_grow(struct variable_table *t)
{
    size_t new_cap = t->capacity ? t->capacity * 2 : 16;
    struct retro_variable *new_items =
        realloc(t->items, new_cap * sizeof(*new_items));
    if (!new_items)
        return false;
    t->items = new_items;
    t->capacity = new_cap;
    return true;
}

static bool sorted_index_grow(struct variable_table *t)
{
    size_t new_cap = t->index_capacity ? t->index_capacity * 2 : 16;
    size_t *new_index = realloc(t->sorted_index, new_cap * sizeof(*new_index));
    if (!new_index)
        return false;
    t->sorted_index = new_index;
    t->index_capacity = new_cap;
    return true;
}

bool variable_table_set(struct variable_table *t,
                        const char *key, const char *value)
{
    if (!t || !key || !value)
        return false;

    /* In-place update if key already exists. */
    size_t hit = sorted_index_find(t, key);
    if (hit != (size_t)-1) {
        size_t item_idx = t->sorted_index[hit];
        size_t vl = strlen(value);
        char *v = malloc(vl + 1);
        if (!v)
            return false;
        memcpy(v, value, vl + 1);
        free((char *)t->items[item_idx].value);
        t->items[item_idx].value = v;
        return true;
    }

    /* New key: grow storage if needed. */
    if (t->count >= t->capacity && !items_grow(t))
        return false;
    if (t->count >= t->index_capacity && !sorted_index_grow(t))
        return false;

    size_t kl = strlen(key);
    size_t vl = strlen(value);
    char *k = malloc(kl + 1);
    char *v = malloc(vl + 1);
    if (!k || !v) {
        free(k);
        free(v);
        return false;
    }
    memcpy(k, key, kl + 1);
    memcpy(v, value, vl + 1);

    size_t new_item_idx = t->count;
    t->items[new_item_idx].key = k;
    t->items[new_item_idx].value = v;

    /* Insert new_item_idx into sorted_index at the right position. */
    size_t pos = sorted_index_lower_bound(t, key);
    if (pos < t->count) {
        memmove(&t->sorted_index[pos + 1], &t->sorted_index[pos],
                (t->count - pos) * sizeof(t->sorted_index[0]));
    }
    t->sorted_index[pos] = new_item_idx;

    t->count++;
    return true;
}

const char *variable_table_get(const struct variable_table *t, const char *key)
{
    if (!t || !key || t->count == 0)
        return NULL;
    size_t hit = sorted_index_find(t, key);
    if (hit == (size_t)-1)
        return NULL;
    return t->items[t->sorted_index[hit]].value;
}

void variable_table_clear(struct variable_table *t)
{
    if (!t)
        return;
    if (t->items) {
        for (size_t i = 0; i < t->count; ++i) {
            free((char *)t->items[i].key);
            free((char *)t->items[i].value);
        }
        free(t->items);
    }
    free(t->sorted_index);
    t->items = NULL;
    t->sorted_index = NULL;
    t->count = 0;
    t->capacity = 0;
    t->index_capacity = 0;
}

size_t variable_table_count(const struct variable_table *t)
{
    return t ? t->count : 0;
}

const struct retro_variable *variable_table_at(
        const struct variable_table *t, size_t i)
{
    if (!t || i >= t->count)
        return NULL;
    return &t->items[i];
}
