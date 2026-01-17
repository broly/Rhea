export module string_helpers;
import <utility>;
import <string>;
import <stdexcept>;
import <algorithm>;

export namespace string_helpers
{
    std::pair<std::string, std::string> split_by_dot(const std::string& str)
    {
        size_t dot_pos = str.find('.');

        if (dot_pos == std::string::npos)
            throw std::runtime_error("No dot found");

        if (str.find('.', dot_pos + 1) != std::string::npos)
            throw std::runtime_error("More than one dot found");

        if (dot_pos == 0 || dot_pos == str.size() - 1)
            throw std::runtime_error("Dot at invalid position");

        return {
            str.substr(0, dot_pos),
            str.substr(dot_pos + 1)
        };
    }
    
    
    std::string trim_and_remove_newlines(const std::string& input)
    {
        std::string result;
        result.reserve(input.size());

        for (char c : input)
        {
            if (c != '\n')
                result.push_back(c);
        }

        auto left = std::find_if_not(result.begin(), result.end(),
            [](unsigned char ch) { return std::isspace(ch); });

        auto right = std::find_if_not(result.rbegin(), result.rend(),
            [](unsigned char ch) { return std::isspace(ch); }).base();

        if (left >= right)
            return "";

        return std::string(left, right);
    }
}
