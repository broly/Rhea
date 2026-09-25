export module paths;

import std.compat;


export namespace paths
{
    namespace priv 
    {
        struct Paths
        {
            std::filesystem::path project;
        };
        
        std::filesystem::path find_project_path()
        {
            // The project root is the nearest ancestor of the working directory
            // (the build dir, e.g. out/build/debug, or the project itself) that
            // contains project_marker_file.
            const std::filesystem::path start = std::filesystem::current_path();

            for (std::filesystem::path dir = start; ; dir = dir.parent_path())
            {
                if (std::filesystem::exists(dir / "project_marker_file"))
                {
                    std::cout << "Project path is: " << dir << std::endl;
                    return dir;
                }
                if (!dir.has_parent_path() || dir.parent_path() == dir)
                    break;
            }

            std::cerr << "project_marker_file not found in " << start << " or any parent directory" << std::endl;
            std::abort();
        }

        Paths& get_paths()
        {
            static Paths paths {
                find_project_path(),
            };
            return paths;
        }
        
    }
    
    void init()
    {
        (void)priv::get_paths();
    }
    std::filesystem::path get_project_path()
    {
        return priv::get_paths().project;
    }
    std::filesystem::path get_assets_path()
    {
        return get_project_path() / "assets";
    }
    std::filesystem::path get_cache_path()
    {
        return get_project_path() / "cache";
    }

}
