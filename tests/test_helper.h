#if defined(_WIN32) || defined(__CYGWIN__)

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>

int init_socket(void)
{
    int err;
    WORD wVersionRequested;
    WSADATA wsaData;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return 1;
    }

    return 0;
}

#else

#include <arpa/inet.h>

int init_socket(void)
{
    return 0;
}

#endif

