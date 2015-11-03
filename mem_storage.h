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
        printf ( "DEBUG::TEST SET 1 accepted_value %" PRIu64 "\n", 
                hs.accepted_value().size() );
        storage_.emplace(hs.index(), 
                std::unique_ptr<paxos::HardState>{new paxos::HardState});
        storage_[hs.index()]->CopyFrom(hs);
        printf ( "DEBUG::TEST SET 2 accepted_value %" PRIu64 "\n", 
                storage_[hs.index()]->accepted_value().size() );
        return 0;
    }

    std::unique_ptr<paxos::HardState> Get(uint64_t index)
    {
        if (storage_.end() == storage_.find(index)) {
            return nullptr;
        }

        assert(nullptr != storage_[index]);
        printf ( "DEBUG::TEST GET accepted_value %" PRIu64 "\n", 
                storage_[index]->accepted_value().size());

        auto hs = std::unique_ptr<paxos::HardState>{new paxos::HardState};
        hs->CopyFrom(*(storage_[index]));
        return hs;
    }

private:
    std::map<uint64_t, std::unique_ptr<paxos::HardState>> storage_;
};


}; 



