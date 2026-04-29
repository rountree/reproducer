/*
 * Reproducer for flux-core event watch callback delivery bug
 *
 * This plugin mimics Spindle's Flux operations to reproduce the bug where
 * callbacks silently fail to fire on ~9-22% of nodes in flux-core v0.82.0.
 *
 * Key operations ported from Spindle's flux-spindle.c:
 * - JSON parsing of events with jansson
 * - flux_shell_add_event_context on rank 0
 * - task.init handler for command manipulation
 * - flux_future_reset for non-matching events
 * - Environment variable handling
 */

#define FLUX_SHELL_PLUGIN_NAME "event-watch-test"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <jansson.h>

static unsigned long seq = 0;

/* Context passed to callback */
struct watch_ctx {
    flux_t *h;
    flux_shell_t *shell;
    flux_plugin_t *plugin;
    int rank;
    flux_jobid_t jobid;
    flux_future_t *watch_future;
    int success;
    FILE *logfile;
    int test_value;  /* Value set by rank 0 via flux_shell_add_event_context */
};

/* Event watch callback - ported from Spindle's wait_for_shell_init */
static void event_watch_callback(flux_future_t *f, void *arg)
{
    struct watch_ctx *ctx = arg;
    json_t *o = NULL;
    const char *event;
    const char *name;
    int rc = -1;
    FILE *log = ctx->logfile ? ctx->logfile : stderr;

    fprintf(log, "[QQQ rank=%d seq=%lu] %s:%d:%s - Callback fired!\n",
            ctx->rank, seq++, __FILE__, __LINE__, __func__);
    fflush(log);

    /* Get the event from the future - Spindle pattern */
    if (flux_job_event_watch_get(f, &event) < 0) {
        fprintf(log, "[ERROR rank=%d] flux_job_event_watch_get: %s\n",
                ctx->rank, flux_strerror(errno));
        fflush(log);
        return;
    }

    fprintf(log, "[EVENT rank=%d] Received event: %s\n", ctx->rank, event);
    fflush(log);

    /* Parse JSON with jansson - Spindle pattern */
    if (!(o = json_loads(event, 0, NULL))
            || json_unpack(o, "{s:s}", "name", &name) < 0) {
        fprintf(log, "[ERROR rank=%d] failed to parse event JSON\n", ctx->rank);
        fflush(log);
        if (o) json_decref(o);
        return;
    }

    fprintf(log, "[PARSED rank=%d] Event name: %s\n", ctx->rank, name);
    fflush(log);

    /* Check if this is the shell.init event - Spindle pattern */
    if (strcmp(name, "shell.init") == 0) {
        /* Try to unpack test_value from context - Spindle pattern for port/num_ports */
        rc = json_unpack(o,
                "{s:{s:i}}",
                "context",
                "test_value", &ctx->test_value);

        if (rc == 0) {
            fprintf(log, "[SUCCESS rank=%d] Found shell.init event with test_value=%d - callbacks working!\n",
                    ctx->rank, ctx->test_value);
            fflush(log);
            ctx->success = 1;

            /* Clean up and destroy future - Spindle pattern */
            json_decref(o);
            flux_future_destroy(f);
            ctx->watch_future = NULL;
            return;
        }
    }

    /* Not the event we want, reset and wait for more - Spindle pattern */
    json_decref(o);
    flux_future_reset(f);
}

/* Task init handler - ported from Spindle's sp_task */
static int task_init_callback(flux_plugin_t *p,
                              const char *topic __attribute__((unused)),
                              flux_plugin_arg_t *args __attribute__((unused)),
                              void *data __attribute__((unused)))
{
    struct watch_ctx *ctx = flux_plugin_aux_get(p, "watch_ctx");
    flux_shell_t *shell;
    flux_shell_task_t *task;
    flux_cmd_t *cmd;
    FILE *log = ctx ? ctx->logfile : stderr;

    if (!ctx) {
        return 0;
    }

    fprintf(log, "[QQQ rank=%d seq=%lu] %s:%d:%s - task.init\n",
            ctx->rank, seq++, __FILE__, __LINE__, __func__);
    fflush(log);

    /* Get current task and command - Spindle pattern */
    if (!(shell = flux_plugin_get_shell(p))) {
        fprintf(log, "[ERROR rank=%d] flux_plugin_get_shell failed\n", ctx->rank);
        fflush(log);
        return -1;
    }

    task = flux_shell_current_task(shell);
    cmd = flux_shell_task_cmd(task);

    /* Log the command - Spindle uses flux_cmd_stringify */
    char *cmdstr = flux_cmd_stringify(cmd);
    fprintf(log, "[TASK rank=%d] Task command: %s\n", ctx->rank, cmdstr ? cmdstr : "(null)");
    fflush(log);
    free(cmdstr);

    return 0;
}

/* Shell init handler - ported from Spindle's sp_init */
static int shell_init_callback(flux_plugin_t *p,
                               const char *topic __attribute__((unused)),
                               flux_plugin_arg_t *args __attribute__((unused)),
                               void *data __attribute__((unused)))
{
    flux_shell_t *shell = flux_plugin_get_shell(p);
    flux_t *h = NULL;
    flux_jobid_t jobid;
    int rank;
    json_t *R = NULL;
    struct watch_ctx *ctx = NULL;
    char hostname[256];
    char logpath[512];
    FILE *logfile = NULL;
    flux_future_t *f = NULL;
    const char *tmpdir;

    /* Open log file */
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        snprintf(hostname, sizeof(hostname), "unknown");
    }
    snprintf(logpath, sizeof(logpath), "/tmp/event_watch_output.%s.%d", hostname, getpid());

    logfile = fopen(logpath, "w");
    if (!logfile) {
        fprintf(stderr, "[ERROR] Failed to open log file %s, using stderr\n", logpath);
        logfile = stderr;
    }

    fprintf(logfile, "[QQQ seq=%lu] %s:%d:%s - Entered\n",
            seq++, __FILE__, __LINE__, __func__);
    fflush(logfile);

    /* Get flux handle - Spindle pattern */
    if (!(shell = flux_plugin_get_shell(p))
        || !(h = flux_shell_get_flux(shell))) {
        fprintf(logfile, "[ERROR] failed to get shell or flux handle\n");
        fflush(logfile);
        if (logfile != stderr) fclose(logfile);
        return -1;
    }

    /* Environment variable handling - Spindle pattern */
    tmpdir = flux_shell_getenv(shell, "TMPDIR");
    if (!tmpdir) {
        tmpdir = "/tmp";
        if (flux_shell_setenvf(shell, 1, "TMPDIR", "%s", tmpdir) < 0) {
            fprintf(logfile, "[ERROR] failed to set TMPDIR=/tmp\n");
            fflush(logfile);
            if (logfile != stderr) fclose(logfile);
            return -1;
        }
    }
    setenv("TMPDIR", tmpdir, 1);

    /* Get jobid, R, and shell rank - Spindle pattern */
    if (flux_shell_info_unpack(shell,
                                "{s:I s:o s:i}",
                                "jobid", &jobid,
                                "R", &R,
                                "rank", &rank) < 0) {
        fprintf(logfile, "[ERROR] flux_shell_info_unpack failed\n");
        fflush(logfile);
        if (logfile != stderr) fclose(logfile);
        return -1;
    }

    fprintf(logfile, "[INIT rank=%d jobid=%ju] Registering event watch\n",
            rank, (uintmax_t)jobid);
    fflush(logfile);

    /* Allocate context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(logfile, "[ERROR rank=%d] calloc failed\n", rank);
        fflush(logfile);
        if (logfile != stderr) fclose(logfile);
        return -1;
    }
    ctx->h = h;
    ctx->shell = shell;
    ctx->plugin = p;
    ctx->rank = rank;
    ctx->jobid = jobid;
    ctx->success = 0;
    ctx->logfile = logfile;
    ctx->test_value = -1;

    /* Rank 0: add test_value to shell.init event context - Spindle pattern */
    if (rank == 0) {
        fprintf(logfile, "[QQQ rank=%d seq=%lu] Adding event context to shell.init\n",
                rank, seq++);
        fflush(logfile);

        flux_shell_add_event_context(shell, "shell.init", 0,
                                     "{s:i}",
                                     "test_value",
                                     42);
    }

    /* All ranks watch guest.exec.eventlog - Spindle pattern */
    fprintf(logfile, "[QQQ rank=%d seq=%lu] %s:%d - About to call flux_job_event_watch\n",
            rank, seq++, __FILE__, __LINE__);
    fflush(logfile);

    if (!(f = flux_job_event_watch(h, jobid, "guest.exec.eventlog", 0))
        || flux_future_then(f, -1., event_watch_callback, ctx) < 0) {
        fprintf(logfile, "[ERROR rank=%d] flux_job_event_watch failed\n", rank);
        fflush(logfile);
        if (logfile != stderr) fclose(logfile);
        free(ctx);
        return -1;
    }

    ctx->watch_future = f;

    fprintf(logfile, "[QQQ rank=%d seq=%lu] %s:%d - flux_future_then succeeded, callback registered\n",
            rank, seq++, __FILE__, __LINE__);
    fprintf(logfile, "[REGISTERED rank=%d] Event watch and callback registered successfully\n",
            rank);
    fflush(logfile);

    /* Store context in plugin aux for cleanup - Spindle pattern */
    flux_plugin_aux_set(p, "watch_ctx", ctx, free);

    return 0;
}

/* Cleanup handler - ported from Spindle's sp_exit */
static int shell_exit_callback(flux_plugin_t *p,
                                const char *topic __attribute__((unused)),
                                flux_plugin_arg_t *args __attribute__((unused)),
                                void *data __attribute__((unused)))
{
    struct watch_ctx *ctx = flux_plugin_aux_get(p, "watch_ctx");
    if (ctx && ctx->watch_future) {
        flux_future_destroy(ctx->watch_future);
        ctx->watch_future = NULL;
    }
    if (ctx && ctx->logfile && ctx->logfile != stderr) {
        fclose(ctx->logfile);
        ctx->logfile = NULL;
    }
    return 0;
}

/* Plugin initialization */
int flux_plugin_init(flux_plugin_t *p)
{
    fprintf(stderr, "[QQQ seq=%lu] %s:%d:%s - Plugin initializing\n",
            seq++, __FILE__, __LINE__, __func__);

    /* Register all handlers - Spindle pattern */
    if (flux_plugin_add_handler(p, "shell.init", shell_init_callback, NULL) < 0) {
        fprintf(stderr, "[ERROR] flux_plugin_add_handler(shell.init) failed: %s\n",
                flux_strerror(errno));
        return -1;
    }

    if (flux_plugin_add_handler(p, "task.init", task_init_callback, NULL) < 0) {
        fprintf(stderr, "[ERROR] flux_plugin_add_handler(task.init) failed: %s\n",
                flux_strerror(errno));
        return -1;
    }

    if (flux_plugin_add_handler(p, "shell.exit", shell_exit_callback, NULL) < 0) {
        fprintf(stderr, "[ERROR] flux_plugin_add_handler(shell.exit) failed: %s\n",
                flux_strerror(errno));
        return -1;
    }

    fprintf(stderr, "[QQQ seq=%lu] %s:%d:%s - All handlers registered\n",
            seq++, __FILE__, __LINE__, __func__);

    return 0;
}
