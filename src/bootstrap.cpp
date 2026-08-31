#include "bootstrap.hpp"

#include <windows.h>

#include <atomic>
#include <string.h>
#include <wchar.h>

#include <safetyhook.hpp>

#include "config.hpp"
#include "fixes.hpp"
#include "il2cpp_api.hpp"
#include "log.hpp"
#include "unity_bindings.hpp"

// Everything here exists to answer one question safely: when is it legal to
// call into il2cpp?
//
// Doing it from our own thread as soon as il2cpp_domain_get() returns non-null
// crashes the game with "Fatal error in GC: Collecting from unknown thread".
// The domain pointer appears part-way through il2cpp_init, while the garbage
// collector is still coming up, so an allocating call from a thread the GC has
// never seen aborts the process.
//
// The reliable answer is to never touch il2cpp from our own thread at all.
// We intercept LoadLibrary to catch the moment GameAssembly.dll is mapped,
// hook the exported il2cpp_init before the game can call it, and run our setup
// from inside that hook, after the original returns. That puts the work on the
// game's own main thread with the runtime fully initialised, which is the same
// approach BepInEx's doorstop takes.

namespace bootstrap {
namespace {

SafetyHookInline g_il2cpp_init{};
SafetyHookInline g_load_library_a{};
SafetyHookInline g_load_library_w{};
SafetyHookInline g_load_library_ex_a{};
SafetyHookInline g_load_library_ex_w{};

constexpr int kSetupTimeoutMs = 60000;
constexpr int kPollIntervalMs = 10;

std::atomic<bool> g_setup_done{false};
std::atomic_flag g_init_hook_installed = ATOMIC_FLAG_INIT;

void setup() {
    if (g_setup_done.exchange(true))
        return;

    if (!g_config.enabled) {
        LOG_INFO("Disabled by configuration; nothing will be hooked.");
        return;
    }

    if (!il2cpp::bind()) {
        LOG_ERROR("Could not bind the il2cpp exports; giving up.");
        return;
    }

    if (!unity::resolve()) {
        LOG_ERROR("Could not resolve the Unity bindings; giving up.");
        return;
    }

    if (fixes::install())
        LOG_INFO("Hooks installed.");
    else
        LOG_ERROR("No hooks installed.");
}

// il2cpp_init(const char* domain_name) -> int
int il2cpp_init_hook(const char* domain_name) {
    const int result = g_il2cpp_init.unsafe_call<int>(domain_name);
    // The runtime is up and we are on the main thread: the only safe moment.
    setup();
    return result;
}

bool hook_il2cpp_init() {
    HMODULE module = GetModuleHandleA("GameAssembly.dll");
    if (!module)
        return false;

    void* target = reinterpret_cast<void*>(GetProcAddress(module, "il2cpp_init"));
    if (!target) {
        LOG_ERROR("GameAssembly.dll exports no il2cpp_init");
        return false;
    }

    if (g_init_hook_installed.test_and_set())
        return true;

    g_il2cpp_init = safetyhook::create_inline(target, reinterpret_cast<void*>(&il2cpp_init_hook));
    if (g_il2cpp_init) {
        LOG_INFO("Hooked il2cpp_init at {}", target);
        return true;
    }

    LOG_ERROR("Failed to hook il2cpp_init");
    return false;
}

bool is_game_assembly(const char* path) {
    if (!path)
        return false;
    const char* name = strrchr(path, '\\');
    return _stricmp(name ? name + 1 : path, "GameAssembly.dll") == 0;
}

bool is_game_assembly(const wchar_t* path) {
    if (!path)
        return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"GameAssembly.dll") == 0;
}

// These detours run under the loader lock, so they stay minimal and never log.
HMODULE WINAPI load_library_a_hook(LPCSTR path) {
    HMODULE result = g_load_library_a.unsafe_call<HMODULE>(path);
    if (result && is_game_assembly(path))
        hook_il2cpp_init();
    return result;
}

HMODULE WINAPI load_library_w_hook(LPCWSTR path) {
    HMODULE result = g_load_library_w.unsafe_call<HMODULE>(path);
    if (result && is_game_assembly(path))
        hook_il2cpp_init();
    return result;
}

HMODULE WINAPI load_library_ex_a_hook(LPCSTR path, HANDLE file, DWORD flags) {
    HMODULE result = g_load_library_ex_a.unsafe_call<HMODULE>(path, file, flags);
    if (result && is_game_assembly(path))
        hook_il2cpp_init();
    return result;
}

HMODULE WINAPI load_library_ex_w_hook(LPCWSTR path, HANDLE file, DWORD flags) {
    HMODULE result = g_load_library_ex_w.unsafe_call<HMODULE>(path, file, flags);
    if (result && is_game_assembly(path))
        hook_il2cpp_init();
    return result;
}

void hook_loader(SafetyHookInline& hook, HMODULE module, const char* name, void* detour) {
    void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) {
        LOG_WARN("kernel32!{} not found", name);
        return;
    }

    hook = safetyhook::create_inline(target, detour);
    // Steam's overlay hooks these too, so a failure here is plausible and must
    // not be silent: the polling fallback below is what saves the session.
    if (!hook)
        LOG_WARN("Could not hook kernel32!{}; relying on polling instead", name);
}

// If the runtime was already initialised before we got here, il2cpp_init will
// never be called again. That only happens when the plugin is injected late,
// and by definition the runtime is finished, so attaching our own thread is
// safe in that specific case.
void setup_late_injection() {
    LOG_WARN("il2cpp was already running when the plugin loaded; setting up from this thread.");
    if (!il2cpp::bind())
        return;
    il2cpp::api().thread_attach(il2cpp::api().domain_get());
    setup();
}

} // namespace

void install() {
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) {
        LOG_ERROR("kernel32.dll handle unavailable; cannot bootstrap.");
        return;
    }

    hook_loader(g_load_library_a, kernel32, "LoadLibraryA",
                reinterpret_cast<void*>(&load_library_a_hook));
    hook_loader(g_load_library_w, kernel32, "LoadLibraryW",
                reinterpret_cast<void*>(&load_library_w_hook));
    hook_loader(g_load_library_ex_a, kernel32, "LoadLibraryExA",
                reinterpret_cast<void*>(&load_library_ex_a_hook));
    hook_loader(g_load_library_ex_w, kernel32, "LoadLibraryExW",
                reinterpret_cast<void*>(&load_library_ex_w_hook));

    LOG_INFO("Waiting for GameAssembly.dll.");

    // The LoadLibrary detours are the fast path, but they are not guaranteed:
    // Steam's overlay hooks the same functions, and a lost race there once left
    // a whole session with no hooks at all. Polling for the module as well
    // means initialisation no longer depends on winning that race.
    //
    // What this loop must never do is call into il2cpp itself. A non-null
    // domain appears part-way through il2cpp_init, while the garbage collector
    // is still coming up, so treating it as "ready" and attaching this thread
    // is what produces "Fatal error in GC: Collecting from unknown thread".
    bool init_hooked = false;
    for (int waited = 0; waited < kSetupTimeoutMs; waited += kPollIntervalMs) {
        if (g_setup_done)
            return;
        if (!init_hooked)
            init_hooked = hook_il2cpp_init();
        Sleep(kPollIntervalMs);
    }

    if (g_setup_done)
        return;

    // Only after the full timeout, with il2cpp_init long since finished, is
    // setting up from this thread safe. Reaching here means the hook never
    // fired at all.
    if (il2cpp::bind() && il2cpp::runtime_ready()) {
        setup_late_injection();
        return;
    }

    if (!g_setup_done)
        LOG_ERROR("GameAssembly.dll never appeared; nothing was hooked.");
}

} // namespace bootstrap
