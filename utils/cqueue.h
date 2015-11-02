#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cassert>

template <typename EntryType>
class CQueue {

public:
    void Push(EntryType item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            msg_.emplace(std::move(item));
        }

        cv_.notify_one();
    }

    EntryType Pop()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (msg_.empty()) {
                cv_.wait(lock, [&]() {
                    return !msg_.empty();
                });
            }

            assert(false == msg_.empty());
            auto item = move(msg_.front());
            msg_.pop();
            return item;
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<paxos::Message>> msg_;
};




