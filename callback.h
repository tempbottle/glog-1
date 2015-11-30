#pragma once

#include <memory>
#include <cassert>
#include "cqueue.h"
#include "paxos.pb.h"
#include "utils.h"



namespace glog {

template <typename StorageType>
class ReadCallBack {

public:
    ReadCallBack(StorageType& storage)
        : storage_(storage)
    {

    }

    std::unique_ptr<paxos::HardState> 
        operator()(uint64_t logid, uint64_t index)
    {
        assert(0 < index);
        return storage_.Get(logid, index);
    }

private:
    StorageType& storage_;
};

template <typename StorageType>
class WriteCallBack {

public:
    WriteCallBack(StorageType& storage)
        : storage_(storage)
    {

    }

    int operator()(const paxos::HardState& hs)
    {
        return storage_.Set(hs);
    }

private:
    StorageType& storage_;
};

class SendCallBack {

public:
    SendCallBack(
            CQueue<paxos::Message>& msg_queue)
        : msg_queue_(msg_queue)
    {

    }

    int operator()(const paxos::Message& rsp_msg)
    {
        assert(0 < rsp_msg.index());
        logdebug("TEST index %" PRIu64 " send msg", rsp_msg.index());
        msg_queue_.Push(std::make_unique<paxos::Message>(rsp_msg));
        return 0;
    }

private:
    CQueue<paxos::Message>& msg_queue_;
};

//template <typename StorageType>
//class CallBack {
//
//public:
//    CallBack(
//            StorageType& storage,  
//            CQueue<std::unique_ptr<paxos::Message>>& msg_queue)
//        : storage_(storage)
//        , msg_queue_(msg_queue)
//    {
//
//    }
//
//    int operator()(
//            std::unique_ptr<paxos::HardState> hs, 
//            std::unique_ptr<paxos::Message> msg)
//    {
//        int ret = 0;
//        if (nullptr != hs) {
//            assert(0 < hs->index());
//            ret = storage_.Set(*hs);
//            logdebug("TEST index %" PRIu64 " store hs ret %d", 
//                    hs->index(), ret);
//        }
//
//        if (0 == ret && nullptr != msg) {
//            assert(0 < msg->index());
//            logdebug("TEST index %" PRIu64 " send msg", msg->index());
//            msg_queue_.Push(move(msg));
//        }
//
//        return ret;
//    }
//
//private:
//    StorageType& storage_;
//    CQueue<std::unique_ptr<paxos::Message>>& msg_queue_;
//};


    
} // namespace glog



