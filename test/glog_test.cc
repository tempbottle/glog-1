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


void StartServer(uint64_t selfid, const std::map<uint64_t, std::string>& groups)
{
    MemStorage storage;
    printf ( "svrid %d storage %p\n", static_cast<int>(selfid), &storage );

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

    grpc::ServerBuilder builder;
    builder.AddListeningPort(
            groups.at(selfid), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<grpc::Server> server(builder.BuildAndStart());
    logdebug("Server %" PRIu64 " listening on %s\n", 
            selfid, groups.at(selfid).c_str());

    server->Wait();
    return ;
}

//int SimplePropose(
//        int index, 
//        uint64_t svrid, 
//        const std::map<uint64_t, std::string>& groups)
//{
//    GlogClientImpl client(svrid, 
//            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));
//
//    string sData("dengos@test.com");
//    int ret = client.Propose({sData.data(), sData.size()});
//    logdebug("try index %d client.Propose svrid %" PRIu64  " ret %d", 
//            index, svrid, ret);
//
//    {
//        string info, data;
//
//        tie(info, data) = client.GetGlog(index);
//        logdebug("svrid %" PRIu64 " index %d info %s data %s", 
//                svrid, index, info.c_str(), data.c_str());
//    }
//
//    {
//        int retcode = 0;
//        uint64_t max_index = 0;
//        uint64_t commited_index = 0;
//        tie(retcode, max_index, commited_index) = client.GetPaxosInfo();
//        logdebug("client.GetPaxosInfo retcode %d max_index %" PRIu64 
//                 " commited_index %" PRIu64, 
//                 retcode, max_index, commited_index);
//    }
//
//    return ret;
//}
//
//void SimpleTryCatchUp(
//        uint64_t svrid, const std::map<uint64_t, std::string>& groups)
//{
//    GlogClientImpl client(svrid, 
//            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));
//    client.TryCatchUp();
//}
//
//void SimpleTryPropose(
//        uint64_t svrid, const std::map<uint64_t, std::string>& groups, 
//        uint64_t index)
//{
//    GlogClientImpl client(svrid, 
//            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));
//    client.TryPropose(index);
//}

int SimpleTestCreateAndQueryLog(
        int times, 
        uint64_t svrid, const std::map<uint64_t, std::string>& groups)
{
    assert(0 < times);

    GlogClientImpl client(svrid, 
            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));

    
    paxos::RandomStrGen<10, 100> str_gen;
    set<string> uniq_logname;
    for (int i = 0; i < times; ++i) {
        string logname = str_gen.Next();
        while (uniq_logname.end() != uniq_logname.find(logname)) {
            logname = str_gen.Next();
        }

        int ret = 0;
        uint64_t logid = 0;
        tie(ret, logid) = client.CreateANewLog(logname);
        hassert(0 == ret, "CreateANewLog logname %s ret %d", logname.c_str(), ret);

        while (true) {
            ret = 0;
            uint64_t ans_logid = 0;
            tie(ret, ans_logid) = client.QueryLogId(logname);
            hassert(0 <= ret, "QueryLogId logname %s ret %d", logname.c_str(), ret);
            if (0 == ret) {
                hassert(logid == ans_logid, "logname %s logid %" PRIu64 
                        " ans_logid %" PRIu64, logname.c_str(), logid, ans_logid);
                break;
            }
            printf ( "INFO logname %s QueryLogId ret %d\n", logname.c_str(), ret );
            usleep(1* 1000);
        }

        printf ( "INFO logname %s logid %" PRIu64 "\n", logname.c_str(), logid );
        uniq_logname.insert(logname);
    }

    return 0;
}

int SimpleSetAndGetTest(
        int times, 
        uint64_t svrid, const std::map<uint64_t, std::string>& groups)
{
    assert(0 < times);
    GlogClientImpl client(svrid, 
            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));

    int ret = 0;
    uint64_t commited_index = 0;
    string commited_value;
    tie(ret, commited_index, commited_value) = client.Get(1ull);
    hassert(0 <= ret, "client.Get 1ull ret %d", ret);

    string test_data;
    {
        paxos::RandomStrGen<100, 200> str_gen;
        test_data = str_gen.Next();
    }
    assert(false == test_data.empty());
    uint64_t max_index = commited_index + times;
    
    printf ( "test_data.size %" PRIu64 " commited_index %" PRIu64 
            " max_index %" PRIu64 "\n", test_data.size(), commited_index, max_index );

    for (uint64_t index = commited_index + 1; index < max_index; ++index) {
        ret = 0;
        commited_index = 0;
        commited_value.clear();

        printf ( "commited_index %" PRIu64 " index %" PRIu64 " times %d\n", 
                commited_index, index, times );
        ret = client.Set(index, {test_data.data(), test_data.size()});
        hassert(0 == ret, "client.Set index %" PRIu64 " ret %d", index, ret);

        tie(ret, commited_index, commited_value) = client.Get(index);
        hassert(0 == ret, "client.Get index %" PRIu64 " ret %d", index, ret);

        hassert(index == commited_index, 
                "index %" PRIu64 " commited_index %" PRIu64, index, commited_index);
        assert(commited_value == test_data);
    }
    return 0;
}

    int
main ( int argc, char *argv[] )
{
    const char* sFileName = "../test/config.example.json";

    Config config(gsl::cstring_view<>{sFileName, strlen(sFileName)});

    auto groups = config.GetGroups();

    vector<future<void>> vec;
    for (auto piter : groups) {
        cout << piter.first << ":" << piter.second << endl;
        auto res = async(launch::async, 
                StartServer, piter.first, cref(groups));
        vec.push_back(move(res));
    }

    // test
    sleep(2);
//    for (int i = 0; i < 1;) {
//        int ret = SimplePropose(i+1, 1ull, groups);
//        printf ( "TEST SimplePropose ret %d\n", ret );
//        if (0 == ret) {
//            ++i;
//            continue;
//        }
//
//        // else
//        usleep(1000);
//    }
//
//    SimpleTryCatchUp(1ull, groups);
//    SimpleTryPropose(1ull, groups, 10ull);

    int test_times = 100;
    auto t = paxos::measure::execution(SimpleTestCreateAndQueryLog, test_times, 1ull, groups);
    cout << "SimpleTestCreateAndQueryLog " << test_times << " times ret " << std::get<0>(t)
         << " cost time " << std::get<1>(t).count() << " ms" << endl;
//    auto t = paxos::measure::execution(SimpleSetAndGetTest, 100, 1ull, groups);
//    cout << "SimpleSetAndGetTest " << 100 << " times ret " << std::get<0>(t)
//         << " cost time " << std::get<1>(t).count() << " ms" << endl;
    for (auto& v : vec) {
        v.get();
    }

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

