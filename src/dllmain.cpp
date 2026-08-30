#include <windows.h>

#include <filesystem>

#include "bootstrap.hpp"
#include "config.hpp"
#include "log.hpp"

namespace {

HMODULE g_module = nullptr;

std::filesystem::path module_directory() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

DWORD WINAPI initialise(LPVOID) {
    const std::filesystem::path directory = module_directory();
    logging::init(directory);
    load_config(directory / "AtelierReslerianaFix.ini");
    logging::set_enabled(g_config.enable_logging);

    LOG_INFO("AtelierReslerianaFix loaded from {}", directory.string());
    LOG_INFO("Target resolution {}x{}, fullscreen mode {}", g_config.target_width(),
             g_config.target_height(), g_config.fullscreen_mode);

    // No il2cpp call happens on this thread. bootstrap arranges for the real
    // work to run on the game's main thread once the runtime is initialised.
    bootstrap::install();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, initialise, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
