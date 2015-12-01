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

    // retcode, max_index, commited_index
    std::tuple<
        glog::ErrorCode, uint64_t, uint64_t> GetPaxosInfo(uint64_t logid);

    void TryCatchUp();

    glog::ErrorCode 
        Set(uint64_t logid, uint64_t index, gsl::cstring_view<> data);

    // ret, commited_index, index-value
    std::tuple<glog::ErrorCode, uint64_t, std::string> 
        Get(uint64_t logid, uint64_t index);

    // ret, logid
    std::tuple<glog::ErrorCode, uint64_t> 
        CreateANewLog(const std::string& logname);

    // ret, logid
    std::tuple<glog::ErrorCode, uint64_t> 
        QueryLogId(const std::string& logname);

private:
    uint64_t svrid_;
    std::unique_ptr<glog::Glog::Stub> stub_;
};

} // namespace glog



