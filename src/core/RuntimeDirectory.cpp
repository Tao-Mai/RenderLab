#include "core/RuntimeDirectory.h"

#include "core/Logger.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

const std::filesystem::path& runtimeDirectory()
{
    static const auto kDirectory = []
    {
#ifdef _WIN32
        HMODULE module = nullptr;
        const bool found = GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&runtimeDirectory), &module
        );
        CHECK(found, "cannot locate RenderLabCore: {}", GetLastError());
        std::wstring path(512, L'\0');
        for (;;)
        {
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            CHECK(length, "cannot resolve the RenderLabCore path: {}", GetLastError());
            if (length < path.size())
            {
                path.resize(length);
                return std::filesystem::path{path}.parent_path();
            }
            path.resize(path.size() * 2);
        }
#else
        Dl_info info{};
        const auto found = dladdr(reinterpret_cast<const void*>(&runtimeDirectory), &info);
        CHECK(found && info.dli_fname, "cannot locate RenderLabCore");
        std::error_code error;
        const auto path = std::filesystem::absolute(info.dli_fname, error);
        CHECK(!error, "cannot resolve RenderLabCore path '{}': {}", info.dli_fname, error.message());
        return path.parent_path();
#endif
    }();
    return kDirectory;
}
