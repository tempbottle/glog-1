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


    grpc::Status PostMsg(
            grpc::ServerContext* context, 
            const paxos::Message* request, 
            glog::NoopMsg* reply) override;

//    // internal use
//    grpc::Status GetPaxosInfo(
//            grpc::ServerContext* context, 
//            const glog::NoopMsg* request, 
//            glog::PaxosInfo* reply) override;
//
//    grpc::Status TryCatchUp(
//            grpc::ServerContext* context, 
//            const glog::NoopMsg* request, 
//            glog::NoopMsg* reply) override;
//
//    grpc::Status TryPropose(
//            grpc::ServerContext* context, 
//            const glog::TryProposeRequest* request, 
//            glog::NoopMsg* reply) override;
//
//    // test
//    grpc::Status GetGlog(
//            grpc::ServerContext* context, 
//            const glog::GetGlogRequest* request, 
//            glog::GetGlogResponse* reply)    override;

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
            const glog::PaxosLogName* request, 
            glog::PaxosLogId* response) override;

    grpc::Status QueryLogId(
            grpc::ServerContext* context, 
            const glog::PaxosLogName* request, 
            glog::PaxosLogId* response) override;

public:
    // async worker

private:
    glog::ProposeValue ConvertInto(const glog::ProposeRequest& request);
    std::string ConvertInto(const glog::ProposeValue& value);

    glog::ProposeValue PickleFrom(const std::string& data);

    glog::ProposeValue Convert(const std::string& orig_data);

    glog::ProposeValue CreateANewProposeValue(const std::string& orig_data);


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
}; 



}  // namespace glog


