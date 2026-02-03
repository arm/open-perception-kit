#pragma once

#include "amp/Result.h"

#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

namespace amp {

typedef void *DynamicLibraryHandle;

struct Tools {

    static std::string getLocalIp();

    // ---

    static void abort();

    // ---

    static Result<DynamicLibraryHandle> DynamicLibraryOpen(const std::string &name);
    static void DynamicLibraryClose(void *handle);
    static Result<void *> DynamicLibraryGetSymbolRaw(DynamicLibraryHandle handle,
                                                     const std::string &symbolName);

    template <typename FuncPtr>
    static Result<FuncPtr> DynamicLibraryGetSymbol(DynamicLibraryHandle handle,
                                                   const std::string &symbolName) {
        static_assert(std::is_pointer_v<FuncPtr>, "FuncPtr must be a pointer type");
        static_assert(std::is_function_v<std::remove_pointer_t<FuncPtr>>,
                      "FuncPtr must be a function pointer type, e.g. int(*)(int)");

        auto raw = DynamicLibraryGetSymbolRaw(handle, symbolName);
        if (!raw) {
            return tl::make_unexpected(raw.error());
        }

        return reinterpret_cast<FuncPtr>(*raw);
    }
};

class Uuid {
  public:
    // Implicit conversion to uint64_t
    operator uint64_t() const noexcept {
        // fetch_add returns the previous value
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    static std::atomic<uint64_t> counter_;
};

class TsUtcNs {
  public:
    operator uint64_t() const noexcept {
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);

        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }
};

} // namespace amp

#define AMP_ABORT ::abort();
