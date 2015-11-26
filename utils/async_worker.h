#pragma once

#include <future>
#include <atomic>

namespace glog {

class AsyncWorker {

public:

    template <typename WorkerType, typename ...Args>
    AsyncWorker(WorkerType func, Args... args)
        : stop_(false)
        , worker_(std::async(std::launch::async, func, args..., std::ref(stop_)))
    {

    }

    ~AsyncWorker() {
        stop_ = true;
        worker_.get();
    }

    // delete: copy construct, move construct, assginment..

private:
    std::atomic<bool> stop_;
    std::future<void> worker_;
};


} // namespace glog


