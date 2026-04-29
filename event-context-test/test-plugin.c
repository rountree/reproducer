/*
 * Minimal test plugin to verify flux_shell_add_event_context works
 *
 * This plugin:
 * 1. Registers for shell.init handler
 * 2. Adds test_value=42 to shell.init event context
 * 3. Logs to stderr for verification
 *
 * Expected: test_value=42 should appear in guest.exec.eventlog shell.init line
 */

#define FLUX_SHELL_PLUGIN_NAME "event-context-test"

#include <stdio.h>
#include <flux/core.h>
#include <flux/shell.h>

static int shell_init_handler(flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    (void)topic;
    (void)args;
    (void)data;

    flux_shell_t *shell = flux_plugin_get_shell(p);
    int rank = -1;

    /* Get our shell rank */
    if (flux_shell_info_unpack(shell, "{s:i}", "rank", &rank) < 0) {
        fprintf(stderr, "[ERROR] Failed to get rank\n");
        return -1;
    }

    fprintf(stderr, "[TEST rank=%d] shell.init handler entered\n", rank);

    /* All ranks add context - let's see if it works */
    fprintf(stderr, "[TEST rank=%d] Calling flux_shell_add_event_context...\n", rank);

    if (flux_shell_add_event_context(shell, "shell.init", 0,
                                     "{s:i s:i}",
                                     "test_value", 42,
                                     "test_rank", rank) < 0) {
        fprintf(stderr, "[ERROR rank=%d] flux_shell_add_event_context FAILED: %s\n",
                rank, flux_strerror(errno));
        return -1;
    }

    fprintf(stderr, "[TEST rank=%d] flux_shell_add_event_context returned SUCCESS\n", rank);

    return 0;
}

int flux_plugin_init(flux_plugin_t *p)
{
    fprintf(stderr, "[TEST] Plugin initializing\n");

    if (flux_plugin_add_handler(p, "shell.init", shell_init_handler, NULL) < 0) {
        fprintf(stderr, "[ERROR] flux_plugin_add_handler failed: %s\n",
                flux_strerror(errno));
        return -1;
    }

    fprintf(stderr, "[TEST] Handler registered for shell.init\n");
    return 0;
}
