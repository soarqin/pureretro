/*
 * PureRetro — Core option variable table implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_variables.h"
#include "core_variables_parse.h"
#include "frontend.h"

/* ------------------------------------------------------------------ */
/* core_options_table                                                  */
/* ------------------------------------------------------------------ */

static void core_option_clear(struct core_option *opt)
{
    if (!opt)
        return;
    free(opt->key);
    free(opt->desc);
    free(opt->info);
    if (opt->values) {
        for (size_t i = 0; opt->values[i]; ++i)
            free(opt->values[i]);
        free(opt->values);
    }
    free(opt->default_value);
    free(opt->current_value);
    memset(opt, 0, sizeof(*opt));
}

void core_options_table_clear(struct core_options_table *t)
{
    if (!t)
        return;
    for (size_t i = 0; i < t->count; ++i)
        core_option_clear(&t->options[i]);
    free(t->options);
    free(t->sorted_index);
    t->options = NULL;
    t->sorted_index = NULL;
    t->count = 0;
    t->capacity = 0;
    t->index_capacity = 0;
}

bool core_options_table_add(struct core_options_table *t,
                            const char *key,
                            const char *desc,
                            const char *info,
                            const char *const *values,
                            const char *default_value)
{
    if (!t || !key)
        return false;

    if (t->count >= t->capacity) {
        size_t new_cap = t->capacity ? t->capacity * 2 : 16;
        struct core_option *new_opts =
            realloc(t->options, new_cap * sizeof(*new_opts));
        if (!new_opts)
            return false;
        t->options = new_opts;
        t->capacity = new_cap;
    }

    if (t->count >= t->index_capacity) {
        size_t new_cap = t->index_capacity ? t->index_capacity * 2 : 16;
        size_t *new_idx = realloc(t->sorted_index, new_cap * sizeof(*new_idx));
        if (!new_idx)
            return false;
        t->sorted_index = new_idx;
        t->index_capacity = new_cap;
    }

    struct core_option opt = {0};
    opt.key = strdup(key);
    if (!opt.key)
        return false;
    if (desc) {
        opt.desc = strdup(desc);
        if (!opt.desc)
            goto fail;
    }
    if (info) {
        opt.info = strdup(info);
        if (!opt.info)
            goto fail;
    }
    if (default_value) {
        opt.default_value = strdup(default_value);
        if (!opt.default_value)
            goto fail;
    }

    if (values) {
        size_t n = 0;
        for (const char *const *vp = values; *vp; ++vp)
            ++n;
        opt.values = calloc(n + 1, sizeof(char *));
        if (!opt.values)
            goto fail;
        for (size_t i = 0; i < n; ++i) {
            opt.values[i] = strdup(values[i]);
            if (!opt.values[i])
                goto fail;
        }
        opt.values[n] = NULL;
    }

    t->options[t->count] = opt;

    /* Insert t->count into sorted_index at the correct position. */
    size_t pos = t->count;
    for (size_t i = 0; i < t->count; ++i) {
        if (strcmp(key, t->options[t->sorted_index[i]].key) < 0) {
            pos = i;
            break;
        }
    }
    if (pos < t->count) {
        memmove(&t->sorted_index[pos + 1], &t->sorted_index[pos],
                (t->count - pos) * sizeof(t->sorted_index[0]));
    }
    t->sorted_index[pos] = t->count;

    t->count++;
    return true;

fail:
    core_option_clear(&opt);
    return false;
}

const struct core_option *core_options_table_get(
        const struct core_options_table *t, const char *key)
{
    if (!t || !key || t->count == 0)
        return NULL;

    size_t lo = 0, hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(key, t->options[t->sorted_index[mid]].key);
        if (cmp == 0)
            return &t->options[t->sorted_index[mid]];
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

const struct core_option *core_options_table_at(
        const struct core_options_table *t, size_t i)
{
    if (!t || i >= t->count)
        return NULL;
    return &t->options[i];
}

size_t core_options_table_count(const struct core_options_table *t)
{
    return t ? t->count : 0;
}

bool core_options_table_set_value(struct core_options_table *t,
                                  const char *key,
                                  const char *value)
{
    if (!t || !key || !value)
        return false;

    const struct core_option *opt = core_options_table_get(t, key);
    if (!opt)
        return false;

    char *v = strdup(value);
    if (!v)
        return false;

    struct core_option *mutable_opt = (struct core_option *)opt;
    free(mutable_opt->current_value);
    mutable_opt->current_value = v;
    return true;
}

/* ------------------------------------------------------------------ */
/* variable_table                                                      */
/* ------------------------------------------------------------------ */

static int retro_var_cmp(const void *a, const void *b)
{
    const struct retro_variable *va = a;
    const struct retro_variable *vb = b;
    return strcmp(va->key, vb->key);
}

bool variable_table_set(struct variable_table *t,
                        const char *key, const char *value)
{
    if (!t || !key || !value)
        return false;

    /* Try in-place update via bsearch. */
    struct retro_variable key_var = { .key = (char *)key };
    struct retro_variable *found = bsearch(&key_var,
                                           t->items, t->count,
                                           sizeof(t->items[0]), retro_var_cmp);
    if (found) {
        char *v = strdup(value);
        if (!v)
            return false;
        free((char *)found->value);
        found->value = v;
        return true;
    }

    /* Grow and append. */
    if (t->count >= t->capacity) {
        size_t new_cap = t->capacity ? t->capacity * 2 : 16;
        struct retro_variable *new_items =
            realloc(t->items, new_cap * sizeof(*new_items));
        if (!new_items)
            return false;
        t->items = new_items;
        t->capacity = new_cap;
    }

    char *k = strdup(key);
    char *v = strdup(value);
    if (!k || !v) {
        free(k);
        free(v);
        return false;
    }

    t->items[t->count].key = k;
    t->items[t->count].value = v;
    t->count++;

    qsort(t->items, t->count, sizeof(t->items[0]), retro_var_cmp);
    return true;
}

const char *variable_table_get(const struct variable_table *t, const char *key)
{
    if (!t || !key || t->count == 0)
        return NULL;

    struct retro_variable key_var = { .key = (char *)key };
    const struct retro_variable *found = bsearch(&key_var,
                                                  (void *)t->items, t->count,
                                                  sizeof(t->items[0]), retro_var_cmp);
    if (!found)
        return NULL;
    return found->value;
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
    t->items = NULL;
    t->count = 0;
    t->capacity = 0;
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

        if (variable_table_set(&g_frontend.disk_overrides, key, value))
            loaded++;
    }
    fclose(fp);

    fprintf(stderr, "Loaded %zu variable override(s) from %s\n", loaded, path);
    return true;
}

bool core_variables_save(const char *path)
{
    if (!path)
        return false;

    /* Persist disk_overrides, with rich comments derived from the core options
     * the core declared this run. CLI overrides are intentionally excluded.
     *
     * Layout for each declared option:
     *   # <description>
     *   # Choices: a | b | c
     *   key=value
     *
     * Disk overrides whose keys were not declared by the core this run
     * (e.g. left over from a previous core version) are written without a
     * comment block to preserve the user's data. */

    if (core_options_table_count(&g_frontend.core_options) == 0 &&
        variable_table_count(&g_frontend.disk_overrides) == 0) {
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

    /* First pass: every option the core declared, with comment block. */
    size_t opt_count = core_options_table_count(&g_frontend.core_options);
    for (size_t i = 0; i < opt_count; ++i) {
        const struct core_option *opt = core_options_table_at(&g_frontend.core_options, i);
        const char *key = opt->key;

        const char *value = variable_table_get(&g_frontend.disk_overrides, key);
        if (!value)
            value = opt->default_value;
        if (!value)
            continue;

        fputc('\n', fp);
        if (opt->desc && opt->desc[0])
            fprintf(fp, "# %s\n", opt->desc);

        if (opt->values && opt->values[0]) {
            fputs("# Choices: ", fp);
            bool first = true;
            for (size_t j = 0; opt->values[j]; ++j) {
                if (!first)
                    fputs(" | ", fp);
                first = false;
                fputs(opt->values[j], fp);
            }
            fputc('\n', fp);
        }

        const char *cli = variable_table_get(&g_frontend.cli_overrides, key);
        if (cli)
            fprintf(fp, "# (CLI override in effect this run: %s)\n", cli);

        fprintf(fp, "%s=%s\n", key, value);
        written++;
    }

    /* Second pass: stray disk overrides whose key the core did not declare. */
    bool stray_header = false;
    size_t disk_count = variable_table_count(&g_frontend.disk_overrides);
    for (size_t i = 0; i < disk_count; ++i) {
        const struct retro_variable *iv = variable_table_at(&g_frontend.disk_overrides, i);
        if (core_options_table_get(&g_frontend.core_options, iv->key))
            continue;

        if (!stray_header) {
            fputs("\n# --- Persisted from a previous run; not declared by the "
                  "current core ---\n", fp);
            stray_header = true;
        }
        fprintf(fp, "%s=%s\n", iv->key, iv->value);
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
    if (!variable_table_set(&g_frontend.cli_overrides, key, value)) {
        fprintf(stderr, "Failed to store variable override: %s=%s\n", key, value);
    } else {
        fprintf(stderr, "Variable override (CLI): %s=%s\n", key, value);
    }
}
