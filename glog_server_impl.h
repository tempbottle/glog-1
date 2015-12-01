#pragma once

#include <atomic>
#include <memory>
#include "glog.grpc.pb.h"
#include "paxos.h"
#include "cqueue.h"
#include "glog_comm.h"


namespace paxos {
    class Paxos;
    class Message;
}

namespace glog {
    class Status;
    class ServerContext;
    class NoopMsg;
}

template <typename EntryType>
class CQueue;


namespace glog {

class GlogMetaInfo;
class AsyncWorker;


class GlogServiceImpl final : public Glog::Service {

public:
    GlogServiceImpl(
            uint64_t selfid, 
            const std::map<uint64_t, std::string>& groups, 
            ReadCBType readcb, 
            WriteCBType writecb);

    ~GlogServiceImpl();


    // begin of assistant function
    grpc::Status PostMsg(
            grpc::ServerContext* context, 
            const paxos::Message* request, 
            glog::NoopMsg* reply) override;
 
    grpc::Status GetPaxosInfo(
            grpc::ServerContext* context, 
            const glog::LogId* request, 
            glog::PaxosInfoResponse* reply) override;
   
    grpc::Status TryCatchUp(
            grpc::ServerContext* context, 
            const glog::NoopMsg* request, 
            glog::NoopMsg* reply) override;

    grpc::Status CheckAndFixTimeoutPropose(
            grpc::ServerContext* context, 
            const glog::NoopMsg* request, 
            glog::NoopMsg* reply) override;

    // end of assistant function

    // read, write
    grpc::Status Get(
            grpc::ServerContext* context, 
            const glog::GetRequest* request, 
            glog::GetResponse* response) override;

    grpc::Status Set(
            grpc::ServerContext* context, 
            const glog::SetRequest* request, 
            glog::RetCode* response) override;

    grpc::Status CreateANewLog(
            grpc::ServerContext* context, 
            const glog::LogName* request, 
            glog::LogIdResponse* response) override;

    grpc::Status QueryLogId(
            grpc::ServerContext* context, 
            const glog::LogName* request, 
            glog::LogIdResponse* response) override;

public:
    // async worker
    void StartAssistWorker();

private:
    glog::ProposeValue ConvertInto(const glog::ProposeRequest& request);
    std::string ConvertInto(const glog::ProposeValue& value);

    glog::ProposeValue PickleFrom(const std::string& data);

    glog::ProposeValue Convert(const std::string& orig_data);

    glog::ProposeValue 
        CreateANewProposeValue(const std::string& orig_data);


private:
    std::atomic<uint64_t> proposing_seq_;
    std::map<uint64_t, std::string> groups_;
    // TODO
    //
    ReadCBType readcb_;
    WriteCBType writecb_;
 
    MessageQueue send_msg_queue_;
    MessageQueue recv_msg_queue_;

    std::unique_ptr<GlogMetaInfo> metainfo_;

    // async worker
    std::unique_ptr<AsyncWorker> async_sendmsg_worker_;
    std::unique_ptr<AsyncWorker> async_recvmsg_worker_;

    std::vector<std::unique_ptr<AsyncWorker>> vec_assit_worker_;
}; 



}  // namespace glog


