/*
 * Minimal reproducer for flux-core event watch callback delivery bug
 *
 * This plugin registers an event watch on guest.exec.eventlog and logs
 * when callbacks fire. In flux-core v0.82.0, callbacks silently fail to
 * fire on ~9-22% of nodes, even though events are present in the KVS.
 */

#define FLUX_SHELL_PLUGIN_NAME "event-watch-test"

#include <stdio.h>
#include <flux/core.h>
#include <flux/shell.h>

static unsigned long seq = 0;

/* Context passed to callback */
struct watch_ctx {
    flux_t *h;
    flux_shell_t *shell;
    int rank;
    flux_jobid_t jobid;
    flux_future_t *watch_future;
    int success;
};

/* Callback invoked for each event in guest.exec.eventlog */
static void event_watch_callback(flux_future_t *f, void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *event;

    fprintf(stderr, "[QQQ rank=%d seq=%lu] %s:%d:%s - Callback fired!\n",
            ctx->rank, seq++, __FILE__, __LINE__, __func__);

    /* Get the event from the future */
    if (flux_job_event_watch_get(f, &event) < 0) {
        fprintf(stderr, "[ERROR rank=%d] flux_job_event_watch_get: %s\n",
                ctx->rank, flux_strerror(errno));
        return;
    }

    /* Log the event we received */
    fprintf(stderr, "[EVENT rank=%d] Received event: %s\n", ctx->rank, event);

    /* Check if this is the shell.init event we're waiting for */
    if (strstr(event, "\"name\":\"shell.init\"") != NULL) {
        fprintf(stderr, "[SUCCESS rank=%d] Found shell.init event - callbacks working!\n",
                ctx->rank);
        ctx->success = 1;

        /* Cancel the watch - we're done */
        flux_future_destroy(f);
        ctx->watch_future = NULL;
        return;
    }

    /* Reset the future to wait for the next event */
    flux_future_reset(f);
}

/* Called during shell.init phase */
static int shell_init_callback(flux_plugin_t *p,
                               const char *topic __attribute__((unused)),
                               flux_plugin_arg_t *args __attribute__((unused)),
                               void *data __attribute__((unused)))
{
    flux_shell_t *shell = flux_plugin_get_shell(p);
    flux_t *h = NULL;
    flux_jobid_t jobid;
    int rank;
    struct watch_ctx *ctx = NULL;

    fprintf(stderr, "[QQQ seq=%lu] %s:%d:%s - Entered\n",
            seq++, __FILE__, __LINE__, __func__);

    /* Get flux handle */
    if (!(h = flux_shell_get_flux(shell))) {
        fprintf(stderr, "[ERROR] flux_shell_get_flux failed\n");
        return -1;
    }

    /* Get shell rank */
    if (flux_shell_rank_info_unpack(shell, -1, "rank", &rank) < 0) {
        fprintf(stderr, "[ERROR] flux_shell_rank_info_unpack failed\n");
        return -1;
    }

    /* Get job ID */
    if (flux_shell_info_unpack(shell, "{s:I}", "jobid", &jobid) < 0) {
        fprintf(stderr, "[ERROR] flux_shell_info_unpack failed\n");
        return -1;
    }

    fprintf(stderr, "[INIT rank=%d jobid=%ju] Registering event watch\n",
            rank, (uintmax_t)jobid);

    /* Allocate context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "[ERROR rank=%d] calloc failed\n", rank);
        return -1;
    }
    ctx->h = h;
    ctx->shell = shell;
    ctx->rank = rank;
    ctx->jobid = jobid;
    ctx->success = 0;

    /* Register event watch on guest.exec.eventlog */
    fprintf(stderr, "[QQQ rank=%d seq=%lu] %s:%d - About to call flux_job_event_watch\n",
            rank, seq++, __FILE__, __LINE__);

    ctx->watch_future = flux_job_event_watch(h, jobid, "guest.exec.eventlog", 0);
    if (!ctx->watch_future) {
        fprintf(stderr, "[ERROR rank=%d] flux_job_event_watch failed: %s\n",
                rank, flux_strerror(errno));
        free(ctx);
        return -1;
    }

    fprintf(stderr, "[QQQ rank=%d seq=%lu] %s:%d - flux_job_event_watch succeeded\n",
            rank, seq++, __FILE__, __LINE__);

    /* Register callback */
    if (flux_future_then(ctx->watch_future, -1.0, event_watch_callback, ctx) < 0) {
        fprintf(stderr, "[ERROR rank=%d] flux_future_then failed: %s\n",
                rank, flux_strerror(errno));
        flux_future_destroy(ctx->watch_future);
        free(ctx);
        return -1;
    }

    fprintf(stderr, "[QQQ rank=%d seq=%lu] %s:%d - flux_future_then succeeded, callback registered\n",
            rank, seq++, __FILE__, __LINE__);
    fprintf(stderr, "[REGISTERED rank=%d] Event watch and callback registered successfully\n",
            rank);

    /* Store context in plugin aux for cleanup */
    flux_plugin_aux_set(p, "watch_ctx", ctx, free);

    return 0;
}

/* Plugin initialization */
int flux_plugin_init(flux_plugin_t *p)
{
    fprintf(stderr, "[QQQ seq=%lu] %s:%d:%s - Plugin initializing\n",
            seq++, __FILE__, __LINE__, __func__);

    /* Register callback for shell.init phase */
    if (flux_plugin_add_handler(p, "shell.init", shell_init_callback, NULL) < 0) {
        fprintf(stderr, "[ERROR] flux_plugin_add_handler failed: %s\n",
                flux_strerror(errno));
        return -1;
    }

    fprintf(stderr, "[QQQ seq=%lu] %s:%d:%s - Handler registered\n",
            seq++, __FILE__, __LINE__, __func__);

    return 0;
}
