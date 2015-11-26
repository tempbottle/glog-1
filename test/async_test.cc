#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include "async_worker.h"


using namespace std;
using namespace glog;


void TestAsyncWorker(int iTimes, std::atomic<bool>& stop)
{
    while (true) {
        for (int i = 0; i < iTimes; ++i) {
            printf ( "TEST i %d\n", i );
            sleep(1);
        }

        if (true == stop) {
            break;
        }
    }

    printf ( "exist\n" );
    return ;
}


    int
main ( int argc, char *argv[] )
{
    AsyncWorker worker(TestAsyncWorker, 5);

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

