#pragma once
#include <filesystem>
#include <string>

namespace paths
{
    namespace priv 
    {
        struct Paths
        {
            std::filesystem::path project;
        };
        
        std::filesystem::path find_project_path();

        Paths& get_paths();
        
    }
    
    void init();
    std::filesystem::path get_project_path();
}
