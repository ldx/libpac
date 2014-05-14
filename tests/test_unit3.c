#include "test_helper.h"

#include "greatest.h"

#include "util.h"

SUITE(suite);

TEST test_dns_resolve_one(void)
{
    int ret;
    char buf[256];
    struct in_addr in;
    struct in6_addr in6;

    ret = util_dns_resolve("www.google.com", buf, sizeof(buf), 0);
    ASSERT(ret >= 0);

    ASSERT(inet_pton(AF_INET, buf, &in) == 1 ||
           inet_pton(AF_INET6, buf, &in6) == 1);

    PASS();
}

TEST test_dns_resolve_all(void)
{
    int ret;
    char buf[256], *ip = &buf[0], *needle;
    struct in_addr in;
    struct in6_addr in6;

    ret = util_dns_resolve("www.google.com", buf, sizeof(buf), 1);
    ASSERT(ret >= 0);

    for (;;) {
        needle = strchr(ip, ';');
        if (needle)
            *needle = '\0';
        ASSERT(inet_pton(AF_INET, ip, &in) == 1 ||
               inet_pton(AF_INET6, ip, &in6) == 1);
        if (needle)
            ip = needle + 1;
        else
            break;
    }

    PASS();
}

GREATEST_SUITE(suite)
{
    RUN_TEST(test_dns_resolve_one);
    RUN_TEST(test_dns_resolve_all);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
    init_socket();

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(suite);
    GREATEST_MAIN_END();
}
