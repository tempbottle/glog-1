#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include "paxos.pb.h"


namespace glog {

class MemStorage {

public:
    int Set(const paxos::HardState& hs)
    {
        assert(0 < hs.index());
        storage_.emplace(hs.index(), 
                std::unique_ptr<paxos::HardState>{new paxos::HardState{hs}});
        return 0;
    }

    std::unique_ptr<paxos::HardState> Get(uint64_t index)
    {
        if (storage_.end() == storage_.find(index)) {
            return nullptr;
        }

        assert(nullptr != storage_[index]);
        return std::unique_ptr<paxos::HardState>{
            new paxos::HardState{*storage_[index]}};
    }

private:
    std::map<uint64_t, std::unique_ptr<paxos::HardState>> storage_;
};


}; 



