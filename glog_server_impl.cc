#include <map>
#include <vector>
#include <future>
#include <sstream>
#include "glog_server_impl.h"
#include "glog_client_impl.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "utils.h"
#include "mem_storage.h"
#include "callback.h"
#include "glog_metainfo.h"
#include "utils/async_worker.h"
#include "utils/data_helper.h"


#define ASSERT_INPUT_PARAMS  {    \
    assert(nullptr != context);   \
    assert(nullptr != request);   \
    assert(nullptr != reply);     \
}

using namespace std;

namespace {

using namespace glog;


std::tuple<bool, uint64_t, uint64_t>
    FindMostUpdatePeer(
            uint64_t selfid, 
            uint64_t self_commited_index, 
            const std::map<uint64_t, std::string>& groups)
{
    uint64_t peer_id = 0;
    uint64_t peer_commited_index = 0;
    // rand-shuffle the groups ?
    for (auto& piter : groups) {
        if (piter.first == selfid) {
            continue;
        }

        GlogClientImpl client(piter.first, grpc::CreateChannel(
                    piter.second, grpc::InsecureCredentials()));
        int retcode = 0;
        uint64_t max_index = 0;
        uint64_t commited_index = 0;
        tie(retcode, max_index, commited_index) = client.GetPaxosInfo();
        if (0 == retcode) {
            if (commited_index > peer_commited_index) {
                peer_id = piter.first;
                peer_commited_index = commited_index;
            }
        }
    }

    return make_tuple(
            peer_commited_index <= self_commited_index, peer_id, peer_commited_index);
}

//std::unique_ptr<std::string> Dump(const glog::ProposeValue& value)
//{
//    stringstream ss;
//    bool ret = value.SerializeToOstream(&ss);
//    if (false == ret) {
//        return nullptr;
//    }
//
//    return unique_ptr<string>(new string(ss.str()));
//}

std::unique_ptr<glog::ProposeValue> Pickle(const std::string& data)
{
    ProposeValue value;
    {
        stringstream ss;
        ss.str(data);
        bool ret = value.ParseFromIstream(&ss);
        if (false == ret) {
            return nullptr;
        }
    }
    
    return std::unique_ptr<glog::ProposeValue>{new glog::ProposeValue{value}};
}



} // namespace

namespace glog {

namespace {

void ClientCallPostMsg(
        std::map<uint64_t, std::unique_ptr<glog::GlogClientImpl>>& client_cache, 
        const std::map<uint64_t, std::string>& groups, 
        const paxos::Message& msg)
{
    assert(0 < msg.to_id());
    assert(0 < msg.peer_id());

    auto svrid = msg.to_id();
    if (client_cache.end() == client_cache.find(svrid)) {
        client_cache.emplace(svrid, std::unique_ptr<glog::GlogClientImpl>{
                new glog::GlogClientImpl{svrid, grpc::CreateChannel(
                        groups.at(svrid), grpc::InsecureCredentials())}});
        assert(nullptr != client_cache.at(svrid));
    }

    auto& client = client_cache.at(svrid);
    assert(nullptr != client);

    client->PostMsg(msg);
    logdebug("PostMsg svrid %" PRIu64 
            " msg type %d", svrid, static_cast<int>(msg.type()));
    return ;
}


void SendPostMsgWorker(
        uint64_t selfid, 
        const std::map<uint64_t, std::string>& groups, 
        MessageQueue& send_msg_queue, 
        std::atomic<bool>& stop)
{
    std::map<uint64_t, std::unique_ptr<glog::GlogClientImpl>> client_cache;

    // TODO: 
    // read atomi<bool> by seq-order might be too cost;
    // => add timeout in Pop;
    while (false == stop) {
        auto msg = send_msg_queue.Pop(chrono::microseconds{100});
        if (nullptr == msg) {
            continue;
        }

        // deal with the msg;
        assert(nullptr != msg);
        assert(selfid == msg->peer_id());
        assert(0 < msg->index());

        logdebug("TEST index %" PRIu64 " msgtype %d to_id %" PRIu64 " peer_id %" PRIu64 " ", 
                msg->index(), static_cast<int>(msg->type()),
                msg->to_id(), msg->peer_id());

        if (0 != msg->to_id()) {
            ClientCallPostMsg(client_cache, groups, *msg);
        } else {
            // broadcast
            for (auto& piter : groups) {
                if (selfid == piter.first) {
                    continue;
                }

                msg->set_to_id(piter.first);
                ClientCallPostMsg(client_cache, groups, *msg);
            }
        }
    }
    logdebug("%s thread exist", __func__);
    return ;
}

void RecvPostMsgWorker(
        uint64_t selfid, 
        GlogMetaInfo* metainfo, 
        MessageQueue& recv_msg_queue, 
        std::atomic<bool>& stop)
{
    assert(nullptr != metainfo);
    // TODO: fix while loop: stop
    while (false == stop) {
        auto recv_msg = recv_msg_queue.Pop(chrono::microseconds{100});
        if (nullptr == recv_msg) {
            continue;
        }

        assert(nullptr != recv_msg);
        assert(selfid == recv_msg->to_id());
        assert(0 < recv_msg->index());

        logdebug("TEST selfid %" PRIu64 " RecvMsg logid %" PRIu64 "index %" PRIu64 
                " msgtype %d to_id %" PRIu64 " peer_id %" PRIu64 " ", 
                selfid, recv_msg->index(), static_cast<int>(recv_msg->type()),
                recv_msg->to_id(), recv_msg->peer_id());

        paxos::Paxos* paxos_log = metainfo->GetPaxosLog(recv_msg->logid());
        if (nullptr == paxos_log) {
            // ERROR CASE
            logerr("selfid %" PRIu64 " recvmsg logid %" PRIu64 
                    " don't exist yet", selfid, recv_msg->logid());
            continue;
        }

        assert(nullptr != paxos_log);
        int ret = paxos_log->Step(*recv_msg);
        if (0 > ret) {
            logerr("paxos_log.step index %" PRIu64 " msgtype %d ret %d", 
                    recv_msg->index(), static_cast<int>(recv_msg->type()), ret);
        }
    }

    logdebug("%s thread exist", __func__);
}


} // namespace

GlogServiceImpl::GlogServiceImpl(
        uint64_t selfid, 
        const std::map<uint64_t, std::string>& groups, 
        ReadCBType readcb, 
        WriteCBType writecb)
    : proposing_seq_(selfid)
    , groups_(groups)
    , readcb_(readcb)
    , writecb_(writecb)
{
    auto paxos_creater = 
            [=](uint64_t logid) -> std::unique_ptr<paxos::Paxos> {
                auto paxos_readcb = 
                        [=](uint64_t index) 
                            -> std::unique_ptr<paxos::HardState> {
                            return readcb_(logid, index);
                        };

                return make_unique<paxos::Paxos>(
                        logid, selfid, groups.size(), 
                        paxos::PaxosCallBack{
                            paxos_readcb, 
                            writecb_, 
                            SendCallBack{send_msg_queue_}
                        }); 
            };

    metainfo_ = make_unique<GlogMetaInfo>(readcb_, paxos_creater);
    assert(nullptr != metainfo_);

    async_sendmsg_worker_ = 
        make_unique<AsyncWorker>(SendPostMsgWorker, 
                selfid, cref(groups_), ref(send_msg_queue_));
    async_recvmsg_worker_ = make_unique<AsyncWorker>(
            RecvPostMsgWorker, selfid, metainfo_.get(), ref(recv_msg_queue_));
}

  
GlogServiceImpl::~GlogServiceImpl() = default;


grpc::Status 
GlogServiceImpl::PostMsg(
        grpc::ServerContext* context, 
        const paxos::Message* request, glog::NoopMsg* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    logdebug("PostMsg msg logid %" PRIu64 " index %" PRIu64 
            " type %u peer_id %" PRIu64 " to_id %" PRIu64 " ", 
            request->logid(), 
            request->index(), static_cast<uint32_t>(request->type()), 
            request->peer_id(), request->to_id());

    const paxos::Message& msg = *request;
    recv_msg_queue_.Push(make_unique<paxos::Message>(msg));
    return grpc::Status::OK;
}

//grpc::Status
//GlogServiceImpl::GetPaxosInfo(
//        grpc::ServerContext* context, 
//        const glog::NoopMsg* request, 
//        glog::PaxosInfo* reply)
//{
//    assert(nullptr != context);
//    assert(nullptr != request);
//    assert(nullptr != reply);
//
//    {
//        string peer = context->peer();
//        logdebug("peer %s", peer.c_str());
//    }
//
//    uint64_t selfid = 0;
//    uint64_t max_index = 0;
//    uint64_t commited_index = 0;
//    tie(selfid, max_index, commited_index) = paxos_log_.GetPaxosInfo();
//    reply->set_max_index(max_index);
//    reply->set_commited_index(commited_index);
//    return grpc::Status::OK;
//}
//
//grpc::Status
//GlogServiceImpl::TryCatchUp(
//        grpc::ServerContext* context, 
//        const glog::NoopMsg* request, 
//        glog::NoopMsg* reply)
//{
//    assert(nullptr != context);
//    assert(nullptr != request);
//    assert(nullptr != reply);
//
//    {
//        string peer = context->peer();
//        logdebug("peer %s", peer.c_str());
//    }
//
//    uint64_t selfid = 0;
//    uint64_t max_index = 0;
//    uint64_t commited_index = 0;
//    tie(selfid, max_index, commited_index) = paxos_log_.GetPaxosInfo();
//    
//    bool most_recent = false;
//    uint64_t peer_id = 0;
//    uint64_t peer_commited_index = 0;
//    tie(most_recent, peer_id, peer_commited_index) = 
//        FindMostUpdatePeer(selfid, commited_index, groups_);
//
//    if (false == most_recent) {
//        GlogClientImpl client(peer_id, grpc::CreateChannel(
//                    groups_.at(peer_id), grpc::InsecureCredentials()));
//        for (uint64_t catchup_index = commited_index + 1; 
//                catchup_index <= peer_commited_index; ++catchup_index) {
//            
//            paxos::Message msg;
//            msg.set_type(paxos::MessageType::CATCHUP);
//            msg.set_peer_id(selfid);
//            msg.set_to_id(peer_id);
//            msg.set_index(catchup_index);
//
//            client.PostMsg(msg);
//        }
//    }
//
//    logdebug("most_recent %d seldid %" PRIu64 
//            " commited_index %" PRIu64 " peer_commited_index %" PRIu64, 
//            most_recent, selfid, commited_index, peer_commited_index);
//    return grpc::Status::OK;
//}
//
//grpc::Status
//GlogServiceImpl::TryPropose(
//        grpc::ServerContext* context, 
//        const glog::TryProposeRequest* request, 
//        glog::NoopMsg* reply)
//{
//    assert(nullptr != context);
//    assert(nullptr != request);
//    assert(nullptr != reply);
//
//    {
//        string peer = context->peer();
//        logdebug(" peer %s", peer.c_str());
//    }
//
//    // glog::CallBack<MemStorage> callback(storage_, msg_queue_);
//    int retcode = 0;
//    uint64_t index = 0;
//
//    tie(retcode, index) = 
//        paxos_log_.Propose(request->index(), {nullptr, 0}, false);
//    if (0 != retcode) {
//        retcode = -10011;
//        logerr("TryPropose retcode %d", retcode);
//        return grpc::Status(
//                static_cast<grpc::StatusCode>(retcode), "redo propose failed");
//    }
//
//    assert(index == request->index());
//    return grpc::Status::OK;
//}
//
//grpc::Status
//GlogServiceImpl::GetGlog(
//        grpc::ServerContext* context, 
//        const glog::GetGlogRequest* request, 
//        glog::GetGlogResponse* reply)
//{
//    assert(nullptr != context);
//    assert(nullptr != request);
//    assert(nullptr != reply);
//
//    {
//        string peer = context->peer();
//        logdebug("GetGlog request index %" PRIu64 " %s", 
//                request->index(), peer.c_str());
//    }
//
//    string info, data;
//    tie(info, data) = paxos_log_.GetInfo(request->index());
//    reply->set_info(info);  
//    reply->set_data(data);
//    return grpc::Status::OK;
//}


grpc::Status
GlogServiceImpl::Get(
        grpc::ServerContext* context, 
        const glog::GetRequest* request, 
        glog::GetResponse* reply)
{
    ASSERT_INPUT_PARAMS;

    if (0 == request->index()) {
        reply->set_ret(ErrorCode::INVALID_PARAMS);
        logerr("invalid index 0");
        return grpc::Status::OK;
    }

    assert(nullptr != metainfo_);
    paxos::Paxos* paxos_log = metainfo_->GetPaxosLog(request->logid());
    if (nullptr == paxos_log) {
        logerr("svr don't have paxos_log %" PRIu64 " yet", 
                request->logid());
        reply->set_ret(ErrorCode::LOGID_DONT_EXIST);
        return grpc::Status::OK;
    }

    assert(nullptr != paxos_log);
    paxos::ErrorCode ret = paxos::ErrorCode::OK;
    uint64_t commited_index = 0;
    std::unique_ptr<paxos::HardState> hs;
    tie(ret, commited_index, hs) = paxos_log->Get(request->index());
    if (paxos::ErrorCode::OK == ret ||
            paxos::ErrorCode::UNCOMMITED_INDEX == ret) {
        reply->set_commited_index(commited_index);
        if (paxos::ErrorCode::OK == ret) {
            assert(nullptr != hs);
            assert(commited_index >= hs->index());

            auto commited_data = 
                Pickle<glog::ProposeValue>(hs->accepted_value());
            if (nullptr == commited_data) {
                reply->set_ret(ErrorCode::PROTOBUF_PICKLE_ERROR);
                logerr("Pickle index %" PRIu64 " failed", request->index());
                return grpc::Status::OK;
            }

            reply->set_data(commited_data->data());
            reply->set_ret(ErrorCode::OK);
        }
        else {
            reply->set_ret(ErrorCode::UNCOMMITED_INDEX);
        }
    }
    else {
        logerr("paxos_log_.Get logid %" PRIu64 " index %" PRIu64 " ret %d", 
                request->logid(), request->index(), ret);
        reply->set_ret(ErrorCode::PAXOS_LOG_GET_ERROR);
    }

    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::Set(
        grpc::ServerContext* context, 
        const glog::SetRequest* request, 
        glog::RetCode* reply)
{
    ASSERT_INPUT_PARAMS;

    assert(nullptr != metainfo_);
    paxos::Paxos* paxos_log = metainfo_->GetPaxosLog(request->logid());
    if (nullptr == paxos_log) {
        logerr("svr don't have paxos_log %" PRIu64 " yet", 
                request->logid());
        reply->set_ret(ErrorCode::LOGID_DONT_EXIST);
        return grpc::Status::OK;
    }

    assert(nullptr != paxos_log);
    auto value = CreateANewProposeValue(request->data());
    auto proposing_data = Dump(value);
    if (nullptr == proposing_data) {
        reply->set_ret(ErrorCode::PROTOBUF_DUMP_ERROR);
        return grpc::Status::OK;
    }

    assert(nullptr != proposing_data);
    paxos::ErrorCode ret = paxos::ErrorCode::OK;
    uint64_t index = 0;
    {
        // glog::CallBack<MemStorage> callback(storage_, msg_queue_);
        tie(ret, index) = paxos_log->TrySet(
                request->index(), 
                {proposing_data->data(), proposing_data->size()});
    }

    if (paxos::ErrorCode::OK != ret) {
        logerr("Propose ret %d", ret);
        reply->set_ret(ErrorCode::PAXOS_LOG_TRY_SET_ERROR);
        return grpc::Status::OK;
    }

    hassert(0 < index, "index %" PRIu64 "\n", index); 
    assert(0 == request->index() || index == request->index());
    paxos_log->Wait(index);
    {
        // ADD FOR TEST
        assert(index <= paxos_log->GetCommitedIndex()); 
    }

    // check data by read from storage
    auto hs = readcb_(request->logid(), index);
    assert(nullptr != hs);
    assert(request->logid() == hs->logid());
    const string& chosen_data = hs->accepted_value();
    assert(false == chosen_data.empty());
    logdebug("PROP: seq %" PRIu64 " timestaemp %" PRIu64, 
            value.seq(), value.timestamp());
    logdebug("SIZE: proposing_data %" PRIu64 " chosen_data %" PRIu64, 
            proposing_data->size(), chosen_data.size());
    if (proposing_data->size() == chosen_data.size()) {
        ProposeValue chosen_value = PickleFrom(chosen_data); 
        logdebug("CHOSEN: seq %" PRIu64 " timestamp %" PRIu64, 
                chosen_value.seq(), chosen_value.timestamp());
        if (chosen_value.seq() == value.seq() &&
                chosen_value.timestamp() == value.timestamp()) {
            assert(chosen_value.data() == value.data());

            reply->set_ret(ErrorCode::OK);
            return grpc::Status::OK;
        }
    }

    reply->set_ret(ErrorCode::PAXOS_LOG_PREEMPTED);
    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::CreateANewLog(
        grpc::ServerContext* context, 
        const glog::PaxosLogName* request, 
        glog::PaxosLogId* reply)
{
    ASSERT_INPUT_PARAMS;

    if (true == request->logname().empty()) {
        reply->set_ret(glog::ErrorCode::INVALID_PARAMS);
        logerr("client request logname empty");
        return grpc::Status::OK;
    }

    assert(false == request->logname().empty());
    uint64_t new_logid = 0;
    unique_ptr<glog::SetRequest> self_request;
    tie(self_request, new_logid)
        = metainfo_->PackMetaInfoEntry(request->logname());
    if (nullptr == self_request) {
        logdebug("log %s already exist logid %" PRIu64, 
                request->logname().c_str(), new_logid);
        reply->set_ret(glog::ErrorCode::ALREADY_EXIST);
        reply->set_logid(new_logid);
        return grpc::Status::OK;
    }

    assert(nullptr != self_request);
    assert(0 < new_logid);
    grpc::ServerContext self_context;
    glog::RetCode self_reply;
    auto status = Set(&self_context, self_request.get(), &self_reply);
    if (!status.ok()) {
        logerr("Set return status failed");
        return status;
    }

    if (ErrorCode::OK != self_reply.ret()) {
        reply->set_ret(self_reply.ret());
        logerr("Set metainfo reply ret %d", self_reply.ret());
        return grpc::Status::OK;
    }

    reply->set_ret(glog::ErrorCode::OK);
    reply->set_logid(new_logid);
    logdebug("success set metainfo logname %s logid %" PRIu64, 
            request->logname().c_str(), new_logid);
    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::QueryLogId(
        grpc::ServerContext* context, 
        const glog::PaxosLogName* request, 
        glog::PaxosLogId* reply)
{
    ASSERT_INPUT_PARAMS;

    if (true == request->logname().empty()) {
        reply->set_ret(glog::ErrorCode::INVALID_PARAMS);
        logerr("client request logname empty");
        return grpc::Status::OK;
    }

    assert(false == request->logname().empty()); 
    assert(nullptr != metainfo_);
    uint64_t logid = metainfo_->QueryLogId(request->logname());
    if (0 == logid) {
        logerr("logname %s don't exist yet", request->logname().c_str());
        reply->set_ret(glog::ErrorCode::LOGNAME_DONT_EXIST);
        return grpc::Status::OK;
    }

    logdebug("INFO: logname %s logid %" PRIu64, 
            request->logname().c_str(), logid);
    reply->set_ret(glog::ErrorCode::OK);
    reply->set_logid(logid);
    return grpc::Status::OK;
}

glog::ProposeValue GlogServiceImpl::Convert(const std::string& orig_data)
{
    ProposeValue value;
    value.set_seq(proposing_seq_.fetch_add(groups_.size()));
    value.set_timestamp(static_cast<uint64_t>(
                chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now().time_since_epoch()).count()));
    value.set_data(orig_data);
    return value;
}

glog::ProposeValue
GlogServiceImpl::CreateANewProposeValue(const std::string& orig_data)
{
    ProposeValue value;
    value.set_seq(proposing_seq_.fetch_add(groups_.size()));
    value.set_timestamp(static_cast<uint64_t>(
                chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now().time_since_epoch()).count()));
    value.set_data(orig_data);
    return value;
}



glog::ProposeValue GlogServiceImpl::ConvertInto(const glog::ProposeRequest& request)
{
    return Convert(request.data());
}


std::string GlogServiceImpl::ConvertInto(const glog::ProposeValue& value)
{
    stringstream ss;
    assert(true == value.SerializeToOstream(&ss));
    // TODO: in-efficient copy
    return ss.str();    
}


glog::ProposeValue GlogServiceImpl::PickleFrom(const std::string& data)
{
    ProposeValue value;
    {
        stringstream ss;
        // TODO: in-efficient cpy
        ss.str(data);
        assert(true == value.ParseFromIstream(&ss));
    }
    return value;
}


} // namespace glog


