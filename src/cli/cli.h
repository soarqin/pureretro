/*
 * PureRetro — Command-line argument parsing
 *
 * cli_parse() reads argv[] into the passed-in frontend_state (positional
 * <core> [<content>] plus a table of `--flag [arg]` options). All per-flag
 * behavior lives in the dispatch table in cli.c; adding a new flag means
 * adding one handler + one row and nothing else in this header.
 */

#ifndef PURERETRO_CLI_H
#define PURERETRO_CLI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct frontend_state;

/* Parse argv into cfg. Prints usage to stderr on failure or when argc < 2.
 * Returns true only when parsing succeeded and cfg->core_path is set. */
bool cli_parse(int argc, char *argv[], struct frontend_state *cfg);

/* Emit the same usage text cli_parse writes on failure. Exposed for tests. */
void cli_print_usage(const char *argv0);

#ifdef __cplusplus
}
#endif

#endif /* PURERETRO_CLI_H */
