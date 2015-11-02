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

//int single_call(
//        const std::map<uint64_t, std::string>& groups, const Message& msg) 
//{
//    assert(0 != msg.to_id());
//    assert(0 != msg.peer_id());
//
//    auto svrid = msg.to_id();
//
//    glog::GlogClientImpl client(svrid, 
//            grpc::CreateChannel(groups.at(svrid), grpc::InsecureCredentials()));
//    client.PostMsg(msg);
//    logdebug("single_call PostMsg svrid %" PRIu64 
//            " msg type %d", svrid, static_cast<int>(msg.type()));
//    return 0;
//}
//
//int RPCWorker(
//        uint64_t selfid, 
//        const std::map<uint64_t, std::string>& groups, 
//        CQueue<std::unique_ptr<paxos::Message>>& queue)
//{
//    while (true) {
//        unique_ptr<Message> msg = queue.Pop();
//
//        // deal with the msg;
//        assert(nullptr != msg);
//        assert(selfid == msg->peer_id());
//        assert(0 < msg->index());
//
//        logdebug("TEST index %" PRIu64 " msgtype %d to_id %" PRIu64 " peer_id %" PRIu64 " ", 
//                msg->index(), static_cast<int>(msg->type()),
//                msg->to_id(), msg->peer_id());
//
//        int ret = 0;
//        if (0 != msg->to_id()) {
//            ret = single_call(groups, *msg);
//        } else {
//            // broadcast
//            for (auto& piter : groups) {
//                if (selfid == piter.first) {
//                    continue;
//                }
//
//                msg->set_to_id(piter.first);
//                ret = single_call(groups, *msg);
//            }
//        }
//    }
//}
//
//
//class CallBack {
//
//public:
//    CallBack(uint64_t selfid, 
//            const std::map<uint64_t, std::string>& groups)
//        : selfid_(selfid)
//        , groups_(groups)
//        , queue_(make_shared<CQueue<unique_ptr<Message>>>())
//        , rpc_worker_(make_shared<thread>(RPCWorker, selfid_, groups_, ref(*queue_)))
//    {
//        assert(0 < selfid_);
//        assert(groups_.end() != groups_.find(selfid_));
//        assert(nullptr != queue_);
//        assert(nullptr != rpc_worker_);
//        rpc_worker_->detach();
//    }
//
//    int operator()(
//            std::unique_ptr<HardState> hs, 
//            std::unique_ptr<Message> msg)
//    {
//        if (nullptr != hs) {
//            assert(0 < hs->index());
//            logdebug("TEST index %" PRIu64 " store hs", hs->index());
//        }
//
//        if (nullptr != msg) {
//            queue_->Push(move(msg));
//        }
//
//        logdebug("TEST index %" PRIu64 " hs %p msg %p", 
//                index, hs.get(), msg.get());
//        return 0;
//    }
//
//private:
//    uint64_t selfid_;
//    std::map<uint64_t, std::string> groups_;
//
//    std::shared_ptr<CQueue<std::unique_ptr<paxos::Message>>> queue_;
//    std::shared_ptr<std::thread> rpc_worker_;
//};


    int
main ( int argc, char *argv[] )
{
    printf ( "%s config\n", argv[0] );

    assert(2 == argc);
    const char* sConfigFileName = argv[1];

    Config config({sConfigFileName, strlen(sConfigFileName)});

    auto selfid = config.GetSelfId();
    auto groups = config.GetGroups();

    auto paxos_log = unique_ptr<Paxos>{new Paxos{selfid, groups.size()}};
    assert(nullptr != paxos_log);

    MemStorage storage;
    CQueue<std::unique_ptr<paxos::Message>> msg_queue;


    future<int> f = async(
            launch::async, 
            ClientPostMsgWorker, selfid, cref(groups), ref(msg_queue));

    glog::CallBack<MemStorage> callback(storage, msg_queue);
    GlogServiceImpl service(groups, move(paxos_log), callback);
    assert(nullptr == paxos_log);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(
            groups.at(selfid), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<grpc::Server> server(builder.BuildAndStart());

    logdebug("Server %" PRIu64 " listening on %s\n", 
            selfid, groups.at(selfid).c_str());

    server->Wait();
    f.get();

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

