#pragma once

#include <stdint.h>
#include <map>
#include <tuple>
#include <memory>
#include <grpc++/grpc++.h>
#include "gsl.h"
#include "glog.grpc.pb.h"

namespace paxos {
    class Message;
} // namespace paxos


template <typename EntryType>
class CQueue;


namespace glog {



class GlogClientImpl {


public:
    GlogClientImpl(
            const uint64_t svrid, 
            std::shared_ptr<grpc::Channel> channel);

    ~GlogClientImpl();

    void PostMsg(const paxos::Message& msg);

    int Propose(gsl::cstring_view<> data);

    std::tuple<int, uint64_t, uint64_t> GetPaxosInfo();

    void TryCatchUp();

    void TryPropose(uint64_t index);

    std::tuple<std::string, std::string> GetGlog(uint64_t index);

    int Set(uint64_t index, gsl::cstring_view<> data);
    std::tuple<int, uint64_t, std::string> Get(uint64_t index);

    std::tuple<int, uint64_t> CreateANewLog(const std::string& logname);
    std::tuple<int, uint64_t> QueryLogId(const std::string& logname);

private:
    uint64_t svrid_;
    std::unique_ptr<glog::Glog::Stub> stub_;
};

class GlogAsyncClientImpl {

public:
    GlogAsyncClientImpl(
            const uint64_t selfid, 
            std::shared_ptr<grpc::Channel> channel);

    ~GlogAsyncClientImpl();

    void PostMsg(const paxos::Message& msg);

    GlogAsyncClientImpl(GlogAsyncClientImpl&&) = delete;
    GlogAsyncClientImpl(const GlogAsyncClientImpl&) = delete;

private:
    void gc();

private:
    uint64_t selfid_;
    std::unique_ptr<glog::Glog::Stub> stub_;

    uint64_t rpc_seq_ = 0;
    grpc::CompletionQueue cq_;
    std::map<uint64_t, 
        std::tuple<
            std::unique_ptr<grpc::ClientAsyncResponseReader<RetCode>>, 
            grpc::Status, 
            NoopMsg>> rsp_map_;
};


int ClientPostMsgWorker(
        uint64_t selfid, 
        const std::map<uint64_t, std::string>& groups, 
        CQueue<std::unique_ptr<paxos::Message>>& msg_queue);

} // namespace glog



