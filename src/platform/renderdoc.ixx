export module platform:renderdoc;

import std.compat;

// RenderDoc in-application API (renderdoc_app.h): programmatic frame captures.
// load() must run before the Vulkan instance is created (the capture layer hooks instance creation).
export namespace platform::renderdoc
{
    // Uses renderdoc.dll when the process was launched from RenderDoc, otherwise loads it from the install dir.
    // On failure `error` says why.
    bool load(std::string* error = nullptr);
    bool available();

    // Captures the next `frames` presented frames, one .rdc file each (under the path template)
    void trigger_capture(uint32_t frames);
    void set_capture_path_template(const std::string& path_template);
    uint32_t get_num_captures();
    // true while the current frame is being captured
    bool is_frame_capturing();
}
