#include <unistd.h>
#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <future>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <grpc++/grpc++.h>
#include <cstring>
#include <cassert>
#include <stdint.h>
#include "gsl.h"
#include "config.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "utils.h"
#include "cqueue.h"
#include "glog_server_impl.h"
#include "glog_client_impl.h"
#include "mem_storage.h"
#include "callback.h"


using namespace std;
using namespace paxos;
using namespace glog;



    int
main ( int argc, char *argv[] )
{
    const char* sFileName = "../test/config.example.json";

    Config config(gsl::cstring_view<>{sFileName, strlen(sFileName)});

    auto groups = config.GetGroups();
    MemStorage storage;
    uint64_t selfid = 1ull;

    GlogServiceImpl service{selfid, groups, 
        [&storage, selfid](uint64_t logid, uint64_t index) 
            -> std::unique_ptr<paxos::HardState> {
            auto hs = storage.Get(logid, index);
            printf ( "TESTINFO: GET hs %p selfid %" PRIu64 " logid %" PRIu64 " index %" PRIu64 "\n", 
                    hs.get(), selfid, logid, index);
            return hs;
        }, 
        [&storage, selfid](const paxos::HardState& hs) -> int { 
            int ret = storage.Set(hs);
            printf ( "TESTINFO: SET hs %p selfid %" PRIu64 " logid %" PRIu64 " index %" PRIu64 "\n", 
                    &hs, selfid, hs.logid(), hs.index());
            return ret;
        }};


    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */
