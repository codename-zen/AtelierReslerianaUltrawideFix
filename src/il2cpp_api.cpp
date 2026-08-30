#include "il2cpp_api.hpp"

#include <windows.h>
#include <string.h>

#include "log.hpp"

namespace il2cpp {
namespace {

Api g_api{};
bool g_bound = false;

template <typename T>
bool bind_one(HMODULE module, const char* name, T& target) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!target)
        LOG_ERROR("GameAssembly.dll is missing export '{}'", name);
    return target != nullptr;
}

} // namespace

const Api& api() { return g_api; }

bool bind() {
    if (g_bound)
        return true;

    HMODULE module = GetModuleHandleA("GameAssembly.dll");
    if (!module)
        return false;

    bool ok = true;
    ok &= bind_one(module, "il2cpp_domain_get", g_api.domain_get);
    ok &= bind_one(module, "il2cpp_domain_get_assemblies", g_api.domain_get_assemblies);
    ok &= bind_one(module, "il2cpp_assembly_get_image", g_api.assembly_get_image);
    ok &= bind_one(module, "il2cpp_image_get_name", g_api.image_get_name);
    ok &= bind_one(module, "il2cpp_class_from_name", g_api.class_from_name);
    ok &= bind_one(module, "il2cpp_class_get_method_from_name", g_api.class_get_method_from_name);
    ok &= bind_one(module, "il2cpp_class_get_field_from_name", g_api.class_get_field_from_name);
    ok &= bind_one(module, "il2cpp_field_get_offset", g_api.field_get_offset);
    ok &= bind_one(module, "il2cpp_thread_attach", g_api.thread_attach);
    ok &= bind_one(module, "il2cpp_class_get_methods", g_api.class_get_methods);
    ok &= bind_one(module, "il2cpp_method_get_name", g_api.method_get_name);
    ok &= bind_one(module, "il2cpp_method_get_param_count", g_api.method_get_param_count);
    ok &= bind_one(module, "il2cpp_method_get_param", g_api.method_get_param);
    ok &= bind_one(module, "il2cpp_type_get_name", g_api.type_get_name);
    ok &= bind_one(module, "il2cpp_free", g_api.mem_free);
    ok &= bind_one(module, "il2cpp_string_chars", g_api.string_chars);
    ok &= bind_one(module, "il2cpp_string_length", g_api.string_length);
    ok &= bind_one(module, "il2cpp_class_get_type", g_api.class_get_type);
    ok &= bind_one(module, "il2cpp_type_get_object", g_api.type_get_object);
    ok &= bind_one(module, "il2cpp_array_length", g_api.array_length);
    ok &= bind_one(module, "il2cpp_object_get_class", g_api.object_get_class);
    ok &= bind_one(module, "il2cpp_class_get_name", g_api.class_get_name);

    g_bound = ok;
    return ok;
}

bool runtime_ready() {
    return g_bound && g_api.domain_get() != nullptr;
}

Il2CppImage* find_image(const char* image_name) {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain)
        return nullptr;

    size_t count = 0;
    Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &count);
    if (!assemblies)
        return nullptr;

    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* image = g_api.assembly_get_image(assemblies[i]);
        const char* name = image ? g_api.image_get_name(image) : nullptr;
        if (name && _stricmp(name, image_name) == 0)
            return image;
    }
    return nullptr;
}

Il2CppClass* find_class(const char* image_name, const char* name_space, const char* name) {
    Il2CppImage* image = find_image(image_name);
    if (!image) {
        LOG_ERROR("Image '{}' not found", image_name);
        return nullptr;
    }

    Il2CppClass* klass = g_api.class_from_name(image, name_space, name);
    if (!klass)
        LOG_ERROR("Class '{}.{}' not found in {}", name_space, name, image_name);
    return klass;
}

const MethodInfo* find_method(Il2CppClass* klass, const char* name, int arg_count) {
    if (!klass)
        return nullptr;

    const MethodInfo* method = g_api.class_get_method_from_name(klass, name, arg_count);
    if (!method)
        LOG_ERROR("Method '{}' ({} args) not found", name, arg_count);
    return method;
}

const MethodInfo* find_overload(Il2CppClass* klass, const char* name, int arg_count,
                                unsigned int param_index, const char* param_type_name) {
    if (!klass)
        return nullptr;

    // Kept so a build whose type introspection misbehaves still gets a usable
    // method rather than nothing at all.
    const MethodInfo* first_by_arity = nullptr;

    void* iter = nullptr;
    while (const MethodInfo* method = g_api.class_get_methods(klass, &iter)) {
        const char* method_name = g_api.method_get_name(method);
        if (!method_name || strcmp(method_name, name) != 0)
            continue;
        if (g_api.method_get_param_count(method) != static_cast<unsigned int>(arg_count))
            continue;

        if (!first_by_arity)
            first_by_arity = method;

        const void* param = g_api.method_get_param(method, param_index);
        char* type_name = param ? g_api.type_get_name(param) : nullptr;

        LOG_INFO("Overload candidate: {}({} args), param {} is {}", name, arg_count, param_index,
                 type_name ? type_name : "<unknown>");

        // Substring, so it matches whether or not the runtime qualifies the
        // type name with its namespace.
        const bool matched = type_name && strstr(type_name, param_type_name) != nullptr;
        if (type_name)
            g_api.mem_free(type_name);
        if (matched)
            return method;
    }

    if (first_by_arity) {
        LOG_WARN("No {} overload matched parameter type {}; falling back to the first "
                 "{}-argument overload.",
                 name, param_type_name, arg_count);
    } else {
        LOG_ERROR("Overload {} ({} args) not found at all", name, arg_count);
    }
    return first_by_arity;
}

std::string to_narrow(const Il2CppObject* managed_string) {
    if (!managed_string)
        return {};

    const uint16_t* chars = g_api.string_chars(managed_string);
    const int length = g_api.string_length(managed_string);
    if (!chars || length <= 0)
        return {};

    std::string result;
    result.reserve(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        result.push_back(chars[i] < 128 ? static_cast<char>(chars[i]) : '?');
    return result;
}

bool param_type_contains(const MethodInfo* method, unsigned int param_index,
                         const char* substring) {
    if (!method)
        return false;

    const void* param = g_api.method_get_param(method, param_index);
    char* type_name = param ? g_api.type_get_name(param) : nullptr;
    if (!type_name)
        return false;

    const bool contains = strstr(type_name, substring) != nullptr;
    g_api.mem_free(type_name);
    return contains;
}

void* method_pointer(const MethodInfo* method) {
    if (!method)
        return nullptr;
    return *reinterpret_cast<void* const*>(method);
}

size_t field_offset(Il2CppClass* klass, const char* field_name) {
    if (!klass)
        return 0;

    FieldInfo* field = g_api.class_get_field_from_name(klass, field_name);
    if (!field) {
        LOG_ERROR("Field '{}' not found", field_name);
        return 0;
    }
    return g_api.field_get_offset(field);
}

} // namespace il2cpp
