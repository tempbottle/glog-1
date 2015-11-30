#include <chrono>
#include "glog_client_impl.h"
#include "utils.h"
#include "glog.pb.h"
#include "paxos.pb.h"
#include "cqueue.h"


using grpc::ClientContext;
using grpc::Status;
using namespace std;


namespace {

const int DEFAULT_TIMEOUT = 100;

void set_deadline(grpc::ClientContext& context, int milliseconds)
{
    context.set_deadline(
            std::chrono::system_clock::now() + 
            std::chrono::milliseconds{milliseconds});
}


}

namespace glog {


GlogClientImpl::GlogClientImpl(
        const uint64_t svrid, 
        std::shared_ptr<grpc::Channel> channel)
    : svrid_(svrid)
    , stub_(glog::Glog::NewStub(channel))
{
    assert(0 < svrid_);
    assert(nullptr != channel);
}

GlogClientImpl::~GlogClientImpl() = default;

void GlogClientImpl::PostMsg(const paxos::Message& msg)
{
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);
    hassert(msg.to_id() == svrid_, "msg.peer_id %" PRIu64 
            " svrid_ %" PRIu64 "", msg.peer_id(), svrid_);
    Status status = stub_->PostMsg(&context, msg, &reply);
    if (status.ok()) {
        logdebug("index %" PRIu64 " PostMsg succ", msg.index());
    } else {
        logerr("index %" PRIu64 " PostMsg failed", msg.index());
    }

    return ;
}

int GlogClientImpl::Propose(gsl::cstring_view<> data)
{
    ProposeRequest request;
    request.set_data(data.data(), data.size());
    
    ProposeResponse reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);
    Status status = stub_->Propose(&context, request, &reply);
    printf ( "ret from Propose\n" );
    if (status.ok()) {
        return reply.retcode();
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return static_cast<int>(status.error_code());
}

std::tuple<glog::ErrorCode, uint64_t, uint64_t> 
GlogClientImpl::GetPaxosInfo(uint64_t logid)
{
    LogId request;
    request.set_logid(logid);

    PaxosInfoResponse reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->GetPaxosInfo(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("Propose failed error_code %d error_message %s", 
                static_cast<int>(status.error_code()), error_message.c_str());
        return make_tuple(ErrorCode::GRPC_ERROR, 0ull, 0ull);
    }

    assert(true == status.ok());
    assert(ErrorCode::OK == reply.ret() || 
            ErrorCode::LOGID_DONT_EXIST == reply.ret());
    if (ErrorCode::LOGID_DONT_EXIST == reply.ret()) {
        return make_tuple(reply.ret(), 0ull, 0ull);
    }

    return make_tuple(reply.ret(), reply.max_index(), reply.commited_index());
}

void GlogClientImpl::TryCatchUp()
{
    NoopMsg request;
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->TryCatchUp(&context, request, &reply);
    if (status.ok()) {
        return ;
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return ;
}

void GlogClientImpl::TryPropose(uint64_t index)
{
    assert(0 < index);
    TryProposeRequest request;
    request.set_index(index);
    NoopMsg reply;

    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->TryPropose(&context, request, &reply);
    if (status.ok()) {
        return ;
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return ;
}

std::tuple<std::string, std::string> GlogClientImpl::GetGlog(uint64_t index)
{
    assert(0 < index);
    GetGlogRequest request;
    request.set_index(index);

    GetGlogResponse reply;

    ClientContext context; 
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->GetGlog(&context, request, &reply);
    if (status.ok()) {
        return std::make_tuple(reply.info(), reply.data());
    }

    auto error_message = status.error_message();
    logerr("Propose failed error_code %d error_message %s", 
            static_cast<int>(status.error_code()), error_message.c_str());
    return std::make_tuple(move(error_message), "");
}

glog::ErrorCode 
GlogClientImpl::Set(uint64_t logid, uint64_t index, gsl::cstring_view<> data)
{
    SetRequest request;
    request.set_logid(logid);
    request.set_index(index);
    request.set_data(data.data(), data.size());

    RetCode reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->Set(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("logid %" PRIu64 " index %" PRIu64 
                " failed error_code %d error_message %s", logid, index, 
                static_cast<int>(status.error_code()), error_message.c_str());
        return ErrorCode::GRPC_ERROR;
    }

    if (ErrorCode::OK != reply.ret()) {
        logerr("logid %" PRIu64 " index %" PRIu64 " Set ret %d", 
                logid, index, reply.ret());
        return reply.ret();
    }

    logdebug("logid %" PRIu64 " index %" PRIu64 " Set success", logid, index);
    return ErrorCode::OK; // success
}

std::tuple<glog::ErrorCode, uint64_t, std::string> 
GlogClientImpl::Get(uint64_t logid, uint64_t index)
{
    if (0 == index) {
        return std::make_tuple(ErrorCode::INVALID_PARAMS, 0ull, "");
    }

    assert(0 < index);
    GetRequest request;
    request.set_logid(logid);
    request.set_index(index);

    GetResponse reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    Status status = stub_->Get(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("logid %" PRIu64 " index %" PRIu64
                "failed error_code %d error_message %s", logid, index, 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(ErrorCode::GRPC_ERROR, 0ull, "");
    }

    if (ErrorCode::OK != reply.ret() &&
            ErrorCode::UNCOMMITED_INDEX != reply.ret()) {
        logerr("logid %" PRIu64 " failed index %" PRIu64 " ret %d", 
                logid, index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull, "");
    }

    logdebug("logid %" PRIu64 " index %" PRIu64 " Get success", logid, index);
    return std::make_tuple(reply.ret(), reply.commited_index(), 
            ErrorCode::OK != reply.ret() ? "" : reply.data());
}

std::tuple<glog::ErrorCode, uint64_t>
GlogClientImpl::CreateANewLog(const std::string& logname)
{
    if (true == logname.empty()) {
        return make_tuple(ErrorCode::INVALID_PARAMS, 0ull);
    }

    assert(false == logname.empty());
    LogName request;
    request.set_logname(logname);

    LogIdResponse reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    auto status = stub_->CreateANewLog(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("%s failed error_code %d error_message %s", logname.c_str(), 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(ErrorCode::GRPC_ERROR, 0ull);
    }

    if (ErrorCode::OK != reply.ret() &&
            ErrorCode::UNCOMMITED_INDEX != reply.ret()) {
        logerr("failed index %" PRIu64 " ret %d", index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull);
    }

    logdebug("INFO logname %s %s ret %d logid %" PRIu64, 
            logname.c_str(), __func__, reply.ret(), reply.logid());
    return make_tuple(reply.ret(), reply.logid());
}

std::tuple<glog::ErrorCode, uint64_t>
GlogClientImpl::QueryLogId(const std::string& logname)
{
    if (true == logname.empty()) {
        return make_tuple(ErrorCode::INVALID_PARAMS, 0ull);
    }

    assert(false == logname.empty());
    LogName request;
    request.set_logname(logname);

    LogIdResponse reply;
    ClientContext context;
    set_deadline(context, DEFAULT_TIMEOUT);

    auto status = stub_->QueryLogId(&context, request, &reply);
    if (!status.ok()) {
        auto error_message = status.error_message();
        logerr("%s failed error_code %d error_message %s", logname.c_str(), 
                static_cast<int>(status.error_code()), error_message.c_str());
        return std::make_tuple(ErrorCode::GRPC_ERROR, 0ull);
    }

    if (ErrorCode::OK != reply.ret() &&
            ErrorCode::LOGNAME_DONT_EXIST) {
        logerr("failed index %" PRIu64 " ret %d", index, reply.ret());
        return std::make_tuple(reply.ret(), 0ull);
    }

    logdebug("INFO logname %s %s ret %d logid %" PRIu64, 
            logname.c_str(), __func__, reply.ret(), reply.logid());
    return make_tuple(reply.ret(), reply.logid());
}


} // namespace glog



