export module assets:exr;
import <vector>;
import <string>;
import texture_format;
import rhmath;

export namespace exr
{
    
    bool save(const std::vector<std::byte>& buffer, TextureFormat fmt, Dim2d dim, const std::string& path);
}