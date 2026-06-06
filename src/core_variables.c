/*
 * PureRetro — Core option variable table implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_variables.h"
#include "core_variables_internal.h"
#include "core_variables_parse.h"
#include "frontend.h"

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

/* ------------------------------------------------------------------ */
/* Variable persistence (per-core .opt file)                          */
/* ------------------------------------------------------------------ */

/* Extract the core's short name from a path like ".../nestopia_libretro.so".
 * Returns a heap-allocated string with directory and any "_libretro.{so,dll,dylib}"
 * suffix stripped. Caller frees. */
static char *core_basename(const char *core_path)
{
    if (!core_path)
        return NULL;

    /* Find the last path separator */
    const char *base = core_path;
    for (const char *p = core_path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    size_t len = strlen(base);

    /* Strip known shared-object extensions */
    static const char *exts[] = { ".so", ".dll", ".dylib", NULL };
    for (size_t i = 0; exts[i]; ++i) {
        size_t el = strlen(exts[i]);
        if (len > el && strcmp(base + len - el, exts[i]) == 0) {
            len -= el;
            break;
        }
    }

    /* Strip the "_libretro" suffix if present */
    static const char libretro_suffix[] = "_libretro";
    size_t sl = sizeof(libretro_suffix) - 1;
    if (len > sl && strncmp(base + len - sl, libretro_suffix, sl) == 0)
        len -= sl;

    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

char *core_variables_path(const char *core_path, const char *base_dir)
{
    if (!base_dir)
        return NULL;

    char *name = core_basename(core_path);
    if (!name)
        return NULL;

    size_t bl = strlen(base_dir);
    /* base_dir may or may not end with a separator */
    bool need_sep = bl > 0 && base_dir[bl - 1] != '/' && base_dir[bl - 1] != '\\';
    size_t total = bl + (need_sep ? 1 : 0) + strlen(name) + 4 + 1;
    char *out = malloc(total);
    if (!out) {
        free(name);
        return NULL;
    }
    snprintf(out, total, "%s%s%s.opt", base_dir, need_sep ? "/" : "", name);
    free(name);
    return out;
}

bool core_variables_load(const char *path)
{
    if (!path)
        return false;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Missing file is not an error */
        return true;
    }

    char line[1024];
    size_t loaded = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline / CR */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip blank lines and comments */
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0' || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = p;
        char *value = eq + 1;

        /* Trim trailing whitespace on the key */
        size_t kl = strlen(key);
        while (kl > 0 && (key[kl - 1] == ' ' || key[kl - 1] == '\t'))
            key[--kl] = '\0';
        if (kl == 0)
            continue;

        if (variable_add(&g_frontend.disk_overrides,
                         &g_frontend.disk_override_count,
                         &g_frontend.disk_override_capacity,
                         key, value)) {
            loaded++;
        }
    }
    fclose(fp);

    variables_sort(g_frontend.disk_overrides, g_frontend.disk_override_count);
    fprintf(stderr, "Loaded %zu variable override(s) from %s\n", loaded, path);
    return true;
}

/* Write the choices list portion of a raw variable value string as a
 * single comment line: "# Choices: a | b | c". Choices are pipe-separated
 * in the raw value; we normalize separators with " | " for readability. */
static void write_choices_comment(FILE *fp, const char *raw)
{
    const char *p = core_var_choices_begin(raw);
    if (!p)
        return;

    fputs("# Choices: ", fp);
    bool first = true;
    while (*p) {
        if (!first)
            fputs(" | ", fp);
        first = false;
        while (*p && *p != '|')
            fputc(*p++, fp);
        if (*p == '|')
            ++p;
    }
    fputc('\n', fp);
}

bool core_variables_save(const char *path)
{
    if (!path)
        return false;

    /* Persist disk_overrides, with rich comments derived from the variables
     * the core declared this run. CLI overrides are intentionally excluded.
     *
     * Layout for each declared variable:
     *   # <description>
     *   # Choices: a | b | c
     *   key=value
     *
     * Disk overrides whose keys were not declared by the core this run
     * (e.g. left over from a previous core version) are written without a
     * comment block to preserve the user's data. */

    if (g_frontend.disk_override_count == 0 &&
        g_frontend.variable_count == 0) {
        /* Nothing to write. Avoid creating an empty file. */
        return true;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing: cannot persist variables\n",
                path);
        return false;
    }

    fputs("# PureRetro core options\n", fp);
    fputs("# Lines starting with '#' are comments. Edit values after '=' to taste.\n",
          fp);
    fputs("# Delete this file to reset all options to their defaults.\n", fp);

    size_t written = 0;

    /* First pass: every variable the core declared, with comment block. */
    for (size_t i = 0; i < g_frontend.variable_count; ++i) {
        const char *key = g_frontend.variables[i].key;
        const char *raw = g_frontend.variables[i].value;

        const char *value = variables_find(g_frontend.disk_overrides,
                                            g_frontend.disk_override_count,
                                            key);
        if (!value) {
            /* No persisted value (should not happen — SET_VARIABLES seeds
             * one). Fall back to parsing the default on the fly. */
            static char def[256];
            if (core_var_parse_default(raw, def, sizeof(def)))
                value = def;
        }
        if (!value)
            continue;

        char desc[256];
        core_var_parse_description(raw, desc, sizeof(desc));

        fputc('\n', fp);
        if (desc[0])
            fprintf(fp, "# %s\n", desc);
        write_choices_comment(fp, raw);

        /* If a CLI --variable override is active for this key, the value
         * the core actually saw this run differs from what we are about
         * to persist. Make that explicit so users do not wrongly assume
         * the CLI flag was saved. */
        const char *cli = variables_find(g_frontend.cli_overrides,
                                          g_frontend.cli_override_count,
                                          key);
        if (cli)
            fprintf(fp, "# (CLI override in effect this run: %s)\n", cli);

        fprintf(fp, "%s=%s\n", key, value);
        written++;
    }

    /* Second pass: stray disk overrides whose key the core did not declare. */
    bool stray_header = false;
    for (size_t i = 0; i < g_frontend.disk_override_count; ++i) {
        const char *key = g_frontend.disk_overrides[i].key;
        if (variables_find(g_frontend.variables, g_frontend.variable_count, key))
            continue;

        if (!stray_header) {
            fputs("\n# --- Persisted from a previous run; not declared by the "
                  "current core ---\n", fp);
            stray_header = true;
        }
        fprintf(fp, "%s=%s\n", key, g_frontend.disk_overrides[i].value);
        written++;
    }

    fclose(fp);
    fprintf(stderr, "Saved %zu variable(s) to %s\n", written, path);
    return true;
}

void core_variable_override(const char *key, const char *value)
{
    if (!key || !value)
        return;
    if (!variable_add(&g_frontend.cli_overrides, &g_frontend.cli_override_count,
                      &g_frontend.cli_override_capacity, key, value)) {
        fprintf(stderr, "Failed to store variable override: %s=%s\n", key, value);
    } else {
        fprintf(stderr, "Variable override (CLI): %s=%s\n", key, value);
    }
}
