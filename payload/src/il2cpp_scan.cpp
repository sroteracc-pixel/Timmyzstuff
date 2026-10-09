// il2cpp_scan.cpp - see il2cpp_scan.h.
#include "il2cpp_scan.h"

#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>

namespace tzscan {

namespace {

const int kMaxDetailClasses = 80;     // classes written out in full
const int kMaxFieldsPerClass = 60;
const int kMaxMethodsPerClass = 90;
const int kMaxFieldHits = 260;        // single "this field looks interesting" lines
const int kMaxTypeHits = 120;         // "this class holds a Rigidbody / CharacterController" lines
const int kMaxLines = 2600;

std::string lowerOf(const char* s) {
    std::string r = s ? s : "";
    for (char& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}
bool hasAny(const std::string& hay, const char* const* words) {
    for (int i = 0; words[i]; ++i) if (hay.find(words[i]) != std::string::npos) return true;
    return false;
}

// Assemblies we do not need to look through (the engine and the .NET base library).
bool skipAssembly(const std::string& n) {
    static const char* prefixes[] = {"mscorlib", "System", "Mono.", "netstandard", "UnityEngine", "Unity.TextMeshPro", "Unity.Mathematics", "Unity.Burst",
                                     "Unity.Collections", "Unity.Jobs", "Unity.Timeline", "Newtonsoft", "I18N", "Unity.Serialization", "nunit", "ICSharpCode", nullptr};
    for (int i = 0; prefixes[i]; ++i) if (n.compare(0, std::strlen(prefixes[i]), prefixes[i]) == 0) return true;
    return false;
}

std::string typeName(const Api& api, void* type) {
    if (!type || !api.type_get_name) return "?";
    char* s = api.type_get_name(type);
    if (!s) return "?";
    std::string r = s;
    if (api.il2cpp_free) api.il2cpp_free(s);
    return r;
}

// thread names (so the next step knows which thread is the game's main thread)
void listThreads(LogFn log) {
    std::map<std::string, std::pair<int, int>> names;       // name -> (count, first tid)
    DIR* d = opendir("/proc/self/task");
    if (!d) { log("scan: could not list threads"); return; }
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[320]; std::snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
        FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char nm[64] = {0};
        if (std::fgets(nm, sizeof nm, f)) { size_t n = std::strlen(nm); while (n && (nm[n - 1] == '\n' || nm[n - 1] == '\r')) nm[--n] = 0; }
        std::fclose(f);
        auto& slot = names[nm];
        if (slot.first == 0) slot.second = std::atoi(e->d_name);
        ++slot.first;
    }
    closedir(d);
    log("scan: %d kinds of threads in the game process (name x count, first thread id):", static_cast<int>(names.size()));
    std::string line;
    for (const auto& kv : names) {
        char b[96]; std::snprintf(b, sizeof b, "%s x%d (%d)  ", kv.first.c_str(), kv.second.first, kv.second.second);
        line += b;
        if (line.size() > 150) { log("scan: threads: %s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) log("scan: threads: %s", line.c_str());
}

bool readPtr(uintptr_t from, uint64_t* out) {      // crash-proof read (the kernel says "no" for a bad address)
    static int pfd[2] = {-1, -1};
    if (pfd[0] < 0 && pipe(pfd) != 0) { pfd[0] = pfd[1] = -1; return false; }
    if (write(pfd[1], reinterpret_cast<const void*>(from), 8) != 8) return false;
    return read(pfd[0], out, 8) == 8;
}

}  // namespace

bool classNameMatches(const std::string& name) {
    static const char* words[] = {"move", "locomo", "jump", "gravit", "speed", "fly", "walk", "sprint", "climb", "charactercontroller", "velocity", "teleport", "boost", "stamina", nullptr};
    std::string l = lowerOf(name.c_str());
    // "Remove" contains "move" but is not movement ("RemoveItem"). Take every "remove" out first,
    // so "RemoveItem" does not match but "RemoveMovement" still does.
    for (size_t p = l.find("remove"); p != std::string::npos; p = l.find("remove")) l.erase(p, 6);
    return hasAny(l, words);
}

bool fieldNameMatches(const std::string& name) {
    static const char* words[] = {"speed", "jump", "gravit", "velocity", "sprint", "fly", "stamina", nullptr};
    return hasAny(lowerOf(name.c_str()), words);
}

bool loadApi(void* lib, Api* out, std::string* missing) {
    bool ok = true;
    auto need = [&](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(dlsym(lib, name));
        if (!slot) { ok = false; if (missing) { *missing += name; *missing += " "; } }
    };
    auto want = [&](auto& slot, const char* name) { slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(dlsym(lib, name)); };
    need(out->domain_get, "il2cpp_domain_get");
    need(out->domain_get_assemblies, "il2cpp_domain_get_assemblies");
    need(out->assembly_get_image, "il2cpp_assembly_get_image");
    need(out->image_get_name, "il2cpp_image_get_name");
    need(out->image_get_class_count, "il2cpp_image_get_class_count");
    need(out->image_get_class, "il2cpp_image_get_class");
    need(out->class_get_name, "il2cpp_class_get_name");
    need(out->class_get_namespace, "il2cpp_class_get_namespace");
    need(out->class_get_parent, "il2cpp_class_get_parent");
    need(out->class_get_fields, "il2cpp_class_get_fields");
    need(out->field_get_name, "il2cpp_field_get_name");
    need(out->field_get_type, "il2cpp_field_get_type");
    need(out->field_get_offset, "il2cpp_field_get_offset");
    need(out->class_get_methods, "il2cpp_class_get_methods");
    need(out->method_get_name, "il2cpp_method_get_name");
    need(out->method_get_param_count, "il2cpp_method_get_param_count");
    need(out->method_get_return_type, "il2cpp_method_get_return_type");
    need(out->type_get_name, "il2cpp_type_get_name");
    want(out->field_get_flags, "il2cpp_field_get_flags");
    want(out->method_get_flags, "il2cpp_method_get_flags");
    want(out->il2cpp_free, "il2cpp_free");
    want(out->thread_attach, "il2cpp_thread_attach");
    want(out->thread_detach, "il2cpp_thread_detach");
    return ok;
}

Summary run(const Api& api, uintptr_t libBase, LogFn log) {
    Summary sum;
    int lines = 0;
    auto say = [&](const char* fmt, auto... args) { if (lines < kMaxLines) { log(fmt, args...); ++lines; } };

    say("--- game code scan (read-only) ---");
    listThreads(log);
    void* domain = api.domain_get();
    if (!domain) { sum.error = "the game's runtime is not ready yet (no domain)"; log("scan: %s", sum.error.c_str()); return sum; }
    void* thread = api.thread_attach ? api.thread_attach(domain) : nullptr;
    size_t asmCount = 0;
    void** assemblies = api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies || asmCount == 0) { sum.error = "no assemblies reported"; log("scan: %s", sum.error.c_str()); if (thread && api.thread_detach) api.thread_detach(thread); return sum; }

    int detailClasses = 0, fieldHits = 0, typeHits = 0;
    for (size_t a = 0; a < asmCount; ++a) {
        void* image = api.assembly_get_image(assemblies[a]);
        if (!image) continue;
        const char* rawName = api.image_get_name(image);
        const std::string asmName = rawName ? rawName : "?";
        const size_t count = api.image_get_class_count(image);
        const bool skip = skipAssembly(asmName);
        const bool mainGame = asmName.compare(0, 15, "Assembly-CSharp") == 0;      // also matches Assembly-CSharp-firstpass
        ++sum.assemblies;
        say("scan: assembly %s classes=%d %s", asmName.c_str(), static_cast<int>(count), skip ? "(engine / system: skipped)" : (mainGame ? "(game code: fully scanned)" : "(other: names only)"));
        if (skip) continue;

        for (size_t i = 0; i < count; ++i) {
            void* klass = api.image_get_class(image, i);
            if (!klass) continue;
            const char* cn = api.class_get_name(klass);
            if (!cn) continue;
            const std::string className = cn;
            ++sum.classes;
            if (className.find('<') != std::string::npos || className.find('>') != std::string::npos) continue;    // compiler-made helper classes
            const char* ns = api.class_get_namespace(klass);
            const std::string full = (ns && *ns) ? std::string(ns) + "." + className : className;
            const bool tight = classNameMatches(className);

            if (tight && detailClasses < kMaxDetailClasses) {
                ++detailClasses; ++sum.matchedClasses;
                void* parent = api.class_get_parent(klass);
                const char* pn = parent ? api.class_get_name(parent) : nullptr;
                say("scan: CLASS %s : %s   [assembly %s]", full.c_str(), pn ? pn : "-", asmName.c_str());
                void* it = nullptr; int nf = 0;
                while (void* f = api.class_get_fields(klass, &it)) {
                    if (++nf > kMaxFieldsPerClass) { say("scan:   (more fields not shown)"); break; }
                    const char* fnm = api.field_get_name(f);
                    const int flags = api.field_get_flags ? api.field_get_flags(f) : 0;
                    say("scan:   field %s : %s @%d%s", fnm ? fnm : "?", typeName(api, api.field_get_type(f)).c_str(), static_cast<int>(api.field_get_offset(f)), (flags & 0x10) ? " static" : "");
                }
                it = nullptr; int nm = 0;
                while (void* m = api.class_get_methods(klass, &it)) {
                    if (++nm > kMaxMethodsPerClass) { say("scan:   (more methods not shown)"); break; }
                    const char* mn = api.method_get_name(m);
                    uint32_t iflags = 0;
                    const uint32_t mflags = api.method_get_flags ? api.method_get_flags(m, &iflags) : 0;
                    uint64_t ptr = 0; unsigned long rva = 0;
                    if (readPtr(reinterpret_cast<uintptr_t>(m), &ptr) && libBase && ptr > libBase && ptr - libBase < (1ULL << 31)) rva = static_cast<unsigned long>(ptr - libBase);
                    say("scan:   method %s(%u) : %s rva=%lx%s", mn ? mn : "?", api.method_get_param_count(m), typeName(api, api.method_get_return_type(m)).c_str(), rva, (mflags & 0x10) ? " static" : "");
                }
            } else if (mainGame) {
                void* it = nullptr; int nf = 0;
                while (void* f = api.class_get_fields(klass, &it)) {
                    if (++nf > 400) break;
                    const char* fnm = api.field_get_name(f);
                    const std::string fname = fnm ? fnm : "";
                    if (fieldHits < kMaxFieldHits && fieldNameMatches(fname)) {
                        ++fieldHits; ++sum.fieldHits;
                        say("scan: field-hit %s.%s : %s @%d", full.c_str(), fname.c_str(), typeName(api, api.field_get_type(f)).c_str(), static_cast<int>(api.field_get_offset(f)));
                    } else if (typeHits < kMaxTypeHits) {
                        const std::string tn = typeName(api, api.field_get_type(f));
                        if (tn.find("CharacterController") != std::string::npos || tn.find("Rigidbody") != std::string::npos) {
                            ++typeHits; ++sum.typeHits;
                            say("scan: type-hit %s.%s : %s @%d", full.c_str(), fname.c_str(), tn.c_str(), static_cast<int>(api.field_get_offset(f)));
                        }
                    }
                }
            }
        }
    }
    if (thread && api.thread_detach) api.thread_detach(thread);
    say("scan: DONE. assemblies=%d classes=%d detailed=%d field-hits=%d type-hits=%d%s", sum.assemblies, sum.classes, sum.matchedClasses, sum.fieldHits, sum.typeHits, lines >= kMaxLines ? " (output was cut at the line limit)" : "");
    sum.ok = true;
    return sum;
}

}  // namespace tzscan
