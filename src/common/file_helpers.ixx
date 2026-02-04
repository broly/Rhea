export module file_helpers;

import <fstream>;
import <filesystem>;
import <chrono>;

export namespace file_helpers
{
    std::string load_text_from_file(std::filesystem::path file_path)
    {
        std::ifstream file(file_path);
        
        std::string str;
        
        std::string result;
        
        while (std::getline(file, str))
        {
            result += str;
            result.push_back('\n');
        }  
        
        return result;
    }
    
    
    std::vector<std::string> load_lines_from_file(std::filesystem::path file_path)
    {
        std::ifstream file(file_path);
        
        std::string str;
        
        std::vector<std::string> result;
        
        while (std::getline(file, str))
        {
            result.push_back(str);
        }  
        
        return result;
    }
    
    bool save_lines_to_file(std::filesystem::path file_path, std::vector<std::string> lines)
    {
        std::ofstream file(file_path);
        
        if (!file.is_open())
            return false;
        
        for (auto line : lines)
        {
            file << line << "\n";
        }
        return true;
    }
    
    bool save_text_to_file(std::filesystem::path file_path, std::string text)
    {
        std::ofstream file(file_path);

        if (!file.is_open())
            return false;

        file << text;

        return true;
    }
    
    
    void create_dir_if_not_exists(std::filesystem::path dir)
    {
        
        if (!std::filesystem::exists(dir))
        {
            std::filesystem::create_directory(dir);
        }
    }
    
    std::string remove_extension(const std::string& filename)
    {
        size_t last_dot = filename.find_last_of('.');
        if (last_dot == std::string::npos)
            return filename;

        return filename.substr(0, last_dot);
    }
    
    std::string file_time_to_string(std::filesystem::file_time_type ft)
    {    
        auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(ft);
        return std::format("{:%Y-%m-%d %H:%M:%S}", sys_time);
    }
    
}