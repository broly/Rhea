export module paths;

import <filesystem>;
import <iostream>;

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
            

            std::filesystem::path exePath = std::filesystem::current_path();
    

            std::filesystem::path projectPath = exePath;
    

            for (int i = 0; i < 1; ++i) 
            {
                if (projectPath.has_parent_path()) 
                {
                    projectPath = projectPath.parent_path();
            
                    std::filesystem::path jsonPath = projectPath / "project_marker_file";
    
                    if (std::filesystem::exists(jsonPath)) {
                        std::cout << "Project path is: " << projectPath << std::endl;
                        return projectPath;
                    }
                }
            }
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

}
