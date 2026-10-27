#pragma once

#include "PCH.h"

#include <format>
#include <string_view>
#include <utility>

// Log lines look like "[12:34:56] [compass] hidden: enemy=2". The time comes from the spdlog
// pattern set in Plugin.cpp; the tag names the area the line is about.
namespace OnYourOwn::Log
{
    template <class... Args>
    void Info(std::string_view a_tag, std::format_string<Args...> a_fmt, Args&&... a_args)
    {
        REX::INFO("[{}] {}", a_tag, std::format(a_fmt, std::forward<Args>(a_args)...));
    }

    template <class... Args>
    void Warn(std::format_string<Args...> a_fmt, Args&&... a_args)
    {
        REX::WARN("[warning] {}", std::format(a_fmt, std::forward<Args>(a_args)...));
    }

    template <class... Args>
    void Error(std::format_string<Args...> a_fmt, Args&&... a_args)
    {
        REX::ERROR("[error] {}", std::format(a_fmt, std::forward<Args>(a_args)...));
    }

    constexpr const char* OnOff(bool a_value) noexcept { return a_value ? "on" : "off"; }
}
