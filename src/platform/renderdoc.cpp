module;

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "renderdoc_app.h"

module platform;

import :renderdoc;

import std.compat;

namespace
{
    RENDERDOC_API_1_6_0* api = nullptr;
}

bool platform::renderdoc::load(std::string* error)
{
    if (api)
        return true;

    HMODULE module = GetModuleHandleA("renderdoc.dll");
    if (!module)
        module = LoadLibraryA("C:/Program Files/RenderDoc/renderdoc.dll");
    if (!module)
    {
        if (error)
            *error = "LoadLibrary failed, GetLastError " + std::to_string(GetLastError());
        return false;
    }

    const auto get_api = (pRENDERDOC_GetAPI)GetProcAddress(module, "RENDERDOC_GetAPI");
    if (!get_api)
    {
        if (error)
            *error = "RENDERDOC_GetAPI not exported";
        return false;
    }
    if (get_api(eRENDERDOC_API_Version_1_6_0, (void**)&api) != 1)
    {
        api = nullptr;
        if (error)
            *error = "RENDERDOC_GetAPI refused API version 1.6.0";
        return false;
    }
    return true;
}

bool platform::renderdoc::available()
{
    return api != nullptr;
}

void platform::renderdoc::trigger_capture(uint32_t frames)
{
    if (api)
        api->TriggerMultiFrameCapture(frames);
}

void platform::renderdoc::set_capture_path_template(const std::string& path_template)
{
    if (api)
        api->SetCaptureFilePathTemplate(path_template.c_str());
}

uint32_t platform::renderdoc::get_num_captures()
{
    return api ? api->GetNumCaptures() : 0;
}

bool platform::renderdoc::is_frame_capturing()
{
    return api && api->IsFrameCapturing() == 1;
}
