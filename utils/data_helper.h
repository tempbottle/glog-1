#pragma once

#include <memory>
#include <string>
#include <sstream>

namespace glog {


template <typename T>
std::unique_ptr<T> Pickle(const std::string& raw_data)
{
    T value;
    {
        std::stringstream ss;
        ss.str(raw_data);
        if (false == value.ParseFromIstream(&ss)) {
            return nullptr;
        }
    }

    return std::make_unique<T>(std::move(value));
}




} // namespace glog


