/*
 * PureRetro — Core option and variable environment callbacks
 */

#include "core_internal.h"

#include "core_variables_parse.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static bool add_options_from_v1_defs(struct frontend_state *fe,
                                     const struct retro_core_option_definition *defs)
{
    bool ok = true;
    for (const struct retro_core_option_definition *def = defs; def && def->key; ++def) {
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
            if (!def->values[i].value)
                break;
            values[val_count++] = def->values[i].value;
        }
        values[val_count] = NULL;
        if (!core_options_table_add(&fe->core_options,
                                    def->key, def->desc, def->info,
                                    values, def->default_value)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static bool add_options_from_v2_defs(struct frontend_state *fe,
                                     const struct retro_core_option_v2_definition *defs)
{
    bool ok = true;
    for (const struct retro_core_option_v2_definition *def = defs; def && def->key; ++def) {
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
            if (!def->values[i].value)
                break;
            values[val_count++] = def->values[i].value;
        }
        values[val_count] = NULL;
        if (!core_options_table_add(&fe->core_options,
                                    def->key, def->desc, def->info,
                                    values, def->default_value)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static size_t seed_disk_overrides_from_defaults(struct frontend_state *fe)
{
    size_t seeded = 0;
    size_t total = core_options_table_count(&fe->core_options);
    for (size_t i = 0; i < total; ++i) {
        const struct core_option *opt =
            core_options_table_at(&fe->core_options, i);
        if (variable_table_get(&fe->disk_overrides, opt->key))
            continue;
        if (variable_table_set(&fe->disk_overrides, opt->key, opt->default_value))
            seeded++;
    }
    return seeded;
}


bool env_get_variable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_variable: NULL data"); return false; }
    struct retro_variable *var = (struct retro_variable *)data;
    if (!var->key)
        return false;

    const char *override = variable_table_get(&fe->cli_overrides,
                                              var->key);
    if (!override)
        override = variable_table_get(&fe->disk_overrides, var->key);
    if (override) {
        var->value = override;
        return true;
    }

    const struct core_option *opt =
        core_options_table_get(&fe->core_options, var->key);
    if (!opt)
        return false;

    var->value = opt->current_value ? opt->current_value : opt->default_value;
    return true;
}

bool env_set_variable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_variable: NULL data"); return false; }
    const struct retro_variable *var = (const struct retro_variable *)data;
    if (!var->key || !var->value)
        return false;
    if (!core_options_table_set_value(&fe->core_options,
                                      var->key, var->value))
        return false;
    if (!variable_table_set(&fe->disk_overrides, var->key, var->value))
        return false;
    return true;
}

bool env_set_variables(struct frontend_state *fe, void *data)
{
    const struct retro_variable *vars = (const struct retro_variable *)data;
    if (!vars)
        return false;

    core_options_table_clear(&fe->core_options);

    bool ok = true;
    for (const struct retro_variable *v = vars; v && v->key; ++v) {
        char desc[256];
        core_var_parse_description(v->value, desc, sizeof(desc));

        char def[256];
        core_var_parse_default(v->value, def, sizeof(def));

        const char *choices = core_var_choices_begin(v->value);
        /* libretro spec caps choices at RETRO_NUM_CORE_OPTION_VALUES_MAX
         * (128). Allocate one extra slot for the trailing NULL sentinel
         * required by core_options_table_add. */
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;

        if (choices) {
            const char *p = choices;
            while (*p) {
                if (val_count >= RETRO_NUM_CORE_OPTION_VALUES_MAX) {
                    ok = false;
                    break;
                }
                const char *end = p;
                while (*end && *end != '|')
                    ++end;
                size_t len = (size_t)(end - p);
                char *choice = malloc(len + 1);
                if (!choice) {
                    ok = false;
                    break;
                }
                memcpy(choice, p, len);
                choice[len] = '\0';
                values[val_count++] = choice;
                if (*end == '|')
                    ++end;
                p = end;
            }
        }

        if (!ok) {
            for (size_t i = 0; i < val_count; ++i)
                free((char *)values[i]);
            break;
        }
        values[val_count] = NULL;

        if (!core_options_table_add(&fe->core_options,
                                    v->key, desc, NULL, values, def)) {
            ok = false;
        }

        for (size_t i = 0; i < val_count; ++i)
            free((char *)values[i]);

        if (!ok)
            break;
    }

    if (!ok)
        core_options_table_clear(&fe->core_options);

    size_t seeded = 0;
    size_t total = core_options_table_count(&fe->core_options);
    for (size_t i = 0; i < total; ++i) {
        const struct core_option *opt =
            core_options_table_at(&fe->core_options, i);
        if (variable_table_get(&fe->disk_overrides, opt->key))
            continue;
        if (variable_table_set(&fe->disk_overrides,
                               opt->key, opt->default_value))
            seeded++;
    }

    LOG_INFO("Core registered %zu variables (%zu seeded from defaults)",
             total, seeded);
    return ok;
}

bool env_set_core_options(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options: NULL data"); return false; }
    const struct retro_core_option_definition *defs =
        (const struct retro_core_option_definition *)data;
    core_options_table_clear(&fe->core_options);
    bool ok = add_options_from_v1_defs(fe, defs);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults(fe);
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

bool env_set_core_options_intl(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_intl: NULL data"); return false; }
    const struct retro_core_options_intl *opts =
        (const struct retro_core_options_intl *)data;
    if (opts->local && opts->local != opts->us) {
        LOG_WARN("SET_CORE_OPTIONS_INTL: ignoring localized definitions, "
                 "only the US variant is consumed");
    }
    core_options_table_clear(&fe->core_options);
    bool ok = true;
    if (opts->us)
        ok = add_options_from_v1_defs(fe, opts->us);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults(fe);
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

bool env_set_core_options_v2(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_v2: NULL data"); return false; }
    const struct retro_core_options_v2 *opts =
        (const struct retro_core_options_v2 *)data;
    core_options_table_clear(&fe->core_options);
    bool ok = add_options_from_v2_defs(fe, opts->definitions);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults(fe);
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

bool env_set_core_options_v2_intl(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_v2_intl: NULL data"); return false; }
    const struct retro_core_options_v2_intl *opts =
        (const struct retro_core_options_v2_intl *)data;
    if (opts->local && opts->local != opts->us) {
        LOG_WARN("SET_CORE_OPTIONS_V2_INTL: ignoring localized definitions, "
                 "only the US variant is consumed");
    }
    core_options_table_clear(&fe->core_options);
    bool ok = true;
    if (opts->us)
        ok = add_options_from_v2_defs(fe, opts->us->definitions);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults(fe);
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

bool env_set_core_options_display(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_display: NULL data"); return false; }
    const struct retro_core_option_display *disp =
        (const struct retro_core_option_display *)data;
    if (!disp->key)
        return false;
    if (!core_options_table_set_visible(&fe->core_options,
                                        disp->key, disp->visible)) {
        /* The core may toggle visibility for an option it has not yet
         * declared (e.g. during a multi-stage SET_VARIABLES sequence).
         * Return false so the core knows we did not record it. */
        return false;
    }
    return true;
}

