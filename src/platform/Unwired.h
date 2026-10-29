// SC4_UNWIRED("Interface::Method"): marks a place where the running game reaches a seam, creator or service that nobody
// answers. Each site logs once to stderr the first time it is hit and keeps a hit counter; the totals are written to
// work/unwired.log (or $SC4_UNWIRED_LOG) when the process exits. scripts/unwired_report.py ranks that file.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace platform {
struct UnwiredSite {
    const char* name;
    const char* file;
    int line;
    std::atomic<unsigned long> hits{0};
    UnwiredSite* next = nullptr;
};
namespace detail {
struct UnwiredRegistry {
    std::mutex lock;
    UnwiredSite* sites = nullptr;
    bool atExit = false;
};
inline UnwiredRegistry& Registry() { static UnwiredRegistry r; return r; }
inline void WriteUnwiredLog() {
    const char* path = std::getenv("SC4_UNWIRED_LOG");
    std::FILE* f = std::fopen(path ? path : "work/unwired.log", "w");
    if (!f) return;
    for (UnwiredSite* s = Registry().sites; s; s = s->next)
        std::fprintf(f, "%lu\t%s\t%s:%d\n", s->hits.load(), s->name, s->file, s->line);
    std::fclose(f);
}
}  // namespace detail

// Registers the site on its first hit (prints once to stderr) and counts every hit. Header-only so any target can use it.
inline void ReportUnwired(UnwiredSite& site) {
    if (site.hits.fetch_add(1) != 0) return;
    auto& reg = detail::Registry();
    std::lock_guard<std::mutex> guard(reg.lock);
    site.next = reg.sites;
    reg.sites = &site;
    if (!reg.atExit) { reg.atExit = true; std::atexit(detail::WriteUnwiredLog); }
    std::fprintf(stderr, "UNWIRED: %s (%s:%d)\n", site.name, site.file, site.line);
}
}  // namespace platform

#define SC4_UNWIRED(what)                                                                                    \
    do {                                                                                                     \
        static ::platform::UnwiredSite sc4UnwiredSite{what, __FILE__, __LINE__};                             \
        ::platform::ReportUnwired(sc4UnwiredSite);                                                           \
    } while (0)
