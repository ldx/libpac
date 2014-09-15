#include "greatest.h"

#include "pac.h"

SUITE(suite);

TEST pac_init_valid_js(void)
{
    char *js = "function FindProxyForURL(u, h) { return \"DIRECT\"; }";

    ASSERT(pac_init(js, 1, NULL, NULL) != NULL);

    PASS();
}

TEST pac_init_invalid_js(void)
{
    char *js = "function FindProxyForURL(u, h) { return \"DIRECT\"; } foo;";

    ASSERT(pac_init(js, 1, NULL, NULL) == NULL);

    PASS();
}

GREATEST_SUITE(suite)
{
    RUN_TEST(pac_init_valid_js);
    RUN_TEST(pac_init_invalid_js);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN(); /* command-line arguments, initialization. */
    RUN_SUITE(suite);
    GREATEST_MAIN_END(); /* display results */
}
