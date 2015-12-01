#include <unistd.h>
#include <map>
#include <vector>
#include <future>
#include <sstream>
#include <algorithm>
#include <random>
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

const int CATCHUP_INTERVAL = 5; // 5s
const int TIMEOUT_CHECK_INTERVAL = 500; // 500ms
const int DEFAULT_PAXOS_INSTANCE_TIMEOUT = 50; // 50ms


// logid, <max_index, commited_index>
std::map<uint64_t, std::tuple<uint64_t, uint64_t>>
    QueryLogPaxosInfo(
            uint64_t logid, 
            uint64_t selfid, 
            const std::map<uint64_t, std::string>& groups)
{
    map<uint64_t, tuple<uint64_t, uint64_t>> all_paxosinfo;
    for (const auto& id_addr_pair : groups) {
        if (id_addr_pair.first == selfid) {
            continue;
        }

        GlogClientImpl client(id_addr_pair.first, 
                grpc::CreateChannel(
                    id_addr_pair.second, grpc::InsecureCredentials()));
        glog::ErrorCode retcode = glog::ErrorCode::OK;
        uint64_t max_index = 0;
        uint64_t commited_index = 0;
        tie(retcode, max_index, commited_index) = client.GetPaxosInfo(logid);
        if (glog::ErrorCode::OK == retcode) {
            all_paxosinfo[id_addr_pair.first] = 
                make_tuple(max_index, commited_index);
        }
    }

    return all_paxosinfo;
}


// most update: peer_id, peer_commited_index
std::tuple<uint64_t, uint64_t>
    FindMostUpdatePeer(
            uint64_t logid, 
            uint64_t selfid, 
            const std::map<uint64_t, std::string>& groups)
{
    uint64_t peer_id = 0;
    uint64_t peer_commited_index = 0;
    // rand-shuffle the groups ?
    auto all_paxosinfo = QueryLogPaxosInfo(logid, selfid, groups);
    for (const auto& id_paxosinfo : all_paxosinfo) {
        if (get<1>(id_paxosinfo.second) > peer_commited_index) {
            peer_id = id_paxosinfo.first;
            peer_commited_index = get<1>(id_paxosinfo.second);
        }
    }

    return make_tuple(peer_id, peer_commited_index);
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
    logdebug("%s thread start", __func__);
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

        logdebug("TEST logid %" PRIu64 " index %" PRIu64 " msgtype %d to_id %" PRIu64 " peer_id %" PRIu64 " ", 
                msg->logid(), msg->index(), static_cast<int>(msg->type()),
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
    logdebug("%s thread start", __func__);

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
    return ; 
}


void TryCatchUpWorker(GlogServiceImpl* service, std::atomic<bool>& stop)
{
    assert(nullptr != service);
    logdebug("%s thread start", __func__);

    while (false == stop) {
        grpc::ServerContext self_context;
        glog::NoopMsg self_request;
        glog::NoopMsg self_reply;

        auto status = 
            service->TryCatchUp(&self_context, &self_request, &self_reply);
        if (!status.ok()) {
            auto error_message = status.error_message();
            logerr("%s failed error_code %d error_message %s", 
                    __func__, static_cast<int>(status.error_code()), 
                    error_message.c_str());
        }

        sleep(CATCHUP_INTERVAL);
    }

    logdebug("%s thread exist", __func__);
    return ;
}

void CheckAndFixTimeoutProposeWorker(
        GlogServiceImpl* service, std::atomic<bool>& stop)
{
    assert(nullptr != service);
    logdebug("%s thread start", __func__);

    while (false == stop) {

        grpc::ServerContext self_context;
        glog::NoopMsg self_request;
        glog::NoopMsg self_reply;

        auto status = 
            service->CheckAndFixTimeoutPropose(
                    &self_context, &self_request, &self_reply);
        if (!status.ok()) {
            auto error_message = status.error_message();
            logerr("%s failed error_code %d error_message %s", 
                    __func__, static_cast<int>(status.error_code()), 
                    error_message.c_str());
        }

        usleep(TIMEOUT_CHECK_INTERVAL * 1000); // ms
    }

    logdebug("%s thread exist", __func__);
    return ;
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

grpc::Status
GlogServiceImpl::GetPaxosInfo(
        grpc::ServerContext* context, 
        const glog::LogId* request, 
        glog::PaxosInfoResponse* reply)
{
    ASSERT_INPUT_PARAMS;

    {
        string peer = context->peer();
        logdebug("peer %s", peer.c_str());
    }

    auto paxos_log = metainfo_->GetPaxosLog(request->logid());
    if (nullptr == paxos_log) {
        logerr("svr don't have paxos_log %" PRIu64 " yet", request->logid());
        reply->set_ret(ErrorCode::LOGID_DONT_EXIST);
        return grpc::Status::OK;
    }

    assert(nullptr != paxos_log);
    uint64_t selfid = 0;
    uint64_t max_index = 0;
    uint64_t commited_index = 0;
    tie(selfid, max_index, commited_index) = paxos_log->GetPaxosInfo();
    reply->set_ret(ErrorCode::OK);
    reply->set_max_index(max_index);
    reply->set_commited_index(commited_index);
    logdebug("%s logid %" PRIu64 " selfid %" PRIu64 
            " max_index %" PRIu64 " commited_index %" PRIu64, 
            __func__, request->logid(), selfid, max_index, commited_index);
    return grpc::Status::OK;
}


grpc::Status
GlogServiceImpl::TryCatchUp(
        grpc::ServerContext* context, 
        const glog::NoopMsg* request, 
        glog::NoopMsg* reply)
{
    ASSERT_INPUT_PARAMS;

    auto logid_set = metainfo_->GetAllLogId();
    for (auto logid : logid_set) {
        auto paxos_log = metainfo_->GetPaxosLog(logid);
        assert(nullptr != paxos_log);

        uint64_t selfid = 0;
        uint64_t max_index = 0;
        uint64_t commited_index = 0;
        tie(selfid, max_index, commited_index) = paxos_log->GetPaxosInfo();

        uint64_t peer_id = 0;
        uint64_t peer_commited_index = 0;
        tie(peer_id, peer_commited_index) = FindMostUpdatePeer(logid, selfid, groups_);
        if (peer_commited_index > commited_index) {
            for (uint64_t catchup_index = commited_index + 1; 
                    catchup_index <= peer_commited_index; 
                    ++catchup_index) {
                auto catchup_msg = make_unique<paxos::Message>();
                assert(nullptr != catchup_msg);

                catchup_msg->set_type(paxos::MessageType::CATCHUP);
                catchup_msg->set_peer_id(selfid);
                catchup_msg->set_to_id(peer_id);
                catchup_msg->set_logid(logid);
                catchup_msg->set_index(catchup_index);
                recv_msg_queue_.Push(move(catchup_msg));
                assert(nullptr == catchup_msg);
            }
        }

        logdebug("selfid %" PRIu64 " logid %" PRIu64 
                " commited_index %" PRIu64 
                " peer_commited_index %" PRIu64, 
                selfid, logid, 
                commited_index, peer_commited_index);
    }

    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::CheckAndFixTimeoutPropose(
        grpc::ServerContext* context, 
        const glog::NoopMsg* request, 
        glog::NoopMsg* reply)
{
    ASSERT_INPUT_PARAMS;

    // 1. try to fix timeout propose only if can reach
    // to the quo of cluster
    uint64_t selfid = metainfo_->GetMetaInfoLog().GetSelfId();
    auto all_paxosinfo = QueryLogPaxosInfo(METAINFO_LOGID, selfid, groups_);
    if (all_paxosinfo.size() + 1 <= (groups_.size() / 2)) {
        logerr("%s selfid %" PRIu64 " can't reach to quorum-size server", 
                __func__, selfid);
        return grpc::Status::OK;
    }

    // 2. local check timeout
    // 2.1 random shuffle the logid
    auto logid_set = metainfo_->GetAllLogId();
    vector<uint64_t> logid_vec(logid_set.begin(), logid_set.end());
    {
        random_device rd;
        mt19937 g(rd());
        shuffle(logid_vec.begin(), logid_vec.end(), g);
    }

    for (auto logid : logid_vec) {
        auto paxos_log = metainfo_->GetPaxosLog(logid);
        assert(nullptr != paxos_log);

        auto timeout_idxes = 
            paxos_log->GetAllTimeoutIndex(
                    std::chrono::milliseconds{DEFAULT_PAXOS_INSTANCE_TIMEOUT});
        for (auto index : timeout_idxes) {

            auto reprop_msg = make_unique<paxos::Message>();
            assert(nullptr != reprop_msg);

            reprop_msg->set_type(paxos::MessageType::TRY_PROP);
            reprop_msg->set_to_id(selfid);
            reprop_msg->set_logid(logid);
            reprop_msg->set_index(index);
            recv_msg_queue_.Push(move(reprop_msg));
            assert(nullptr == reprop_msg);
        }

        logdebug("selfid %" PRIu64 " all_paxosinfo.size %" PRIu64 
                " logid %" PRIu64 " timeout_idxes.size %" PRIu64, 
                selfid, all_paxosinfo.size(), logid, timeout_idxes.size());
    }

    return grpc::Status::OK;
}


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
    tie(ret, index) = paxos_log->TrySet(
            request->index(), 
            {proposing_data->data(), proposing_data->size()});

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
        const glog::LogName* request, 
        glog::LogIdResponse* reply)
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
        const glog::LogName* request, 
        glog::LogIdResponse* reply)
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


void GlogServiceImpl::StartAssistWorker()
{
    if (false == vec_assit_worker_.empty()) {
        return ;
    }
    
    vec_assit_worker_.emplace_back(
            make_unique<AsyncWorker>(TryCatchUpWorker, this));
    vec_assit_worker_.emplace_back(
            make_unique<AsyncWorker>(CheckAndFixTimeoutProposeWorker, this));
}

} // namespace glog


