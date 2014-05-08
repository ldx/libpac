#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "duktape.h"
#include "threadpool.h"

#include "nsProxyAutoConfig.h"
#include "util.h"

#include "pac.h"

#define PAC_THREADS 4

static char *javascript;  /* JS code containing FindProxyForURL. */
static pthread_key_t ctx_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static threadpool_t *threadpool;

struct proxy_args {
    char *url;
    char *host;
    void (*cb)(char *, void *);
    char *result;
    void *arg;
};

/*
 * Pluggable logger function. The user can override the default one via
 * pac_set_log_fn().
 */
static void default_log_fn(int level, const char *buf)
{
    if (level == PAC_LOGLVL_WARN)
        fprintf(stderr, "[PAC] %s\n", buf);
}

static log_fn_type log_fn = default_log_fn;

void pac_set_log_fn(log_fn_type fn)
{
    log_fn = fn;
}

#ifdef __GNUC__
#define LOG_ATTR __attribute__((format(printf, 2, 3)))
#else
#define LOG_ATTR
#endif

static void _pac_log(int level, const char *fmt, ...) LOG_ATTR;

static void _pac_log(int level, const char *fmt, ...)
{
    va_list args;
    char buf[1024];

    if (!log_fn)
        return;

    va_start(args,fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    log_fn(level, buf);
}

#define logw(...) do { \
    _pac_log(PAC_LOGLVL_WARN, __VA_ARGS__); \
} while(0)
#define logi(fmt, ...) do { \
    _pac_log(PAC_LOGLVL_INFO, __VA_ARGS__); \
} while(0)
#define logd(fmt, ...) do { \
    _pac_log(PAC_LOGLVL_DEBUG, __VA_ARGS__); \
} while(0)

static void fatal_handler(duk_context *ctx, int code, const char *msg)
{
    logw("Fatal error: %s (%d).", msg, code);
}

static int dns_resolve(duk_context *ctx)
{
    char buf[UTIL_BUFLEN];
    const char *host = duk_require_string(ctx, 0);

    if (util_dns_resolve(host, buf, sizeof(buf), 0) < 0) {
        return -1;
    } else {
        duk_push_string(ctx, buf);
        return 1;
    }
}

static int dns_resolve_ex(duk_context *ctx)
{
    char buf[UTIL_BUFLEN];
    const char *host = duk_require_string(ctx, 0);

    if (util_dns_resolve(host, buf, sizeof(buf), 1) < 0) {
        return -1;
    } else {
        duk_push_string(ctx, buf);
        return 1;
    }
}

static int my_ip_address(duk_context *ctx)
{
    char buf[UTIL_BUFLEN];

    if (util_my_ip_address(buf, sizeof(buf), 0) < 0) {
        return -1;
    } else {
        duk_push_string(ctx, buf);
        return 1;
    }
}

static int my_ip_address_ex(duk_context *ctx)
{
    char buf[UTIL_BUFLEN];

    if (util_my_ip_address(buf, sizeof(buf), 1) < 0) {
        return -1;
    } else {
        duk_push_string(ctx, buf);
        return 1;
    }
}

static void *alloc_ctx(char *js)
{
    duk_context *ctx;

    ctx = duk_create_heap(NULL, NULL, NULL, NULL, fatal_handler);
    if (!ctx)
        return ctx;

    duk_push_global_object(ctx);
    duk_push_c_function(ctx, dns_resolve, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "dnsResolve");
    duk_push_c_function(ctx, my_ip_address, 0 /*nargs*/);
    duk_put_prop_string(ctx, -2, "myIpAddress");
    duk_push_c_function(ctx, dns_resolve_ex, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "dnsResolveEx");
    duk_push_c_function(ctx, my_ip_address_ex, 0 /*nargs*/);
    duk_put_prop_string(ctx, -2, "myIpAddressEx");
    duk_pop(ctx);

    duk_eval_string(ctx, nsProxyAutoConfig);
    duk_pop(ctx);

    duk_eval_string(ctx, nsProxyAutoConfig0);
    duk_pop(ctx);

    /* Try to evaluate our Javascript PAC file. */
    if (duk_peval_string(ctx, js) != 0) {
        logw("Failed to evaluate PAC file: %s.", duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        duk_destroy_heap(ctx);
        errno = EINVAL;
        return NULL;
    }
    duk_pop(ctx);

    return ctx;
}

static void destroy_ctx(void *arg)
{
    duk_context *ctx = arg;

    pthread_setspecific(ctx_key, NULL);

    duk_destroy_heap(ctx);
}

static duk_context *create_ctx(char *js)
{
    duk_context *ctx = pthread_getspecific(ctx_key);

    if (ctx)
        destroy_ctx(ctx);

    ctx = alloc_ctx(js);
    pthread_setspecific(ctx_key, ctx);

    return ctx;
}

static duk_context *get_ctx(void)
{
    duk_context *ctx = pthread_getspecific(ctx_key);

    return ctx;
}

static char *find_proxy(duk_context *ctx, char *url, char *host)
{
    char *result = NULL;
    const char *proxy;

    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1 /*index*/, "FindProxyForURL");
    duk_push_string(ctx, url);
    duk_push_string(ctx, host);

    if (duk_pcall(ctx, 2 /*nargs*/) == DUK_EXEC_SUCCESS) {
        proxy = duk_to_string(ctx, -1);
        if (!proxy)
            logw("Failed to allocate proxy string.");
        else
            result = strdup(proxy);
    } else {
        logw("Javascript call failed: %s.", duk_to_string(ctx, -1));
    }

    duk_pop(ctx); /* Result string. */
    duk_pop(ctx); /* Global object. */

    return result;
}

static void main_result(void *arg)
{
    struct proxy_args *pa = arg;

    pa->cb(pa->result, pa->arg);

    free(pa->host);
    free(pa->url);
    free(pa);
}

static void _pac_find_proxy(void *arg)
{
    struct proxy_args *pa = arg;
    duk_context *ctx = get_ctx();

    if (!ctx)
        ctx = create_ctx(javascript);
    if (ctx) {
        pa->result = find_proxy(ctx, pa->url, pa->host);
    } else {
        logw("Failed to allocate JS context.");
    }

    threadpool_schedule_back(threadpool, main_result, pa);
}

int pac_find_proxy(char *url, char *host,
                   void (*cb)(char *_result, void *_arg), void *arg)
{
    struct proxy_args *pa = malloc(sizeof(struct proxy_args));

    if (!pa) {
        logw("Failed to allocate proxy arguments.");
        return -1;
    }

    pa->url = strdup(url);
    pa->host = strdup(host);
    pa->arg = arg;
    pa->cb = cb;
    pa->result = NULL;

    if (!pa->url || !pa->host) {
        logw("Failed to allocate proxy arguments.");
        return -1;
    }

    if (threadpool_schedule(threadpool, _pac_find_proxy, pa) < 0) {
        logw("Failed to schedule work item.");
        return -1;
    }

    return 0;
}

int pac_find_proxy_sync(char *js, char *url, char *host, char **proxy)
{
    duk_context *ctx = alloc_ctx(js);
    if (ctx) {
        *proxy = find_proxy(ctx, url, host);
        duk_destroy_heap(ctx);
        return 0;
    } else {
        logw("Failed to allocate JS context.");
        return -1;
    }
}

void pac_run_callbacks(void)
{
    threadpool_run_callbacks(threadpool);
}

static void init_key(void)
{
    if (pthread_key_create(&ctx_key, destroy_ctx)) {
        perror("pthread_key_create() failed");
        abort();
    }
}

static int check_js(char *js)
{
    duk_context *ctx = alloc_ctx(js);
    if (!ctx)
        return -1;

    duk_destroy_heap(ctx);

    return 0;
}

int pac_init(char *js, void (*notify_cb)(void *), void *arg)
{
    int ret = check_js(js);
    if (ret)
        return ret;

    if (pthread_once(&key_once, init_key)) {
        perror("Creating thread key");
        return -1;
    }

    javascript = js;

    threadpool = threadpool_create(PAC_THREADS, notify_cb, arg);
    if (!threadpool) {
        logw("Failed to create thread pool.");
        return -1;
    }

    return 0;
}
