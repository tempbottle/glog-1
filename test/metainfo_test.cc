

#include <cstdlib>
#include <cstdio>
#include "glog_metainfo.h"
#include "paxos.h"


using namespace std;
using namespace glog;

    
    int
main ( int argc, char *argv[] )
{
    GlogMetaInfo info{
        [](uint64_t logid, uint64_t index) -> std::unique_ptr<paxos::HardState> {
            printf ( "logid %" PRIu64 " index %" PRIu64 "\n", logid, index );
            return nullptr;

        }, 
        [](uint64_t logid) -> std::unique_ptr<paxos::Paxos> {
            printf ( "logid %" PRIu64 "\n", logid );
            return nullptr;
        }
    };


    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */
