#pragma once

#include <memory>
#include <cassert>
#include "cqueue.h"
#include "paxos.pb.h"
#include "utils.h"


namespace glog {

template <typename StorageType>
class CallBack {

public:
    CallBack(
            StorageType& storage,  
            CQueue<std::unique_ptr<paxos::Message>>& msg_queue)
        : storage_(storage)
        , msg_queue_(msg_queue)
    {

    }

    int operator()(
            std::unique_ptr<paxos::HardState> hs, 
            std::unique_ptr<paxos::Message> msg)
    {
        int ret = 0;
        if (nullptr != hs) {
            assert(0 < hs->index());
            ret = storage_.Set(*hs);
            logdebug("TEST index %" PRIu64 " store hs ret %d", 
                    hs->index(), ret);
        }

        if (0 == ret && nullptr != msg) {
            assert(0 < msg->index());
            logdebug("TEST index %" PRIu64 " send msg", msg->index());
            msg_queue_.Push(move(msg));
        }

        return ret;
    }

private:
    StorageType& storage_;
    CQueue<std::unique_ptr<paxos::Message>>& msg_queue_;
};


    
} // namespace glog



