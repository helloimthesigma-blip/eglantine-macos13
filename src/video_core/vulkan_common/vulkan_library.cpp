// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#ifdef __APPLE__
#include <climits>
#include <filesystem>
#include <vector>
#include <mach-o/dyld.h>
#endif

#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/vulkan_common/vulkan_library.h"

namespace Vulkan {

std::shared_ptr<Common::DynamicLibrary> OpenLibrary(
    [[maybe_unused]] Core::Frontend::GraphicsContext* context) {
    LOG_DEBUG(Render_Vulkan, "Looking for a Vulkan library");
#if defined(__ANDROID__) && defined(ARCHITECTURE_arm64)
    // Android manages its Vulkan driver from the frontend.
    return context->GetDriverLibrary();
#else
    auto library = std::make_shared<Common::DynamicLibrary>();
#ifdef __APPLE__
    std::vector<std::filesystem::path> candidates;
    if (const char* env_path = std::getenv("LIBVULKAN_PATH")) {
        candidates.emplace_back(env_path);
    }
    const auto bundle = Common::FS::GetBundleDirectory();
    candidates.push_back(bundle / "Contents/Frameworks/libvulkan.1.dylib");
    candidates.push_back(bundle / "Contents/Frameworks/libMoltenVK.dylib");
    // eden-cli is not a bundle: look beside it, in an app bundle it sits in, and in the
    // eglantine.app next to it.
    char exe_path[PATH_MAX];
    u32 exe_path_size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &exe_path_size) == 0) {
        std::error_code ec;
        auto exe_dir = std::filesystem::weakly_canonical(exe_path, ec).parent_path();
        candidates.push_back(exe_dir / "libMoltenVK.dylib");
        candidates.push_back(exe_dir / "../Frameworks/libMoltenVK.dylib");
        candidates.push_back(exe_dir / "eglantine.app/Contents/Frameworks/libMoltenVK.dylib");
    }
    for (const auto& path : candidates) {
        if (library->Open(path.c_str())) {
            LOG_INFO(Render_Vulkan, "Vulkan library: {}", path.string());
            break;
        }
    }
#else
    std::string filename = Common::DynamicLibrary::GetVersionedFilename("vulkan", 1);
    LOG_DEBUG(Render_Vulkan, "Trying Vulkan library: {}", filename);
    if (!library->Open(filename.c_str())) {
        // Android devices may not have libvulkan.so.1, only libvulkan.so.
        filename = Common::DynamicLibrary::GetVersionedFilename("vulkan");
        LOG_DEBUG(Render_Vulkan, "Trying Vulkan library (second attempt): {}", filename);
        void(library->Open(filename.c_str()));
    }
#endif
    return library;
#endif
}

} // namespace Vulkan
