#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif
#if defined(__APPLE__)
#include <sys/select.h>
#endif

#include "pac.h"

struct pac *pac;

#if defined(_WIN32) || defined(__CYGWIN__)
#define ERR() WSAGetLastError()
#else
#define ERR() errno
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
typedef SOCKET notifier_t;
#else
typedef int notifier_t;
#endif

void do_notify(notifier_t n)
{
    int ret;

#if defined(_WIN32) || defined(__CYGWIN__)
    ret = send(n, "x", 1, 0);
#else
    ret = write(n, "x", 1);
#endif
    if (ret < 0)
        fprintf(stderr, "Error sending notification (error code %d)\n", ERR());
}

int is_notified(notifier_t n, struct timeval *timeout)
{
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(n, &fds);

#if defined(_WIN32) || defined(__CYGWIN__)
    char buf[64];
    if (select(100000, &fds, NULL, NULL, timeout) > 0) {
        recv(n, buf, sizeof(buf), 0);
#else
    unsigned char buf[64];
    if (select(n + 1, &fds, NULL, NULL, timeout) > 0) {
        int rc = read(n, buf, sizeof(buf));
        if (rc < 0) {
            fprintf(stderr, "Warning, notifier socket error\n");
            perror("read()");
        } else if (rc == 0) {
            fprintf(stderr, "Warning, notifier socket EOF\n");
        }
#endif
        return 1;
    } else {
        return 0;
    }
}

/*
 * Based on evutil_socketpair() from libevent.
 */
int create_notifier(notifier_t not[2])
{
#if defined(_WIN32) || defined(__CYGWIN__)
    SOCKET listener = -1, connector = -1, acceptor = -1;
    struct sockaddr_in listen_addr, connect_addr;
    int size;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return -1;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;	/* kernel chooses port.	 */
    if (bind(listener, (struct sockaddr *) &listen_addr, sizeof (listen_addr))
        == -1)
        goto out;
    if (listen(listener, 1) == -1)
        goto out;

    connector = socket(AF_INET, SOCK_STREAM, 0);
    if (connector < 0)
        goto out;

    /* We want to find out the port number to connect to.  */
    size = sizeof(connect_addr);
    if (getsockname(listener, (struct sockaddr *) &connect_addr, &size) == -1)
        goto out;
    if (size != sizeof(connect_addr))
        goto out;
    if (connect(connector, (struct sockaddr *) &connect_addr,
                sizeof(connect_addr)) == -1)
        goto out;

    size = sizeof(listen_addr);
    acceptor = accept(listener, (struct sockaddr *) &listen_addr, &size);
    if (acceptor < 0)
        goto out;
    if (size != sizeof(listen_addr))
        goto out;
    /* Now check we are talking to ourself by matching port and host on the
       two sockets.	 */
    if (getsockname(connector, (struct sockaddr *) &connect_addr, &size) == -1)
        goto out;
    if (size != sizeof(connect_addr)
        || listen_addr.sin_family != connect_addr.sin_family
        || listen_addr.sin_addr.s_addr != connect_addr.sin_addr.s_addr
        || listen_addr.sin_port != connect_addr.sin_port)
        goto out;
    closesocket(listener);
    not[0] = (int)connector;
    not[1] = (int)acceptor;

    return 0;

out:
    if (listener != -1)
        closesocket(listener);
    if (connector != -1)
        closesocket(connector);
    if (acceptor != -1)
        closesocket(acceptor);
    return -1;
#else
    return pipe(not);
#endif
}

static void usage(char *prog)
{
    fprintf(stderr, "Usage: %s <PAC javascript code> <URL> <host> "
            "[<URL> <host> ...]\n", prog);
    fflush(stderr);
    exit(1);
}

static void notify(void *arg)
{
    do_notify((notifier_t)(long)arg);
}

static int finished = 0;

static void proxy_found(char *proxy, void *arg)
{
    printf("Found proxy %s\n", proxy);
    free(proxy);
    finished++;
}

static char *read_pacfile(char *pacfile)
{
    char *js = NULL;
    int fd = open(pacfile, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening file %s\n", pacfile);
        goto err;
    }

    size_t js_sz = 8192, offset = 0;
    js = calloc(1, js_sz);
    if (!js) {
        fprintf(stderr, "Error allocating memory for PAC file\n");
        goto err;
    }
    for (;;) {
        int rc = read(fd, js + offset, js_sz - offset);
        if (rc == 0) {
            break;
        } else if (rc < 0) {
            fprintf(stderr, "Error reading from file %s\n", pacfile);
            perror("read()");
            goto err;
        }

        offset += rc;
        if (offset == js_sz) {
            js_sz *= 2;
            js = realloc(js, js_sz);
            if (!js) {
                fprintf(stderr, "Error allocating memory for PAC file\n");
                goto err;
            }
        }
    }

    close(fd);
    return js;

err:
    if (fd >= 0)
        close(fd);
    if (js)
        free(js);
    return NULL;
}

int main(int argc, char *argv[])
{
    int i, ret = 1;
    char *url, *host, *js;
    struct timeval tv;
    notifier_t n[2];

#if defined(_WIN32) || defined(__CYGWIN__)
    WSADATA wsaData;
    if (WSAStartup(0x202, &wsaData)) {
        fprintf(stderr, "Failed to initialize WSA\n");
        exit(1);
    }
#endif

    if (argc <= 2 || argc % 2 != 0)
        usage(argv[0]);

    js = read_pacfile(argv[1]);
    if (!js)
        goto out;

    if (create_notifier(n) < 0) {
        fprintf(stderr, "Failed to create notifier\n");
        goto out;
    }

    pac = pac_init(js, 4, notify, (void *)(long)n[1]);

    for (i = 2; i < argc; i += 2) {
        url = argv[i];
        host = argv[i + 1];
        pac_find_proxy(pac, url, host, proxy_found, NULL);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (is_notified(n[0], &tv))
            pac_run_callbacks(pac);
    }

    i = 0;
    while (finished < argc / 2 - 1) {
        tv.tv_sec = 0;
        tv.tv_usec = 10000;
        if (is_notified(n[0], &tv))
            pac_run_callbacks(pac);
        if (++i > 60 * 100)
            goto out;
    }

    ret = 0;

out:
    free(js);
    return ret;
}
