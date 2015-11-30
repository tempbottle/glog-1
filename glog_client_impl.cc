#include <chrono>
#include "glog_client_impl.h"
#include "utils.h"
#include "glog.pb.h"
#include "paxos.pb.h"
#include "cqueue.h"


using grpc::ClientContext;
using grpc::Status;
using namespace std;


namespace {

const int DEFAULT_TIMEOUT = 100;

void set_deadline(grpc::ClientContext& context, int milliseconds)
{
    context.set_deadline(
            std::chrono::system_clock::now() + 
            std::chrono::milliseconds{milliseconds});
}

void ClientCallPostMsg(
        const std::map<uint64_t, std::string>& groups, const paxos::Message& msg)
{
    assert(0 < msg.to_id());
    assert(0 < msg.peer_id());
    static __thread 
        std::map<uint64_t, std::unique_ptr<glog::GlogClientImpl>>* cache;
    if (nullptr == cache) {
        cache = new std::map<uint64_t, std::unique_ptr<glog::GlogClientImpl>>{};
    }

    assert(nullptr != cache);

    auto& client_cache = *cache;

    auto svrid = msg.to_id();
    if (client_cache.end() == client_cache.find(svrid)) {
        client_cache.emplace(svrid, std::unique_ptr<glog::GlogClientImpl>{
                new glog::GlogClientImpl{svrid, grpc::CreateChannel(
                        groups.at(svrid), grpc::InsecureCredentials())}});
    }

    auto& client = client_cache.at(svrid);
    assert(nullptr != client);

    client->PostMsg(msg);
    logdebug("PostMsg svrid %" PRIu64 
            " msg type %d", svrid, static_cast<int>(msg.type()));
    return ;
}


}

namespace glog {


GlogClientImpl::GlogClientImpl(
        const uint64_t svrid, 
        std::shared_ptr<grpc::Channel> channel)
    : svrid_(svrid)
    , stub_(glog::Glog::NewStub(channel))
{
    assert(0 < svrid_);
    assert(nullptr != channel);
}

GlogClientImpl::~GlogClientImpl() = default;

void GlogClientImpl::PostMsg(const paxos::Message& msg)
{
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);
    hassert(msg.to_id() == svrid_, "msg.peer_id %" PRIu64 
            " svrid_ %" PRIu64 "", msg.peer_id(), svrid_);
    Status status = stub_->PostMsg(&context, msg, &reply);
    if (status.ok()) {
        logdebug("index %" PRIu64 " PostMsg succ", msg.index());
    } else {
        logerr("index %" PRIu64 " PostMsg failed", msg.index());
    }

    return ;
}

int GlogClientImpl::Propose(gsl::cstring_view<> data)
{
    ProposeRequest request;
    request.set_data(data.data(), data.size());
    
    ProposeResponse reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);
    Status status = stub_->Propose(&context, request, &reply);
    printf ( "ret from Propose\n" );
    if (status.ok()) {
        return reply.retcode();
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return static_cast<int>(status.error_code());
}

std::tuple<int, uint64_t, uint64_t> GlogClientImpl::GetPaxosInfo()
{
    NoopMsg request;
    PaxosInfo reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->GetPaxosInfo(&context, request, &reply);
    if (status.ok()) {
        return std::make_tuple(0, reply.max_index(), reply.commited_index());
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    assert(0 != static_cast<int>(status.error_code()));
    return std::make_tuple(static_cast<int>(status.error_code()), 0ull, 0ull);
}

void GlogClientImpl::TryCatchUp()
{
    NoopMsg request;
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->TryCatchUp(&context, request, &reply);
    if (status.ok()) {
        return ;
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return ;
}

void GlogClientImpl::TryPropose(uint64_t index)
{
    assert(0 < index);
    TryProposeRequest request;
    request.set_index(index);
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->TryPropose(&context, request, &reply);
    if (status.ok()) {
        return ;
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return ;
}

std::tuple<std::string, std::string> GlogClientImpl::GetGlog(uint64_t index)
{
    assert(0 < index);
    GetGlogRequest request;
    request.set_index(index);

    GetGlogResponse reply;

    ClientContext context; 
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->GetGlog(&context, request, &reply);
    if (status.ok()) {
        return std::make_tuple(reply.info(), reply.data());
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return std::make_tuple(move(error_message), "");
}

int GlogClientImpl::Set(uint64_t index, gsl::cstring_view<> data)
{
    SetRequest request;
    request.set_index(index);
    request.set_data(data.data(), data.size());

    RetCode reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->Set(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("failed error_code %d error_message %s", 
                static_cast<int>(status.error_code()), error_message.c_str());
        return -1;
    }

    if (0 != reply.ret()) {
        logerr("index %" PRIu64 " Set ret %d", index, reply.ret());
        return reply.ret();
    }

    logdebug("index %" PRIu64 " Set success", index);
    return 0; // success
}

std::tuple<int, uint64_t, std::string> GlogClientImpl::Get(uint64_t index)
{
    if (0 == index) {
        return std::make_tuple(-1, 0ull, "");
    }

    assert(0 < index);
    GetRequest request;
    request.set_index(index);

    GetResponse reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->Get(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("failed error_code %d error_message %s", 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(-1, 0ull, "");
    }

    if (0 > reply.ret()) {
        logerr("failed index %" PRIu64 " ret %d", index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull, "");
    }

    logdebug("index %" PRIu64 " Get success", index);
    return std::make_tuple(reply.ret(), 
            reply.commited_index(), 0 != reply.ret() ? "" : reply.data());
}

std::tuple<int, uint64_t>
GlogClientImpl::CreateANewLog(const std::string& logname)
{
    if (true == logname.empty()) {
        return make_tuple(-1, 0ull);
    }

    assert(false == logname.empty());
    PaxosLogName request;
    request.set_logname(logname);

    PaxosLogId reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    auto status = stub_->CreateANewLog(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("%s failed error_code %d error_message %s", logname.c_str(), 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(-1, 0ull);
    }

    if (0 > reply.ret()) {
        logerr("failed index %" PRIu64 " ret %d", index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull);
    }

    logdebug("INFO logname %s %s ret %d logid %" PRIu64, 
            logname.c_str(), __func__, reply.ret(), reply.logid());
    return make_tuple(reply.ret(), reply.logid());
}

std::tuple<int, uint64_t>
GlogClientImpl::QueryLogId(const std::string& logname)
{
    if (true == logname.empty()) {
        return make_tuple(-1, 0ull);
    }

    assert(false == logname.empty());
    PaxosLogName request;
    request.set_logname(logname);

    PaxosLogId reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    auto status = stub_->QueryLogId(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("%s failed error_code %d error_message %s", logname.c_str(), 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(-1, 0ull);
    }

    if (0 > reply.ret()) {
        logerr("failed index %" PRIu64 " ret %d", index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull);
    }

    logdebug("INFO logname %s %s ret %d logid %" PRIu64, 
            logname.c_str(), __func__, reply.ret(), reply.logid());
    return make_tuple(reply.ret(), reply.logid());
}


GlogAsyncClientImpl::GlogAsyncClientImpl(
        const uint64_t selfid, 
        std::shared_ptr<grpc::Channel> channel)
    : selfid_(selfid)
    , stub_(glog::Glog::NewStub(channel))
{

}

GlogAsyncClientImpl::~GlogAsyncClientImpl() = default;

void GlogAsyncClientImpl::gc()
{
    const size_t LOW_WATER_MARK = 0;
    void* got_tag = nullptr;
    bool ok = false;
    while (rsp_map_.size() > LOW_WATER_MARK) {

        cq_.Next(&got_tag, &ok);
        if (ok) {
            auto seq = reinterpret_cast<uint64_t>(got_tag);
            assert(rsp_map_.end() != rsp_map_.find(seq));

            auto& t = rsp_map_[seq];
            if (!std::get<1>(t).ok()) {
                auto err_msg = std::get<1>(t).error_message();
                logerr("rsp seq %" PRIu64 " error msg %s", seq, err_msg.c_str());
            }
            rsp_map_.erase(seq); // success gc 1
        }
    }
}

void GlogAsyncClientImpl::PostMsg(const paxos::Message& msg)
{
    return ; // TODO
//    ClientContext context;
//
//    uint64_t seq = ++rpc_seq_;
//    auto& t = rsp_map_[seq];
//    std::get<0>(t) = stub_->AsyncPostMsg(&context, msg, &cq_);
//    std::get<0>(t)->Finish(&std::get<2>(t), &std::get<1>(t), reinterpret_cast<void*>(seq));
//    // DO NOT CALL cq_.Next(...); // block wait
//    return ;
}


int ClientPostMsgWorker(
        uint64_t selfid, 
        const std::map<uint64_t, std::string>& groups, 
        CQueue<std::unique_ptr<paxos::Message>>& msg_queue)
{
    // TODO: bStop ?
    while (true) {
        std::unique_ptr<paxos::Message> msg = msg_queue.Pop();

        // deal with the msg;
        assert(nullptr != msg);
        assert(selfid == msg->peer_id());
        assert(0 < msg->index());

        logdebug("TEST index %" PRIu64 " msgtype %d to_id %" PRIu64 " peer_id %" PRIu64 " ", 
                msg->index(), static_cast<int>(msg->type()),
                msg->to_id(), msg->peer_id());

        if (0 != msg->to_id()) {
            ClientCallPostMsg(groups, *msg);
        } else {
            // broadcast
            for (auto& piter : groups) {
                if (selfid == piter.first) {
                    continue;
                }

                msg->set_to_id(piter.first);
                ClientCallPostMsg(groups, *msg);
            }
        }
    }
}

} // namespace glog



