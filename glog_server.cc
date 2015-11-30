#include <map>
#include <string>
#include <thread>
#include <future>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <grpc++/grpc++.h>
#include <cassert>
#include "gsl.h"
#include "glog_server_impl.h"
#include "glog_client_impl.h"
#include "config.h"
#include "cqueue.h"
#include "utils.h"
#include "paxos.pb.h"
#include "callback.h"
#include "mem_storage.h"


using namespace std;
using paxos::Message;
using paxos::HardState;
using paxos::Paxos;
using namespace glog;


    int
main ( int argc, char *argv[] )
{
    printf ( "%s config\n", argv[0] );

    assert(2 == argc);
    const char* sConfigFileName = argv[1];

    Config config({sConfigFileName, strlen(sConfigFileName)});

    auto selfid = config.GetSelfId();
    auto groups = config.GetGroups();

    MemStorage storage;

    GlogServiceImpl service{selfid, groups, 
        glog::ReadCallBack<MemStorage>{storage}, 
        glog::WriteCallBack<MemStorage>{storage}};

    grpc::ServerBuilder builder;
    builder.AddListeningPort(
            groups.at(selfid), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<grpc::Server> server(builder.BuildAndStart());

    logdebug("Server %" PRIu64 " listening on %s\n", 
            selfid, groups.at(selfid).c_str());

    server->Wait();
//     f.get();

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

