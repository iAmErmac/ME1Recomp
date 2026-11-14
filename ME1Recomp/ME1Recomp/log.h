#pragma once

#include <fmt/core.h>
#include <string_view>

namespace me1::log
{
    enum class Level { Trace, Info, Warn, Error };

    void Init(const std::filesystem::path& path);
    void Write(Level level, std::string_view message);
    bool TraceEnabled();
}

#define LOGT(...) do { if (::me1::log::TraceEnabled()) ::me1::log::Write(::me1::log::Level::Trace, fmt::format(__VA_ARGS__)); } while (0)
#define LOGI(...) ::me1::log::Write(::me1::log::Level::Info, fmt::format(__VA_ARGS__))
#define LOGW(...) ::me1::log::Write(::me1::log::Level::Warn, fmt::format(__VA_ARGS__))
#define LOGE(...) ::me1::log::Write(::me1::log::Level::Error, fmt::format(__VA_ARGS__))
