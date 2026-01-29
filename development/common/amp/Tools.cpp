#include "Tools.h"
#include "amp/Result.h"
#include "fmt/base.h"

#include "amp/String.h"
#include "fmt/core.h"
#include <dlfcn.h>

using namespace amp;

std::string Tools::getLocalIp() {
    // HTTP server must bind to 0.0.0.0 (all interfaces) to accept connections
    // from host machine through Docker port mapping
    return "0.0.0.0";
}

void Tools::abort() {
    fmt::print("Amp is aborting the pipeline..\n");
    ::abort();
}

Result<DynamicLibraryHandle> Tools::DynamicLibraryOpen(const std::string &name) {

    std::vector<std::string> names;
    names.push_back(name);
    if (amp::utf8::endsWith(name, ".so") == false) {
        names.push_back(name + ".so");
    }

    std::string loadErrors;
    void *handle = nullptr;

    for (const auto &n : names) {
        dlerror();

        handle = dlopen(n.c_str(), RTLD_NOW);

        const char *err = dlerror();
        if (!err)
            err = "unknown error";
        loadErrors += fmt::format("ERROR while loading library [{}]: {}\n", n, err);

        if (handle)
            break;
    }

    if (!handle) {
        fmt::print("{}", loadErrors);
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::SystemFailure, loadErrors));
    }

    return (DynamicLibraryHandle)handle;
}

void Tools::DynamicLibraryClose(DynamicLibraryHandle handle) {
    if (!handle) {
        return;
    }
    dlclose((void *)handle);
}

Result<void *> Tools::DynamicLibraryGetSymbolRaw(DynamicLibraryHandle handle,
                                                 const std::string &symbolName) {
    if (!handle) {
        return tl::make_unexpected(
            AMP_ERROR(amp::ErrorFlag::SystemFailure, "Null dynamic library handle"));
    }

    dlerror(); // clear old errors

    void *sym = dlsym((void *)handle, symbolName.c_str());

    if (const char *err = dlerror(); err != nullptr) {
        std::string errorInfo = fmt::format("Symbol [{}] lookup error: {}", symbolName, err);
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::SystemFailure, errorInfo));
    }

    return sym;
}