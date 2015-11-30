#pragma once

#include <map>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <cassert>
#include <stdint.h>
#include "glog_comm.h"

namespace paxos {

class Paxos;

};

namespace glog {

class AsyncWorker;
class SetRequest;


using PaxosLogCreater = 
    std::function<std::unique_ptr<paxos::Paxos>(uint64_t logid)>;

class GlogMetaInfo {

public:
    GlogMetaInfo(ReadCB readcb, PaxosLogCreater plog_creater);

    ~GlogMetaInfo();

    std::tuple<std::unique_ptr<glog::SetRequest>, uint64_t>
        PackMetaInfoEntry(const std::string& logname);

    uint64_t QueryLogId(const std::string& logname);

    paxos::Paxos& GetMetaInfoLog() {
        assert(nullptr != meta_log_);
        return *meta_log_;
    }

    paxos::Paxos* GetPaxosLog(uint64_t logid);

    int ApplyLogEntry(uint64_t apply_index);

private:
    ReadCB readcb_; 
    PaxosLogCreater plog_creater_;

    std::unique_ptr<paxos::Paxos> meta_log_;

    // protected by meta_log_mutex_;
    std::mutex meta_log_mutex_;
    uint64_t meta_apply_index_ = 0;
    uint64_t max_assigned_logid_ = 0;

    std::map<std::string, uint64_t> state_;
    std::map<uint64_t, std::unique_ptr<paxos::Paxos>> map_plog_;
    // end of protected 
    
    std::unique_ptr<AsyncWorker> async_worker_;
}; 


} // namespace glog


