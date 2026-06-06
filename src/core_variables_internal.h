/*
 * PureRetro — Internal bridge between core.c and core_variables.c
 * during the A-2 migration. Deleted in Task 4 once all callers use
 * variable_table_*.
 */

#ifndef CORE_VARIABLES_INTERNAL_H
#define CORE_VARIABLES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

bool variable_add(struct retro_variable **vars, size_t *count,
                  size_t *capacity, const char *key, const char *value);
const char *variables_find(const struct retro_variable *vars, size_t count,
                           const char *key);
void variables_sort(struct retro_variable *vars, size_t count);
void variables_free(struct retro_variable **vars, size_t *count);

#endif
