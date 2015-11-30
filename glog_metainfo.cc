#include "glog_metainfo.h"
#include "utils/async_worker.h"
#include "utils/data_helper.h"
#include "paxos.h"
#include "glog.pb.h"


using namespace std;

namespace glog {

namespace {

uint64_t METAINFO_LOGID = 0;

void AsyncApplyWorker(
        GlogMetaInfo* metainfo, uint64_t apply_index, 
        std::atomic<bool>& stop)
{
    assert(nullptr != metainfo);

    paxos::Paxos& meta_log = metainfo->GetMetaInfoLog();
    while (true) {
        uint64_t commited_index = meta_log.GetCommitedIndex();
        for (; apply_index < commited_index; ++apply_index) {

            int ret = metainfo->ApplyLogEntry(apply_index + 1);
            hassert(0 <= ret, "ApplyLogEntry apply_index %" PRIu64 
                    " ret %d", apply_index + 1, ret);
        }

        if (true == stop) {
            break;
        }

        // TODO: add wait timeout!
        meta_log.WaitFor(commited_index + 1, chrono::milliseconds{1});
    }

    logerr("INFO %s exist", __func__);
    return ;
}

}


GlogMetaInfo::GlogMetaInfo(ReadCBType readcb, PaxosLogCreater plog_creater)
    : readcb_(readcb)
    , plog_creater_(plog_creater)
{
    assert(nullptr != readcb);
    assert(nullptr != plog_creater_);
    meta_log_ = plog_creater_(METAINFO_LOGID);

    uint64_t apply_index = 0;
    {
        lock_guard<mutex> lock(meta_log_mutex_);
        assert(METAINFO_LOGID <= max_assigned_logid_);
        apply_index = meta_apply_index_;
    }

    async_worker_ = 
        make_unique<AsyncWorker>(AsyncApplyWorker, this, apply_index);
    assert(nullptr != async_worker_);
}

GlogMetaInfo::~GlogMetaInfo() = default;


paxos::Paxos* GlogMetaInfo::GetPaxosLog(uint64_t logid)
{
    if (METAINFO_LOGID == logid) {
        assert(nullptr != meta_log_);
        return meta_log_.get();
    }

    lock_guard<mutex> lock(meta_log_mutex_);
    if (map_plog_.end() == map_plog_.find(logid)) {
        // maybe local out
        return nullptr;
    }

    assert(logid <= max_assigned_logid_);
    assert(nullptr != map_plog_[logid]);
    return map_plog_[logid].get();
}


int GlogMetaInfo::ApplyLogEntry(uint64_t apply_index)
{
    assert(0 < apply_index);
    auto hs = readcb_(METAINFO_LOGID, apply_index);
    assert(nullptr != hs);
    assert(METAINFO_LOGID == hs->logid());
    assert(apply_index == hs->index());

    auto chosen_value = Pickle<ProposeValue>(hs->accepted_value());
    auto metainfo_entry = Pickle<MetaInfoEntry>(chosen_value->data());
    assert(0 < metainfo_entry->logid());
    assert(false == metainfo_entry->logname().empty());

    auto new_plog = plog_creater_(metainfo_entry->logid());
    assert(nullptr != new_plog);

    lock_guard<mutex> lock(meta_log_mutex_);
    if (apply_index != meta_apply_index_ + 1) {
        return apply_index <= meta_apply_index_ ? 1 : -1;
    }

    assert(state_.end() == state_.find(metainfo_entry->logname()));
    assert(map_plog_.end() == map_plog_.find(metainfo_entry->logid()));
    assert(apply_index == meta_apply_index_ + 1);
    assert(metainfo_entry->logid() == max_assigned_logid_ + 1);

    state_[metainfo_entry->logname()] = metainfo_entry->logid();
    map_plog_[metainfo_entry->logid()] = move(new_plog);
    assert(nullptr == new_plog);
    assert(nullptr != map_plog_[metainfo_entry->logid()]);
    meta_apply_index_ = apply_index;
    max_assigned_logid_ = metainfo_entry->logid();
    return 0;
}

std::tuple<std::unique_ptr<glog::SetRequest>, uint64_t>
GlogMetaInfo::PackMetaInfoEntry(const std::string& logname)
{
    assert(false == logname.empty());

    uint64_t new_logid = 0;
    uint64_t set_index = 0;
    {
        lock_guard<mutex> lock(meta_log_mutex_);
        if (state_.end() != state_.find(logname)) {
            uint64_t logid = state_[logname];
            assert(0 < logid);
            assert(map_plog_.end() != map_plog_.find(logid));
            assert(nullptr != map_plog_[logid]);
            return make_tuple(nullptr, logid); // already exist
        }

        new_logid = max_assigned_logid_ + 1;
        set_index = meta_apply_index_ + 1;
    }

    string value;
    {
        MetaInfoEntry entry;
        entry.set_logid(new_logid);
        entry.set_logname(logname);
        entry.set_timestamp(static_cast<int>(time(nullptr)));

        stringstream ss;
        assert(true == entry.SerializeToOstream(&ss));
        value = ss.str();
    }

    auto request = make_unique<glog::SetRequest>();
    assert(nullptr != request);
    request->set_logid(METAINFO_LOGID);
    request->set_index(set_index);
    request->set_data(value);
    return make_tuple(move(request), new_logid);
}

uint64_t GlogMetaInfo::QueryLogId(const std::string& logname)
{
    assert(false == logname.empty());

    lock_guard<mutex> lock(meta_log_mutex_);
    if (state_.end() == state_.find(logname)) {
        return 0; // don't exist
    }

    uint64_t logid = state_[logname];
    assert(0 < logid);
    assert(map_plog_.end() != map_plog_.find(logid));
    assert(nullptr != map_plog_[logid]);
    return logid;
}

std::set<uint64_t> GlogMetaInfo::GetAllLogId()
{
    set<uint64_t> logid_set;
    lock_guard<mutex> lock(meta_log_mutex_);
    for (const auto& piter : map_plog_) {
        logid_set.insert(piter.first); 
    }

    assert(logid_set.end() == logid_set.find(METAINFO_LOGID));
    logid_set.insert(METAINFO_LOGID);
    return logid_set;
}


} // namespace glog


