/*
 * Comprehensive reproducer for flux-core event watch callback delivery bug
 *
 * This is a nearly complete port of Spindle's flux-spindle.c, with all
 * Flux operations intact but Spindle-specific library interception stubbed out.
 *
 * Goal: Reproduce the 9-22% callback delivery failure rate by replicating
 * all of Spindle's Flux patterns, control flow, and initialization sequence.
 */

#define _GNU_SOURCE
#define FLUX_SHELL_PLUGIN_NAME "event-watch-test"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/hostlist.h>
#include <jansson.h>

static unsigned long seq = 0;

/* ===== Stub Spindle types and constants ===== */

typedef unsigned long number_t;
typedef unsigned long unique_id_t;
typedef unsigned long opt_t;

/* Spindle option flags - from spindle_launch.h */
#define OPT_COBO        (1 << 0)
#define OPT_PERSIST     (1 << 1)
#define OPT_PUSH        (1 << 2)
#define OPT_PULL        (1 << 3)
#define OPT_STRIP       (1 << 4)
#define OPT_NOCLEAN     (1 << 5)
#define OPT_FOLLOWFORK  (1 << 6)
#define OPT_RELOCAOUT   (1 << 7)
#define OPT_RELOCSO     (1 << 8)
#define OPT_RELOCEXEC   (1 << 9)
#define OPT_RELOCPY     (1 << 10)
#define OPT_STOPRELOC   (1 << 11)
#define OPT_SEC_MUNGE   (1 << 12)
#define OPT_OFF         (1 << 13)
#define OPT_NUMA        (1 << 14)

#define SPINDLE_FILLARGS_NONUMBER    (1 << 0)
#define SPINDLE_FILLARGS_NOUNIQUEID  (1 << 1)

/* Stub spindle_args_t structure */
typedef struct {
    number_t number;
    int port;
    int num_ports;
    opt_t opts;
    unique_id_t unique_id;
    int use_launcher;
    int startup_type;
    unsigned long shm_cache_size;
    char *location;
    char *pythonprefix;
    char *preloadfile;
    char *numa_files;
    unsigned long bundle_timeout_ms;
    unsigned long bundle_cachesize_kb;
} spindle_args_t;

/* Spindle context - exact copy from flux-spindle.c */
struct spindle_ctx {
    spindle_args_t params;
    int flags;
    pid_t backend_pid;
    int argc;
    char **argv;
    int shell_rank;
    flux_jobid_t id;
    char **hosts;
};

/* Log file for tracking callback delivery */
static FILE *global_logfile = NULL;

/* ===== Helper functions ported from flux-spindle.c ===== */

#define debug_printf(PRIORITY, FORMAT, ...) \
    do { \
        if (global_logfile) { \
            fprintf(global_logfile, "[DEBUG] " FORMAT, ## __VA_ARGS__); \
            fflush(global_logfile); \
        } \
    } while (0)

#define err_printf(PRIORITY, FORMAT, ...) \
    do { \
        if (global_logfile) { \
            fprintf(global_logfile, "[ERROR] " FORMAT, ## __VA_ARGS__); \
            fflush(global_logfile); \
        } \
    } while (0)

static void free_argv(char **argv)
{
    if (argv) {
        char **s;
        for (s = argv; *s != NULL; s++)
            free(*s);
        free(argv);
    }
}

/* Stub for fluxmgr_get_bootstrap - always returns -2 (not in session mode) */
static int fluxmgr_get_bootstrap(flux_t *h, char **bootstrap_str)
{
    (void)h;
    (void)bootstrap_str;
    debug_printf(1, "Flux sessions are not enabled\n");
    return -2;  /* Not in session mode */
}

static int spindle_in_session_mode(flux_t *flux_handle, int *argc, char ***argv)
{
    char *bootstrap_str = NULL;
    int result;

    result = fluxmgr_get_bootstrap(flux_handle, (argc && argv) ? &bootstrap_str : NULL);
    if (result == -1) {
        err_printf(1, "Could not get bootstrap args from flux\n");
        return -1;
    }
    if (result == -2) {
        debug_printf(1, "Spindle is not running in session mode\n");
        return 0;
    }

    debug_printf(1, "Spindle is in session mode\n");
    /* Session mode handling omitted - not needed for reproducer */
    return 1;
}

/* Convert R to hostlist - exact copy from flux-spindle.c */
static char **R_to_hosts(json_t *R)
{
    struct hostlist *hl = hostlist_create();
    json_t *nodelist;
    size_t index;
    json_t *entry;
    const char *host;
    char **hosts = NULL;
    int i;

    if (json_unpack(R,
                    "{s:{s:o}}",
                    "execution",
                    "nodelist", &nodelist) < 0)
        goto error;

    json_array_foreach(nodelist, index, entry) {
        const char *val = json_string_value(entry);
        if (!val || hostlist_append(hl, val) < 0)
            goto error;
    }
    if (!(hosts = calloc(hostlist_count(hl) + 1, sizeof(char *))))
        goto error;
    host = hostlist_first(hl);
    i = 0;
    while (host) {
        if (!(hosts[i] = strdup(host)))
            goto error;
        host = hostlist_next(hl);
        i++;
    }
    hostlist_destroy(hl);
    return hosts;
error:
    free_argv(hosts);
    hostlist_destroy(hl);
    return NULL;
}

static int spindle_is_enabled(struct spindle_ctx *ctx)
{
    char *spindle_env;

    spindle_env = getenv("SPINDLE");
    if (spindle_env) {
        if (strcasecmp(spindle_env, "false") == 0 || strcmp(spindle_env, "0") == 0) {
            return 0;
        }
    }

    if (ctx->params.opts & OPT_OFF) {
        return 0;
    }

    return 1;
}

/* spindle_ctx_create - exact copy from flux-spindle.c */
static struct spindle_ctx *spindle_ctx_create(flux_jobid_t id,
                                              int rank,
                                              json_t *R)
{
    struct spindle_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->id = id;
    ctx->shell_rank = rank;

    if (!(ctx->hosts = R_to_hosts(R))) {
        free(ctx);
        return NULL;
    }

    /* Derive number and unique_id from jobid */
    ctx->params.number = (number_t)id;
    ctx->params.unique_id = (unique_id_t)id;

    /* Prevent regeneration of unique id and number */
    ctx->flags = SPINDLE_FILLARGS_NONUMBER | SPINDLE_FILLARGS_NOUNIQUEID;

    return ctx;
}

static void spindle_ctx_destroy(struct spindle_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free_argv(ctx->argv);
        free_argv(ctx->hosts);
        free(ctx);
        errno = saved_errno;
    }
}

/* Stub for fillInSpindleArgsCmdlineFE */
static int fillInSpindleArgsCmdlineFE(spindle_args_t *params, int flags,
                                     int argc, char **argv, char **envp)
{
    (void)flags;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Set default options - from Spindle config */
    params->opts = OPT_COBO | OPT_FOLLOWFORK | OPT_PUSH |
                   OPT_RELOCAOUT | OPT_RELOCSO | OPT_RELOCEXEC |
                   OPT_RELOCPY | OPT_STRIP | OPT_PERSIST | OPT_SEC_MUNGE;
    params->port = 21940;
    params->num_ports = 250;
    params->location = "/tmp";
    params->pythonprefix = strdup("/usr/lib/python:/usr/lib64/python");
    params->preloadfile = NULL;

    return 0;
}

/* Stub for getApplicationArgsFE */
static int getApplicationArgsFE(spindle_args_t *params, int *argc, char ***argv)
{
    (void)params;
    /* Return empty argv - no prepending needed for test */
    *argc = 0;
    *argv = calloc(1, sizeof(char *));
    return 0;
}

/* Parse yes/no options */
static int parse_yesno(opt_t *opts, opt_t flag, const char *value)
{
    if (strcasecmp(value, "yes") == 0 || strcmp(value, "1") == 0) {
        *opts |= flag;
        return 0;
    }
    if (strcasecmp(value, "no") == 0 || strcmp(value, "0") == 0) {
        *opts &= ~flag;
        return 0;
    }
    return -1;
}

/* sp_getopts - exact copy from flux-spindle.c */
static int sp_getopts(flux_shell_t *shell, struct spindle_ctx *ctx)
{
    json_error_t error;
    json_t *opts;
    int noclean = 0;
    int nostrip = 0;
    int follow_fork = 0;
    int push = 0;
    int pull = 0;
    int had_error = 0;
    int numa = 0;
    const char *relocaout = NULL, *reloclibs = NULL, *relocexec = NULL, *relocpython = NULL;
    const char *followfork = NULL, *preload = NULL, *level = NULL;
    const char *pyprefix = NULL, *location = NULL;
    char *numafiles = NULL;

    if (flux_shell_getopt_unpack(shell, "spindle", "o", &opts) < 0)
        return -1;

    /* Options we need to be always on */
    ctx->params.opts |= OPT_PERSIST;

    /* spindle=1 is valid if no other options set */
    if (json_is_integer(opts) && json_integer_value(opts) > 0)
        return 0;

    /* Unpack extra spindle options */
    if (json_unpack_ex(opts, &error, JSON_STRICT,
                       "{s?i s?i s?i s?i s?s s?s s?s s?s s?s s?s s?s s?i s?s s?s s?s}",
                       "noclean", &noclean,
                       "nostrip", &nostrip,
                       "push", &push,
                       "pull", &pull,
                       "reloc-aout", &relocaout,
                       "follow-fork", &followfork,
                       "reloc-libs", &reloclibs,
                       "reloc-exec", &relocexec,
                       "reloc-python", &relocpython,
                       "python-prefix", &pyprefix,
                       "location", &location,
                       "numa", &numa,
                       "numa-files", &numafiles,
                       "preload", &preload,
                       "level", &level) < 0) {
        err_printf(1, "Error in spindle option: %s\n", error.text);
        return -1;
    }

    if (noclean)
        ctx->params.opts |= OPT_NOCLEAN;
    if (nostrip)
        ctx->params.opts &= ~OPT_STRIP;
    if (follow_fork)
        ctx->params.opts |= OPT_FOLLOWFORK;
    if (push) {
        ctx->params.opts |= OPT_PUSH;
        ctx->params.opts &= ~OPT_PULL;
    }
    if (pull) {
        ctx->params.opts &= ~OPT_PUSH;
        ctx->params.opts |= OPT_PULL;
    }
    if (relocaout)
        had_error |= parse_yesno(&ctx->params.opts, OPT_RELOCAOUT, relocaout);
    if (followfork)
        had_error |= parse_yesno(&ctx->params.opts, OPT_FOLLOWFORK, followfork);
    if (reloclibs)
        had_error |= parse_yesno(&ctx->params.opts, OPT_RELOCSO, reloclibs);
    if (relocexec)
        had_error |= parse_yesno(&ctx->params.opts, OPT_RELOCEXEC, relocexec);
    if (relocpython)
        had_error |= parse_yesno(&ctx->params.opts, OPT_RELOCPY, relocpython);
    if (preload)
        ctx->params.preloadfile = (char *)preload;
    if (numa) {
        ctx->params.opts |= OPT_NUMA;
    }
    if (numafiles) {
        ctx->params.opts |= OPT_NUMA;
        ctx->params.numa_files = numafiles;
    }
    if (pyprefix) {
        char *tmp;
        if (asprintf(&tmp, "%s:%s", ctx->params.pythonprefix, pyprefix) < 0) {
            err_printf(1, "unable to append to pythonprefix\n");
            return -1;
        }
        free(ctx->params.pythonprefix);
        ctx->params.pythonprefix = tmp;
    }
    if (location) {
        ctx->params.location = (char *)location;
    }
    if (level) {
        if (strcmp(level, "high") == 0) {
            ctx->params.opts |= OPT_RELOCAOUT;
            ctx->params.opts |= OPT_RELOCSO;
            ctx->params.opts |= OPT_RELOCPY;
            ctx->params.opts |= OPT_RELOCEXEC;
            ctx->params.opts |= OPT_FOLLOWFORK;
            ctx->params.opts &= ~((opt_t)OPT_STOPRELOC);
            ctx->params.opts &= ~((opt_t)OPT_OFF);
        }
        if (strcmp(level, "medium") == 0) {
            ctx->params.opts &= ~((opt_t)OPT_RELOCAOUT);
            ctx->params.opts |= OPT_RELOCSO;
            ctx->params.opts |= OPT_RELOCPY;
            ctx->params.opts &= ~((opt_t)OPT_RELOCEXEC);
            ctx->params.opts |= OPT_FOLLOWFORK;
            ctx->params.opts &= ~((opt_t)OPT_STOPRELOC);
            ctx->params.opts &= ~((opt_t)OPT_OFF);
        }
        if (strcmp(level, "low") == 0) {
            ctx->params.opts &= ~((opt_t)OPT_RELOCAOUT);
            ctx->params.opts &= ~((opt_t)OPT_RELOCSO);
            ctx->params.opts &= ~((opt_t)OPT_RELOCPY);
            ctx->params.opts &= ~((opt_t)OPT_RELOCEXEC);
            ctx->params.opts |= OPT_FOLLOWFORK;
            ctx->params.opts |= OPT_STOPRELOC;
            ctx->params.opts &= ~((opt_t)OPT_OFF);
        }
        if (strcmp(level, "off") == 0) {
            ctx->params.opts &= ~((opt_t)OPT_RELOCAOUT);
            ctx->params.opts &= ~((opt_t)OPT_RELOCSO);
            ctx->params.opts &= ~((opt_t)OPT_RELOCPY);
            ctx->params.opts &= ~((opt_t)OPT_RELOCEXEC);
            ctx->params.opts |= OPT_FOLLOWFORK;
            ctx->params.opts &= ~((opt_t)OPT_STOPRELOC);
            ctx->params.opts |= OPT_OFF;
        }
    }
    if (had_error)
        return had_error;
    return 0;
}

/* ===== Core handlers - exact copies from flux-spindle.c ===== */

/* wait_for_shell_init - EXACT copy from Spindle */
static void wait_for_shell_init(flux_future_t *f, void *arg)
{
    struct spindle_ctx *ctx = arg;
    json_t *o;
    const char *event;
    const char *name;
    int rc = -1;

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (ctx->params.opts & OPT_OFF) {
        return;
    }

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (flux_job_event_watch_get(f, &event) < 0) {
        err_printf(1, "spindle failed waiting for shell.init event\n");
        return;
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (!(o = json_loads(event, 0, NULL))
            || json_unpack(o, "{s:s}", "name", &name) < 0) {
        err_printf(1, "failed to get event name\n");
        if (o) json_decref(o);
        return;
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (strcmp(name, "shell.init") == 0) {
        fprintf(stderr, "[RANK %d] Found shell.init event: %s\n", ctx->shell_rank, event);
        rc = json_unpack(o,
                "{s:{s:i s:i}}",
                "context",
                "spindle_port", &ctx->params.port,
                "spindle_num_ports", &ctx->params.num_ports);

        if (rc == 0) {
            /* SUCCESS - callback fired and found shell.init */
            fprintf(stderr, "QQQ %s:%d:%s:%lu - CALLBACK SUCCESS rank=%d port=%d num_ports=%d\n",
                    __FILE__, __LINE__, __func__, seq++, ctx->shell_rank,
                    ctx->params.port, ctx->params.num_ports);
            if (global_logfile) {
                fprintf(global_logfile, "[SUCCESS rank=%d] Found shell.init event with port=%d num_ports=%d - callbacks working!\n",
                        ctx->shell_rank, ctx->params.port, ctx->params.num_ports);
                fflush(global_logfile);
            }
        } else {
            fprintf(stderr, "[RANK %d] json_unpack FAILED (rc=%d) - spindle_port/num_ports not in context\n",
                    ctx->shell_rank, rc);
        }
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    json_decref(o);
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (rc != 0) {
        flux_future_reset(f);
        return;
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    flux_future_destroy(f);

    /* Stub: In real Spindle, this is where backend/frontend would start */
    debug_printf(1, "Would start backend/frontend here (stubbed)\n");
}

/* sp_init - nearly exact copy from flux-spindle.c */
static int sp_init(flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *arg,
                   void *data)
{
    (void)topic;
    (void)arg;
    (void)data;
    struct spindle_ctx *ctx;
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    flux_shell_t *shell = flux_plugin_get_shell(p);
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    flux_t *h = flux_shell_get_flux(shell);
    flux_jobid_t id;
    int shell_rank, rc;
    flux_future_t *f;
    json_t *R;
    const char *debug_env;
    const char *tmpdir;
    const char *test;
    const char *spindle_enabled;

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (!(shell = flux_plugin_get_shell(p))
        || !(h = flux_shell_get_flux(shell))) {
        err_printf(1, "failed to get shell or flux handle\n");
        return -1;
    }

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (flux_shell_getopt(shell, "spindle", NULL) != 1)
        return 0;

    /* Environment variable handling */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if ((debug_env = flux_shell_getenv(shell, "SPINDLE_DEBUG")))
        setenv("SPINDLE_DEBUG", debug_env, 1);

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if ((test = flux_shell_getenv(shell, "SPINDLE_TEST")))
        setenv("SPINDLE_TEST", test, 1);

    debug_printf(1, "initializing spindle flux plugin\n");

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    tmpdir = flux_shell_getenv(shell, "TMPDIR");
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (!tmpdir) {
        tmpdir = "/tmp";
        if (flux_shell_setenvf(shell, 1, "TMPDIR", "%s", tmpdir) < 0) {
            err_printf(1, "failed to set TMPDIR=/tmp in job environment\n");
            return -1;
        }
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    setenv("TMPDIR", tmpdir, 1);

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    spindle_enabled = flux_shell_getenv(shell, "SPINDLE");
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (spindle_enabled)
        setenv("SPINDLE", spindle_enabled, 1);

    /* Get jobid, R, and shell rank */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (flux_shell_info_unpack(shell,
                                "{s:I s:o s:i}",
                                "jobid", &id,
                                "R", &R,
                                "rank", &shell_rank) < 0) {
        err_printf(1, "Failed to unpack shell info\n");
        return -1;
    }

    /* Create context */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (!(ctx = spindle_ctx_create(id, shell_rank, R))
        || flux_plugin_aux_set(p,
                                "spindle",
                                ctx,
                                (flux_free_f)spindle_ctx_destroy) < 0) {
        spindle_ctx_destroy(ctx);
        err_printf(1, "failed to create spindle ctx\n");
        return -1;
    }

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    rc = spindle_in_session_mode(h, NULL, NULL);
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (rc == -1) {
        err_printf(1, "failed to read session info from flux\n");
        spindle_ctx_destroy(ctx);
        return -1;
    }
    else if (rc) {
        /* Session mode does not need to start FE or server */
        return 0;
    }

    /* Fill in spindle args with defaults */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (fillInSpindleArgsCmdlineFE(&ctx->params,
                                   ctx->flags,
                                   0,
                                   NULL,
                                   NULL) < 0) {
        err_printf(1, "fillInSpindleArgsCmdlineFE failed\n");
        return -1;
    }

    /* Read other spindle options */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (sp_getopts(shell, ctx) < 0)
        return -1;
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (ctx->params.opts & OPT_OFF) {
        return 0;
    }

    if (!spindle_is_enabled(ctx)) {
        return 0;
    }

    /* Override unique_id with id again to be sure */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    ctx->params.unique_id = (unique_id_t)id;

    /* Get args to prepend */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (getApplicationArgsFE(&ctx->params, &ctx->argc, &ctx->argv) < 0) {
        err_printf(1, "getApplicationArgsFE failed\n");
        return -1;
    }

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (shell_rank == 0) {
        /* Rank 0: add spindle port and num_ports to shell.init event */
        int rc;
        fprintf(stderr, "[SPINDLE rank=%d] Calling flux_shell_add_event_context with port=%d, num_ports=%d\n",
                shell_rank, ctx->params.port, ctx->params.num_ports);
        rc = flux_shell_add_event_context(shell, "shell.init", 0,
                                          "{s:i s:i}",
                                          "spindle_port",
                                          ctx->params.port,
                                          "spindle_num_ports",
                                          ctx->params.num_ports);
        fprintf(stderr, "[SPINDLE rank=%d] flux_shell_add_event_context returned %d\n",
                shell_rank, rc);
    }

    /* All ranks watch guest.exec.eventlog for shell.init */
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    if (!(f = flux_job_event_watch(h, id, "guest.exec.eventlog", 0))
        || flux_future_then(f, -1., wait_for_shell_init, ctx) < 0) {
        err_printf(1, "flux_job_event_watch failed\n");
        return -1;
    }
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);

    return 0;
}

/* sp_task - nearly exact copy from flux-spindle.c */
static int sp_task(flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *arg,
                   void *data)
{
    (void)topic;
    (void)arg;
    (void)data;
    int session_mode;
    int bootstrap_argc;
    char **bootstrap_argv;
    flux_shell_t *shell;
    flux_t *h;
    int i;

    debug_printf(1, "In flux plugin sp_task\n");
    struct spindle_ctx *ctx = flux_plugin_aux_get(p, "spindle");
    if (!ctx || !spindle_is_enabled(ctx)) {
        return 0;
    }

    if (!(shell = flux_plugin_get_shell(p)) || !(h = flux_shell_get_flux(shell))) {
        err_printf(1, "failed to get shell or flux handle\n");
        return -1;
    }
    flux_shell_task_t *task = flux_shell_current_task(shell);
    flux_cmd_t *cmd = flux_shell_task_cmd(task);

    session_mode = spindle_in_session_mode(h, &bootstrap_argc, &bootstrap_argv);
    if (session_mode == -1) {
        err_printf(1, "Failed to lookup whether we're in session mode\n");
        return -1;
    }
    if (session_mode) {
        debug_printf(1, "Using session settings to run spindle\n");
    }
    else {
        bootstrap_argc = ctx->argc;
        bootstrap_argv = ctx->argv;
    }

    /* Prepend spindle_argv to task cmd */
    for (i = bootstrap_argc - 1; i >= 0; i--)
        flux_cmd_argv_insert(cmd, 0, bootstrap_argv[i]);

    char *s = flux_cmd_stringify(cmd);
    debug_printf(1, "running %s\n", s ? s : "(null)");
    free(s);

    return 0;
}

/* sp_exit - exact copy from flux-spindle.c */
static int sp_exit(flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *arg,
                   void *data)
{
    (void)topic;
    (void)arg;
    (void)data;
    flux_shell_t *shell = flux_plugin_get_shell(p);
    flux_t *h = flux_shell_get_flux(shell);

    debug_printf(1, "In flux plugin sp_exit\n");
    struct spindle_ctx *ctx = flux_plugin_aux_get(p, "spindle");
    if (!ctx || !spindle_is_enabled(ctx))
        return 0;
    if (spindle_in_session_mode(h, NULL, NULL) > 0)
        return 0;
    if (ctx->params.opts & OPT_OFF)
        return 0;

    /* Stub: In real Spindle, this is where frontend/backend would close */
    debug_printf(1, "Would close FE/BE here (stubbed)\n");

    if (global_logfile && global_logfile != stderr) {
        fclose(global_logfile);
        global_logfile = NULL;
    }

    return 0;
}

/* Plugin initialization - exact copy from flux-spindle.c */
int flux_plugin_init(flux_plugin_t *p)
{
    char hostname[256];
    char logpath[512];

    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);

    /* Open log file */
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        snprintf(hostname, sizeof(hostname), "unknown");
    }
    snprintf(logpath, sizeof(logpath), "/tmp/event_watch_output.%s.%d", hostname, getpid());
    global_logfile = fopen(logpath, "w");
    if (!global_logfile) {
        fprintf(stderr, "[ERROR] Failed to open log file %s\n", logpath);
        global_logfile = stderr;
    }

    if (flux_plugin_set_name(p, "spindle") < 0
        || flux_plugin_add_handler(p, "shell.init", sp_init, NULL) < 0
        || flux_plugin_add_handler(p, "task.init", sp_task, NULL) < 0
        || flux_plugin_add_handler(p, "shell.exit", sp_exit, NULL) < 0)
        return -1;
    fprintf(stderr, "QQQ %s:%d:%s:%lu\n", __FILE__, __LINE__, __func__, seq++);
    return 0;
}
