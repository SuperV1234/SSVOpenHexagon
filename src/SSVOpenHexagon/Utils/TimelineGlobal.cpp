// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Utils/TimelineGlobal.hpp"

#include <type_traits>
#include <utility>
#include <chrono>
#include <optional>
#include <string>
#include <functional>

namespace hg::Utils {

[[nodiscard]] int timelineGlobal::hasTimepoint(int32_t tp) const noexcept
{
    return _actions.contains(tp);
}

[[nodiscard]] timelineGlobal::timeline_G::iterator
timelineGlobal::begin() noexcept
{
    return _actions.begin();
}

[[nodiscard]] timelineGlobal::timeline_G::iterator
timelineGlobal::end() noexcept
{
    return _actions.end();
}

void timelineGlobal::clear()
{
    _actions.clear();
}

void timelineGlobal::clearTimepoint(int32_t tp)
{
    if(!hasTimepoint(tp))
    {
        return;
    }
    _actions[tp].clear();
}

void timelineGlobal::append_to(int32_t tp, const std::function<void()>& func)
{
    if(!hasTimepoint(tp))
    {
        _actions[tp] = std::vector<action>{};
    }
    _actions[tp].emplace_back(action{func});
}

[[nodiscard]] std::size_t timelineGlobal::size() const noexcept
{
    return _actions.size();
}

[[nodiscard]] std::vector<timelineGlobal::action>& timelineGlobal::actions_at(
    const int32_t tp)
{
    return _actions[tp];
}

[[nodiscard]] std::string timelineGlobal::to_string() noexcept
{
    std::string result = "CURRENT MUSIC TIMELINE MAP:\n";
    for(timelineGlobal::timeline_G::iterator it = begin(); it != end(); it++)
    {
        std::string keyString = std::to_string(it->first);
        result.append("\t[" + keyString + "]\n");
    }
    return result;
}

[[nodiscard]] std::vector<int32_t> timelineGlobal_runner::getRunnableTimepoints(
    timelineGlobal& timeline, const int32_t tp)
{
    std::vector<int32_t> result{};
    for(auto iter = timeline.begin(); iter != timeline.end(); iter++)
    {
        if(iter->first > prev_tp.value() && iter->first <= tp)
        {
            result.emplace_back(iter->first);
        }
    }
    return result;
}

void timelineGlobal_runner::update(timelineGlobal& timeline, int32_t tp)
{
    if(!prev_tp.has_value())
    {
        // Just simply grab the time point
        if(timeline.hasTimepoint(tp))
        {
            std::vector<action> actions = timeline.actions_at(tp);
            for(std::size_t i = 0; i < actions.size(); i++)
            {
                actions[i]._func();
            }
        }
    }
    else
    {
        // Grab all timepoints in between the previous and current timepoint and
        // run all of the functions contained.
        std::vector<int32_t> timepoints = getRunnableTimepoints(timeline, tp);
        for(std::size_t t = 0; t < timepoints.size(); t++)
        {
            std::vector<timelineGlobal::action> actions =
                timeline.actions_at(timepoints[t]);
            for(std::size_t i = 0; i < actions.size(); i++)
            {
                actions[i]._func();
            }
        }
    }
    // Update the previous timepoint with the new timepoint.
    prev_tp = tp;
}

void timelineGlobal_runner::clearLastTp()
{
    prev_tp.reset();
}

} // namespace hg::Utils