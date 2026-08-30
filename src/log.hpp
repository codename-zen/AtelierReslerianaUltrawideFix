#pragma once
#include <exception>
#include <filesystem>
#include <utility>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace logging {

// Writes AtelierReslerianaFix.log next to the .asi.
inline void init(const std::filesystem::path& directory) {
    try {
        auto logger = spdlog::basic_logger_mt(
            "AtelierReslerianaFix", (directory / "AtelierReslerianaFix.log").string(), true);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    } catch (const std::exception&) {
        // A read-only game folder must not stop the fix from loading.
    }
}

inline void set_enabled(bool enabled) {
    spdlog::default_logger()->set_level(enabled ? spdlog::level::info : spdlog::level::off);
}

} // namespace logging

#define LOG_INFO(...) ::spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) ::spdlog::error(__VA_ARGS__)
