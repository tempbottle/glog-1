#pragma once

#include <iostream>
#include <sstream>
#include <map>
#include <mutex>
#include <memory>
#include <stdint.h>
#include <cstring>
#include <cassert>
#include "paxos.pb.h"


namespace glog {

class MemStorage {

public:
    int Set(const paxos::HardState& hs)
    {
        assert(0 < hs.index());
        auto key = MakeKey(hs.logid(), hs.index());
        // storage_[key] = std::make_unique<paxos::HardState>(hs);
        // printf ( "key %s hs %p\n", key.c_str(), storage_[key].get() );
        // assert(nullptr != storage_[key]);
        std::lock_guard<std::mutex> lock(mutex_);
        storage_[key].CopyFrom(hs);
        printf ( "set %p key %s storage_.size %d\n", 
                this, key.c_str(), static_cast<int>(storage_.size()));
        return 0;
    }

    std::unique_ptr<paxos::HardState> Get(uint64_t logid, uint64_t index)
    {
        auto key = MakeKey(logid, index);

        std::lock_guard<std::mutex> lock(mutex_);

        printf ( "get %p key %s storage_.size %d\n", 
                this, key.c_str(), static_cast<int>(storage_.size()) );
        if (storage_.end() == storage_.find(key)) {
            return nullptr;
        }

        return std::make_unique<paxos::HardState>(storage_[key]);
        // assert(nullptr != storage_[key]);
        // auto hs = std::make_unique<paxos::HardState>(*storage_[key]);
        // assert(nullptr != hs);
        // printf ( "get key %s hs %p\n", key.c_str(), hs.get() );
        // hs->CopyFrom(*(storage_[key]));
        // return hs;
    }

private:
    std::string MakeKey(uint64_t logid, uint64_t index)
    {
        std::stringstream ss;
        ss << logid << "_" << index;
        return ss.str();
//        std::string key(sizeof(uint64_t) * 2, 0);
//        assert(key.size() == sizeof(uint64_t) * 2);
//        memcpy(&key[0], &logid, sizeof(logid));
//        memcpy(&key[0] + sizeof(uint64_t), &index, sizeof(index));
//        return key;
    }

private:
    std::mutex mutex_;
    std::map<std::string, paxos::HardState> storage_;
};


}; 



