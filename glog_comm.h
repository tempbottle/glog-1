#pragma once

#include <functional>
#include <memory>
#include <stdint.h>


namespace paxos {
    class HardState;
} // namespace paxos

namespace glog {


using ReadCB = std::function<
    std::unique_ptr<paxos::HardState>(uint64_t, uint64_t)>;
using WriteCB = std::function<int(const paxos::HardState&)>;



} // namespace glog



