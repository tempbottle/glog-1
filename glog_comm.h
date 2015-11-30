#pragma once

#include <functional>
#include <memory>
#include <stdint.h>
#include "utils/cqueue.h"


namespace paxos {
    class HardState;
    class Message;
} // namespace paxos

namespace glog {


using ReadCBType = std::function<
    std::unique_ptr<paxos::HardState>(uint64_t, uint64_t)>;
using WriteCBType = std::function<int(const paxos::HardState&)>;

using MessageQueue = CQueue<paxos::Message>;



} // namespace glog



