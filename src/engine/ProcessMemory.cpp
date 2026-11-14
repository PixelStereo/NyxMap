#include "ProcessMemory.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <map>
#include <string>
#include <vector>
#include <psapi.h> // GetProcessMemoryInfo is K32GetProcessMemoryInfo in kernel32 (PSAPI_VERSION 2)
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFNumber.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <string>
#endif

namespace processMemory {

uint64_t residentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c)) return uint64_t(c.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return uint64_t(info.resident_size);
    return 0;
#else
    // Linux: /proc/self/statm, second field = resident pages
    FILE *f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long long size = 0, resident = 0;
    const int n = std::fscanf(f, "%llu %llu", &size, &resident);
    std::fclose(f);
    const long page = sysconf(_SC_PAGESIZE);
    return n == 2 && page > 0 ? uint64_t(resident) * uint64_t(page) : 0;
#endif
}

#if defined(_WIN32)
namespace {
class GpuEngineCounters {
public:
    GpuEngineCounters() {
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS &&
            PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS)
            PdhCollectQueryData(query);
    }
    ~GpuEngineCounters() { if (query) PdhCloseQuery(query); }

    double sample() {
        if (!counter || PdhCollectQueryData(query) != ERROR_SUCCESS) return -1;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA) return -1;
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> buffer((bytes + sizeof(buffer[0]) - 1) / sizeof(buffer[0]));
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, buffer.data()) != ERROR_SUCCESS) return -1;
        std::map<std::wstring, double> engines;
        for (DWORD i = 0; i < count; ++i) {
            const auto &item = buffer[i];
            if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
            const std::wstring instance(item.szName);
            const auto luid = instance.find(L"luid_");
            const auto type = instance.find(L"_engtype_", luid);
            if (luid != std::wstring::npos)
                engines[instance.substr(luid, type == std::wstring::npos ? type : type - luid)] += item.FmtValue.doubleValue;
        }
        double busiest = -1;
        for (const auto &entry : engines) busiest = std::max(busiest, entry.second);
        return busiest;
    }
private:
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
};
} // namespace
#endif

SystemUsage SystemUsageSampler::sample()
{
    SystemUsage usage;
    uint64_t total = 0, idle = 0;
#if defined(_WIN32)
    FILETIME idleTime{}, kernel{}, user{};
    if (GetSystemTimes(&idleTime, &kernel, &user)) {
        const auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        total = ticks(kernel) + ticks(user);
        idle = ticks(idleTime);
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    if (GlobalMemoryStatusEx(&memory)) usage.ram = memory.dwMemoryLoad;
    thread_local GpuEngineCounters gpu;
    usage.gpu = gpu.sample();
#elif defined(__APPLE__)
    const mach_port_t host = mach_host_self();
    host_cpu_load_info_data_t cpu{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &count) == KERN_SUCCESS) {
        for (const auto ticks : cpu.cpu_ticks) total += ticks;
        idle = cpu.cpu_ticks[CPU_STATE_IDLE];
    }
    vm_statistics64_data_t vm{};
    count = HOST_VM_INFO64_COUNT;
    uint64_t physical = 0;
    size_t length = sizeof physical;
    vm_size_t pageSize = 0;
    if (sysctlbyname("hw.memsize", &physical, &length, nullptr, 0) == 0 && physical &&
        host_page_size(host, &pageSize) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
        const uint64_t pages = uint64_t(vm.active_count) + vm.inactive_count + vm.speculative_count +
                               vm.wire_count + vm.compressor_page_count;
        const uint64_t reclaimable = uint64_t(vm.external_page_count) + vm.purgeable_count;
        usage.ram = 100.0 * double(pages - std::min(pages, reclaimable)) * double(pageSize) / double(physical);
    }
    mach_port_deallocate(mach_task_self(), host);

    io_iterator_t accelerators = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &accelerators) == KERN_SUCCESS) {
        while (const io_object_t accelerator = IOIteratorNext(accelerators)) {
            const auto stats = IORegistryEntryCreateCFProperty(accelerator, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
            if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
                const auto dictionary = static_cast<CFDictionaryRef>(stats);
                const auto value = CFDictionaryGetValue(dictionary, CFSTR("Device Utilization %"));
                double percent = -1;
                if (value && CFGetTypeID(value) == CFNumberGetTypeID() &&
                    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &percent))
                    usage.gpu = std::max(usage.gpu, percent);
            }
            if (stats) CFRelease(stats);
            IOObjectRelease(accelerator);
        }
        IOObjectRelease(accelerators);
    }
#else
    if (FILE *f = std::fopen("/proc/stat", "r")) {
        unsigned long long user = 0, nice = 0, system = 0, wait = 0, idleTicks = 0, irq = 0, softirq = 0, steal = 0;
        if (std::fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &user, &nice, &system, &idleTicks, &wait, &irq, &softirq, &steal) >= 4) {
            idle = idleTicks + wait;
            total = user + nice + system + idle + irq + softirq + steal;
        }
        std::fclose(f);
    }
    if (FILE *f = std::fopen("/proc/meminfo", "r")) {
        char line[256];
        unsigned long long physical = 0, available = 0;
        bool hasAvailable = false;
        while (std::fgets(line, sizeof line, f)) {
            if (std::sscanf(line, "MemTotal: %llu", &physical) == 1) continue;
            if (std::sscanf(line, "MemAvailable: %llu", &available) == 1) hasAvailable = true;
        }
        if (physical && hasAvailable) usage.ram = 100.0 * double(physical - std::min(physical, available)) / double(physical);
        std::fclose(f);
    }
    if (DIR *drm = opendir("/sys/class/drm")) {
        const auto read = [](const std::string &path) -> double {
            FILE *f = std::fopen(path.c_str(), "r");
            if (!f) return -1;
            unsigned long long value = 0;
            const bool ok = std::fscanf(f, "%llu", &value) == 1;
            std::fclose(f);
            return ok ? double(value) : -1;
        };
        while (const auto *entry = readdir(drm)) {
            const char *name = entry->d_name;
            if (std::strncmp(name, "card", 4) != 0 || !name[4] || name[4 + std::strspn(name + 4, "0123456789")]) continue;
            const std::string device = std::string("/sys/class/drm/") + name + "/device/";
            usage.gpu = std::max(usage.gpu, read(device + "gpu_busy_percent"));
            const double used = read(device + "mem_info_vram_used");
            const double capacity = read(device + "mem_info_vram_total");
            if (capacity > 0 && used >= 0) usage.vram = std::max(usage.vram, 100.0 * used / capacity);
        }
        closedir(drm);
    }
#endif
    if (previousTotal && total > previousTotal && idle >= previousIdle)
        usage.cpu = 100.0 * (1.0 - double(std::min(idle - previousIdle, total - previousTotal)) / double(total - previousTotal));
    previousTotal = total;
    previousIdle = idle;
    return usage;
}

} // namespace processMemory
