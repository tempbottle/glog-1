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

    // retcode, max_index, commited_index
    std::tuple<
        glog::ErrorCode, uint64_t, uint64_t> GetPaxosInfo(uint64_t logid);

    void TryCatchUp();

    void TryPropose(uint64_t index);

    std::tuple<std::string, std::string> GetGlog(uint64_t index);

    glog::ErrorCode Set(uint64_t logid, uint64_t index, gsl::cstring_view<> data);
    std::tuple<glog::ErrorCode, uint64_t, std::string> Get(uint64_t logid, uint64_t index);

    std::tuple<glog::ErrorCode, uint64_t> CreateANewLog(const std::string& logname);
    std::tuple<glog::ErrorCode, uint64_t> QueryLogId(const std::string& logname);

private:
    uint64_t svrid_;
    std::unique_ptr<glog::Glog::Stub> stub_;
};

} // namespace glog



