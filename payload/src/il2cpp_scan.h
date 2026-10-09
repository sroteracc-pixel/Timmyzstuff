// il2cpp_scan.h - a READ-ONLY look at the game's own code, written into the facts file.
//
// WHAT THIS IS (plain words)
//   The game is made with Unity "IL2CPP". The game's runtime library (libil2cpp.so) can tell us the names
//   of the game's classes, their fields (with exact sizes/positions) and their methods. This scan asks it
//   and writes down the movement-related ones: anything with Move / Jump / Speed / Gravity / Fly / Walk /
//   Sprint ... in its name, and any field that looks like a speed / jump / gravity value.
//   It changes NOTHING in the game. It only runs when you press "Scan game code" on the Movement page.
//
//   NOT confirmed on a real headset: this was only tested against a pretend runtime on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace tzscan {

typedef void (*LogFn)(const char* fmt, ...);

// The il2cpp functions we use (found by name in libil2cpp.so). The last few are optional.
struct Api {
    void* (*domain_get)() = nullptr;
    void** (*domain_get_assemblies)(void* domain, size_t* size) = nullptr;
    void* (*assembly_get_image)(void* assembly) = nullptr;
    const char* (*image_get_name)(void* image) = nullptr;
    size_t (*image_get_class_count)(void* image) = nullptr;
    void* (*image_get_class)(void* image, size_t index) = nullptr;
    const char* (*class_get_name)(void* klass) = nullptr;
    const char* (*class_get_namespace)(void* klass) = nullptr;
    void* (*class_get_parent)(void* klass) = nullptr;
    void* (*class_get_fields)(void* klass, void** iter) = nullptr;
    const char* (*field_get_name)(void* field) = nullptr;
    void* (*field_get_type)(void* field) = nullptr;
    size_t (*field_get_offset)(void* field) = nullptr;
    void* (*class_get_methods)(void* klass, void** iter) = nullptr;
    const char* (*method_get_name)(void* method) = nullptr;
    uint32_t (*method_get_param_count)(void* method) = nullptr;
    void* (*method_get_return_type)(void* method) = nullptr;
    char* (*type_get_name)(void* type) = nullptr;
    // optional
    int (*field_get_flags)(void* field) = nullptr;
    uint32_t (*method_get_flags)(void* method, uint32_t* iflags) = nullptr;
    void (*il2cpp_free)(void* p) = nullptr;
    void* (*thread_attach)(void* domain) = nullptr;
    void (*thread_detach)(void* thread) = nullptr;
};

// Looks the functions up in an already loaded libil2cpp.so. `missing` lists required ones that were not found.
bool loadApi(void* lib, Api* out, std::string* missing);

struct Summary { bool ok = false; int assemblies = 0, classes = 0, matchedClasses = 0, fieldHits = 0, typeHits = 0; std::string error; };

// Runs the scan. `libBase` = where libil2cpp.so is mapped (to turn method addresses into offsets; 0 = unknown).
Summary run(const Api& api, uintptr_t libBase, LogFn log);

// The words used to pick classes / fields (exposed for the test).
bool classNameMatches(const std::string& name);
bool fieldNameMatches(const std::string& name);

}  // namespace tzscan
