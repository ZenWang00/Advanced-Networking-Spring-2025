#include "unity_fixture.h"
#include <signal.h>
#include <stdio.h>

static void run_tests(void)
{
    RUN_TEST_GROUP(bencode);
    RUN_TEST_GROUP(metainfo);
    RUN_TEST_GROUP(client);
    RUN_TEST_GROUP(handshake);
    RUN_TEST_GROUP(listen_peers);
    RUN_TEST_GROUP(tracker);
}

int main(int argc, const char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    
    return UnityMain(argc, argv, run_tests);
}
