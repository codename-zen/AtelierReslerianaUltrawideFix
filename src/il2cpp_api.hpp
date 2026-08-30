#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Thin binding over the il2cpp C API that GameAssembly.dll exports.
//
// Why this exists: this game ships an ENCRYPTED global-metadata.dat, so the
// usual static tooling (Cpp2IL, and therefore BepInEx/MelonLoader interop
// generation) cannot read type or method names off disk. The il2cpp runtime
// decrypts them in memory during startup, so every name we need is available
// through these exports at runtime. We resolve everything by name, at runtime,
// and never parse the metadata file ourselves.

// All il2cpp handles stay opaque. The single exception is MethodInfo, whose
// first field has been the native method pointer in every il2cpp version.
using Il2CppDomain = void;
using Il2CppAssembly = void;
using Il2CppImage = void;
using Il2CppClass = void;
using Il2CppObject = void;
using MethodInfo = void;
using FieldInfo = void;

namespace il2cpp {

struct Api {
    Il2CppDomain* (*domain_get)();
    Il2CppAssembly** (*domain_get_assemblies)(Il2CppDomain*, size_t*);
    Il2CppImage* (*assembly_get_image)(const Il2CppAssembly*);
    const char* (*image_get_name)(Il2CppImage*);
    Il2CppClass* (*class_from_name)(Il2CppImage*, const char*, const char*);
    const MethodInfo* (*class_get_method_from_name)(Il2CppClass*, const char*, int);
    FieldInfo* (*class_get_field_from_name)(Il2CppClass*, const char*);
    size_t (*field_get_offset)(FieldInfo*);
    void* (*thread_attach)(Il2CppDomain*);
    const MethodInfo* (*class_get_methods)(Il2CppClass*, void**);
    const char* (*method_get_name)(const MethodInfo*);
    unsigned int (*method_get_param_count)(const MethodInfo*);
    const void* (*method_get_param)(const MethodInfo*, unsigned int);
    char* (*type_get_name)(const void*);
    void (*mem_free)(void*);
    const uint16_t* (*string_chars)(const Il2CppObject*);
    int (*string_length)(const Il2CppObject*);
    const void* (*class_get_type)(Il2CppClass*);
    Il2CppObject* (*type_get_object)(const void*);
    size_t (*array_length)(Il2CppObject*);
    Il2CppClass* (*object_get_class)(Il2CppObject*);
    const char* (*class_get_name)(Il2CppClass*);
};

// Resolves the exports from an already-loaded GameAssembly.dll.
bool bind();
// True once il2cpp_init has produced a domain (metadata decrypted and loaded).
bool runtime_ready();
const Api& api();

Il2CppImage* find_image(const char* image_name);
Il2CppClass* find_class(const char* image_name, const char* name_space, const char* name);
const MethodInfo* find_method(Il2CppClass* klass, const char* name, int arg_count);
// Disambiguates overloads that share a name and arity (Screen::SetResolution has
// both a bool and a FullScreenMode three-argument form) by matching one
// parameter's type name, e.g. "UnityEngine.FullScreenMode".
const MethodInfo* find_overload(Il2CppClass* klass, const char* name, int arg_count,
                                unsigned int param_index, const char* param_type_name);

// True when parameter `param_index` of `method` has a type name containing
// `substring`. Used to tell overloads apart after the fact.
bool param_type_contains(const MethodInfo* method, unsigned int param_index,
                         const char* substring);

// Narrows a managed string for logging. Non-ASCII becomes '?', which is fine
// for the GameObject names this is used on.
std::string to_narrow(const Il2CppObject* managed_string);

// MethodInfo::methodPointer -- the native entry point we inline-hook or call.
void* method_pointer(const MethodInfo* method);
// Byte offset of an instance field, for direct reads/writes on a managed object.
size_t field_offset(Il2CppClass* klass, const char* field_name);

} // namespace il2cpp
