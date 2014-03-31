#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "pac.h"

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
        read(n, buf, sizeof(buf));
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
    socklen_t size;

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

static void usage_exit(char *prog)
{
    fprintf(stderr, "Usage: %s <PAC javascript code>\n", prog);
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

int main(int argc, char *argv[])
{
    int i;
    char url[64], host[32], *js;
    struct timeval tv;
    notifier_t n[2];

#if defined(_WIN32) || defined(__CYGWIN__)
    WSADATA wsaData;
    if (WSAStartup(0x202, &wsaData)) {
        fprintf(stderr, "Failed to initialize WSA\n");
        exit(1);
    }
#endif

    if (argc <= 1)
        usage_exit(argv[0]);

    js = argv[1];

    if (create_notifier(n) < 0) {
        fprintf(stderr, "Failed to create notifier\n");
        return 1;
    }

    pac_init(notify, (void *)(long)n[1]);

    for (i = 0; i < 100; i++) {
        snprintf(url, sizeof(url) - 1, "http://google.com/?req=%d", i);
        snprintf(host, sizeof(host) - 1, "google.com");
        pac_find_proxy(js, url, host, proxy_found, NULL);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (is_notified(n[0], &tv))
            pac_run_callbacks();
    }

    while (finished < 100) {
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (is_notified(n[0], &tv))
            pac_run_callbacks();
    }

    sleep(1);

    return 0;
}
