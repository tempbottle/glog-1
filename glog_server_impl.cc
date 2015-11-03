#include <sstream>
#include "glog_server_impl.h"
#include "glog_client_impl.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "utils.h"
#include "mem_storage.h"
#include "callback.h"


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

} // namespace

namespace glog {

GlogServiceImpl::GlogServiceImpl(
        uint64_t selfid, 
        const std::map<uint64_t, std::string>& groups, 
        std::unique_ptr<paxos::Paxos> paxos_log, 
        MemStorage& storage, 
        CQueue<std::unique_ptr<paxos::Message>>& msg_queue)
    : proposing_seq_(selfid)
    , groups_(groups)
    , paxos_log_(move(paxos_log))
    , storage_(storage)
    , msg_queue_(msg_queue)
{

}

//GlogServiceImpl::GlogServiceImpl(
//        const std::map<uint64_t, std::string>& groups, 
//        std::unique_ptr<paxos::Paxos>&& paxos_log, 
//        paxos::Callback callback)
//    : groups_(groups)
//    , paxos_log_(move(paxos_log))
//    , callback_(move(callback))
//{
//    assert(nullptr != paxos_log_);
//    assert(nullptr != callback_);
//}

GlogServiceImpl::~GlogServiceImpl() = default;

grpc::Status 
GlogServiceImpl::PostMsg(
        grpc::ServerContext* context, 
        const paxos::Message* request, glog::NoopMsg* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    logdebug("PostMsg msg index %" PRIu64 " type %u peer_id %" PRIu64 " to_id %" PRIu64 " ", 
            request->index(), static_cast<uint32_t>(request->type()), 
            request->peer_id(), request->to_id());
    const paxos::Message& msg = *request;

    glog::CallBack<MemStorage> callback(storage_, msg_queue_);
    int ret = paxos_log_->Step(msg, callback);
    if (0 != ret) {
        // TODO: 1 == ret => rsp with chosen msg
        logerr("paxos_log.Step selfid %" PRIu64 " index %" PRIu64 " failed ret %d", 
                paxos_log_->GetSelfId(), request->index(), ret);

        return grpc::Status(
                static_cast<grpc::StatusCode>(ret), "paxos log Step failed");
    }

    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::Propose(
        grpc::ServerContext* context, 
        const glog::ProposeRequest* request, glog::ProposeResponse* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    {
        string peer = context->peer();
        logdebug("Propose datasize %" PRIu64 " peer %s", 
                request->data().size(), peer.c_str());
    }

    ProposeValue value = ConvertInto(*request);
    string proposing_data = ConvertInto(value);

    int ret = 0;
    uint64_t index = 0;
    {
        glog::CallBack<MemStorage> callback(storage_, msg_queue_);
        tie(ret, index) = paxos_log_->Propose(
                {proposing_data.data(), proposing_data.size()}, callback);
    }
    reply->set_retcode(ret);
    if (0 != ret) {
        logerr("Propose ret %d", ret);
        return grpc::Status(
                static_cast<grpc::StatusCode>(ret), "Propose failed");
    }

    hassert(0 < index, "index %" PRIu64 "\n", index); 
    paxos_log_->Wait(index);

    {
        // ADD FOR TEST
        assert(index <= paxos_log_->GetCommitedIndex()); 
    }

    // check data by read from storage
    auto hs = storage_.Get(index);
    assert(nullptr != hs);
    const string& chosen_data = hs->accepted_value();
    assert(false == chosen_data.empty());
    logdebug("PROP: seq %" PRIu64 " timestaemp %" PRIu64, 
            value.seq(), value.timestamp());
    logdebug("SIZE: proposing_data %" PRIu64 " chosen_data %" PRIu64, 
            proposing_data.size(), chosen_data.size());
    if (proposing_data.size() == chosen_data.size()) {
        ProposeValue chosen_value = PickleFrom(chosen_data); 
        logdebug("CHOSEN: seq %" PRIu64 " timestamp %" PRIu64, 
                chosen_value.seq(), chosen_value.timestamp());
        if (chosen_value.seq() == value.seq() &&
                chosen_value.timestamp() == value.timestamp()) {
            assert(chosen_value.data() == value.data());
            // only case
            return grpc::Status::OK;
        }
    }

    // else: propose preempted
    return grpc::Status(
            static_cast<grpc::StatusCode>(1), "Propose preempted");
}

grpc::Status
GlogServiceImpl::GetPaxosInfo(
        grpc::ServerContext* context, 
        const glog::NoopMsg* request, 
        glog::PaxosInfo* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    {
        string peer = context->peer();
        logdebug("peer %s", peer.c_str());
    }

    uint64_t selfid = 0;
    uint64_t max_index = 0;
    uint64_t commited_index = 0;
    tie(selfid, max_index, commited_index) = paxos_log_->GetPaxosInfo();
    reply->set_max_index(max_index);
    reply->set_commited_index(commited_index);
    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::TryCatchUp(
        grpc::ServerContext* context, 
        const glog::NoopMsg* request, 
        glog::NoopMsg* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    {
        string peer = context->peer();
        logdebug("peer %s", peer.c_str());
    }

    uint64_t selfid = 0;
    uint64_t max_index = 0;
    uint64_t commited_index = 0;
    tie(selfid, max_index, commited_index) = paxos_log_->GetPaxosInfo();
    
    bool most_recent = false;
    uint64_t peer_id = 0;
    uint64_t peer_commited_index = 0;
    tie(most_recent, peer_id, peer_commited_index) = 
        FindMostUpdatePeer(selfid, commited_index, groups_);

    if (false == most_recent) {
        GlogClientImpl client(peer_id, grpc::CreateChannel(
                    groups_.at(peer_id), grpc::InsecureCredentials()));
        for (uint64_t catchup_index = commited_index + 1; 
                catchup_index <= peer_commited_index; ++catchup_index) {
            
            paxos::Message msg;
            msg.set_type(paxos::MessageType::CATCHUP);
            msg.set_peer_id(selfid);
            msg.set_to_id(peer_id);
            msg.set_index(catchup_index);

            client.PostMsg(msg);
        }
    }

    logdebug("most_recent %d seldid %" PRIu64 
            " commited_index %" PRIu64 " peer_commited_index %" PRIu64, 
            most_recent, selfid, commited_index, peer_commited_index);
    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::TryPropose(
        grpc::ServerContext* context, 
        const glog::TryProposeRequest* request, 
        glog::NoopMsg* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    {
        string peer = context->peer();
        logdebug(" peer %s", peer.c_str());
    }

    glog::CallBack<MemStorage> callback(storage_, msg_queue_);
    int retcode = paxos_log_->TryPropose(request->index(), callback);
    if (0 != retcode) {
        logerr("TryPropose retcode %d", retcode);
        return grpc::Status(
                static_cast<grpc::StatusCode>(retcode), "redo propose failed");
    }

    return grpc::Status::OK;
}

grpc::Status
GlogServiceImpl::GetGlog(
        grpc::ServerContext* context, 
        const glog::GetGlogRequest* request, 
        glog::GetGlogResponse* reply)
{
    assert(nullptr != context);
    assert(nullptr != request);
    assert(nullptr != reply);

    {
        string peer = context->peer();
        logdebug("GetGlog request index %" PRIu64 " %s", 
                request->index(), peer.c_str());
    }

    string info, data;
    tie(info, data) = paxos_log_->GetInfo(request->index());
    reply->set_info(info);  
    reply->set_data(data);
    return grpc::Status::OK;
}

glog::ProposeValue GlogServiceImpl::ConvertInto(const glog::ProposeRequest& request)
{
    ProposeValue value;
    value.set_seq(proposing_seq_.fetch_add(groups_.size()));
    value.set_timestamp(static_cast<uint64_t>(
                chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now().time_since_epoch()).count()));
    value.set_data(request.data());
 
    return value;
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


