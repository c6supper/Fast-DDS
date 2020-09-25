// Copyright 2020 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file DiscoveryDataBase.cpp
 *
 */

#include <mutex>
#include <shared_mutex>

#include <fastdds/dds/log/Log.hpp>

#include "./DiscoveryDataBase.hpp"

namespace eprosima {
namespace fastdds {
namespace rtps {
namespace ddb {

bool DiscoveryDataBase::pdp_is_relevant(
        const eprosima::fastrtps::rtps::CacheChange_t& change,
        const eprosima::fastrtps::rtps::GUID_t& reader_guid) const
{
    auto it = participants_.find(guid_from_change(&change).guidPrefix);
    if (it != participants_.end())
    {
        // it is relevant if the ack has not been received yet
        // in NOT_ALIVE case the set_disposals unmatch every participant
        return !it->second.is_matched(reader_guid.guidPrefix);
    }
    // not relevant
    return false;
}

bool DiscoveryDataBase::edp_publications_is_relevant(
        const eprosima::fastrtps::rtps::CacheChange_t& change,
        const eprosima::fastrtps::rtps::GUID_t& reader_guid) const
{
    auto itp = participants_.find(guid_from_change(&change).guidPrefix);
    if (itp == participants_.end())
    {
        // not relevant
        return false;
    }

    if (!itp->second.is_matched(reader_guid.guidPrefix))
    {
        // participant still not matched
        return false;
    }

    auto itw = writers_.find(guid_from_change(&change));
    if (itw != writers_.end())
    {
        // it is relevant if the ack has not been received yet
        return !itw->second.is_matched(reader_guid.guidPrefix);
    }
    // not relevant
    return false;
}

bool DiscoveryDataBase::edp_subscriptions_is_relevant(
        const eprosima::fastrtps::rtps::CacheChange_t& change,
        const eprosima::fastrtps::rtps::GUID_t& reader_guid) const
{
    auto itp = participants_.find(guid_from_change(&change).guidPrefix);
    if (itp == participants_.end())
    {
        // not relevant
        return false;
    }

    if (!itp->second.is_matched(reader_guid.guidPrefix))
    {
        // participant still not matched
        return false;
    }

    auto itr = readers_.find(guid_from_change(&change));
    if (itr != readers_.end())
    {
        // it is relevant if the ack has not been received yet
        return !itr->second.is_matched(reader_guid.guidPrefix);
    }
    // not relevant
    return false;
}

void DiscoveryDataBase::add_ack_(
        const eprosima::fastrtps::rtps::CacheChange_t* change,
        const eprosima::fastrtps::rtps::GuidPrefix_t& acked_entity)
{
    if (is_participant(change))
    {
        auto it = participants_.find(guid_from_change(change).guidPrefix);
        it->second.add_or_update_ack_participant(acked_entity, true);
    }
}

bool DiscoveryDataBase::update(
        eprosima::fastrtps::rtps::CacheChange_t* change,
        eprosima::fastrtps::string_255 topic_name)
{
    //  add the data to the queue to process
    data_queue_.Push(eprosima::fastdds::rtps::ddb::DiscoveryDataQueueInfo(change, topic_name));

    // not way to check if is an error
    return true;
}

const std::vector<eprosima::fastrtps::rtps::CacheChange_t*> DiscoveryDataBase::changes_to_dispose()
{
    // lock(sharing mode) mutex locally
    std::shared_lock<std::shared_timed_mutex> lock(sh_mtx_);
    return disposals_;
}

void DiscoveryDataBase::clear_changes_to_dispose()
{
    // lock(exclusive mode) mutex locally
    std::unique_lock<std::shared_timed_mutex> lock(sh_mtx_);
    disposals_.clear();
}

////////////
// Functions to process_to_send_lists()
const std::vector<eprosima::fastrtps::rtps::CacheChange_t*> DiscoveryDataBase::pdp_to_send()
{
    // lock(sharing mode) mutex locally
    std::shared_lock<std::shared_timed_mutex> lock(sh_mtx_);
    return pdp_to_send_;
}

void DiscoveryDataBase::clear_pdp_to_send()
{
    // lock(exclusive mode) mutex locally
    std::unique_lock<std::shared_timed_mutex> lock(sh_mtx_);
    pdp_to_send_.clear();
}

const std::vector<eprosima::fastrtps::rtps::CacheChange_t*> DiscoveryDataBase::edp_publications_to_send()
{
    // lock(sharing mode) mutex locally
    std::shared_lock<std::shared_timed_mutex> lock(sh_mtx_);
    return edp_publications_to_send_;
}

void DiscoveryDataBase::clear_edp_publications_to_send()
{
    // lock(exclusive mode) mutex locally
    std::unique_lock<std::shared_timed_mutex> lock(sh_mtx_);
    edp_publications_to_send_.clear();
}

const std::vector<eprosima::fastrtps::rtps::CacheChange_t*> DiscoveryDataBase::edp_subscriptions_to_send()
{
    // lock(sharing mode) mutex locally
    std::shared_lock<std::shared_timed_mutex> lock(sh_mtx_);
    return edp_subscriptions_to_send_;
}

void DiscoveryDataBase::clear_edp_subscriptions_to_send()
{
    // lock(exclusive mode) mutex locally
    std::unique_lock<std::shared_timed_mutex> lock(sh_mtx_);
    edp_subscriptions_to_send_.clear();
}

bool DiscoveryDataBase::process_data_queue()
{
    // std::unique_lock<std::mutex> guard(sh_mutex);
    data_queue_.Swap();
    while (!data_queue_.Empty())
    {
        DiscoveryDataQueueInfo data_queue_info = data_queue_.Front();

        if (data_queue_info.cache_change()->kind == eprosima::fastrtps::rtps::ALIVE)
        {
            // update(participants_);
        }

        data_queue_.Pop();
    }


    return false;
}

bool DiscoveryDataBase::process_dirty_topics()
{
    return true;
}

bool DiscoveryDataBase::delete_entity_of_change(
        fastrtps::rtps::CacheChange_t* change)
{

    if (change->kind != fastrtps::rtps::ChangeKind_t::ALIVE)
    {
        logWarning(DISCOVERY_DATABASE,
                "Attempting to delete information of an ALIVE entity: " << guid_from_change(change));
        return false;
    }

    if (is_participant(change))
    {
        return participants_.erase(guid_from_change(change).guidPrefix);
    }
    else if (is_reader(change))
    {
        return readers_.erase(guid_from_change(change));
    }
    else if (is_writer(change))
    {
        return writers_.erase(guid_from_change(change));
    }
    return false;
}

bool DiscoveryDataBase::is_participant(
        const eprosima::fastrtps::rtps::CacheChange_t* ch)
{
    (void) ch;
    return true;
}

bool DiscoveryDataBase::is_writer(
        const eprosima::fastrtps::rtps::CacheChange_t* ch)
{
    (void) ch;
    return true;
}

bool DiscoveryDataBase::is_reader(
        const eprosima::fastrtps::rtps::CacheChange_t* ch)
{
    (void) ch;
    return true;
}

eprosima::fastrtps::rtps::GUID_t DiscoveryDataBase::guid_from_change(
        const eprosima::fastrtps::rtps::CacheChange_t* ch)
{
    return fastrtps::rtps::iHandle2GUID(ch->instanceHandle);
}

DiscoveryDataBase::AckedFunctor::AckedFunctor(
        DiscoveryDataBase* db,
        eprosima::fastrtps::rtps::CacheChange_t* change)
    : db_(db)
    , change_(change)
{
    db_->exclusive_lock_();
}

DiscoveryDataBase::AckedFunctor::~AckedFunctor()
{
    db_->exclusive_unlock_();
}

void DiscoveryDataBase::AckedFunctor::operator () (
        eprosima::fastrtps::rtps::ReaderProxy* reader_proxy)
{
    // Check whether the change has been acknowledged by a given reader
    bool is_acked = reader_proxy->change_is_acked(change_->sequenceNumber);
    if (is_acked)
    {
        // In the discovery database, mark the change as acknowledged by the reader
        db_->add_ack_(change_, reader_proxy->guid().guidPrefix);
    }
    pending_ |= !is_acked;
}

} // namespace ddb
} // namespace rtps
} // namespace fastdds
} // namespace eprosima