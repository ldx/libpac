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

static void fatal_handler(duk_context *ctx, int code, const char *msg)
{
    fprintf(stderr, "Fatal error %s (%d)\n", msg, code);
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

    duk_eval_string(ctx, js);
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
            fprintf(stderr, "Error allocating proxy string\n");
        else
            result = strdup(proxy);
    } else {
        fprintf(stderr, "Error: %s\n", duk_to_string(ctx, -1));
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
        fprintf(stderr, "Failed to allocated JS context\n");
    }

    threadpool_schedule_back(threadpool, main_result, pa);
}

int pac_find_proxy(char *url, char *host,
                   void (*cb)(char *_result, void *_arg), void *arg)
{
    struct proxy_args *pa = malloc(sizeof(struct proxy_args));

    if (!pa) {
        fprintf(stderr, "Failed to allocate proxy args\n");
        return -1;
    }

    pa->url = strdup(url);
    pa->host = strdup(host);
    pa->arg = arg;
    pa->cb = cb;
    pa->result = NULL;

    if (!pa->url || !pa->host) {
        fprintf(stderr, "Failed to allocate proxy args\n");
        return -1;
    }

    if (threadpool_schedule(threadpool, _pac_find_proxy, pa) < 0) {
        fprintf(stderr, "Failed to schedule work item\n");
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
        fprintf(stderr, "Failed to allocated JS context\n");
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

int pac_init(char *js, void (*notify_cb)(void *), void *arg)
{
    if (pthread_once(&key_once, init_key)) {
        perror("Creating thread key");
        return -1;
    }

    javascript = js;

    threadpool = threadpool_create(PAC_THREADS, notify_cb, arg);
    if (!threadpool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return -1;
    }

    return 0;
}
