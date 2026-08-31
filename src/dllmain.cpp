#include <windows.h>

#include <filesystem>
#include <string.h>

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

// The loader is named after a DLL the game imports, and it is not the only
// thing in that folder importing it: UnityCrashHandler64.exe imports VERSION.dll
// too, so it loads this plugin as well. That process has no GameAssembly.dll,
// so the copy running there span for a full minute and then wrote "nothing was
// hooked" into the same log file, which read like a failure in the game itself.
bool running_in_the_game() {
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0)
        return true; // Cannot tell, so carry on rather than silently do nothing.

    const std::filesystem::path host = std::filesystem::path(path).filename();
    return _stricmp(host.string().c_str(), "AtelierReslerianaRW.exe") == 0;
}

DWORD WINAPI initialise(LPVOID) {
    // Checked before the log is even opened, so the wrong process never touches
    // the file the game's own copy is writing to.
    if (!running_in_the_game())
        return 0;

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
