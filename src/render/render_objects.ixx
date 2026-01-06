export module render:render_objects;

import :handle_types;
import :render_resource_instance;
import rhmath;
import assets;
import glm;
import <string>;

struct RenderProxy
{
    
};


export struct MaterialKey
{
    TextureHandle base_color;
    TextureHandle emissive;
    TextureHandle normal;
    TextureHandle occlusion_roughness_metallic;
    
    float base_color_mult;
    float emissive_mult;
    float occlusion_mult;
    float roughness_mult;
    float metallic_mult;

    bool operator==(const MaterialKey& other) const
    {
        return
            base_color                      == other.base_color &&
            emissive                        == other.emissive &&
            normal                          == other.normal     &&
            occlusion_roughness_metallic    == other.occlusion_roughness_metallic && 
            base_color_mult                 == other.base_color_mult &&
            emissive_mult                   == other.emissive_mult &&
            occlusion_mult                  == other.occlusion_mult     &&
            roughness_mult                  == other.roughness_mult     &&
            metallic_mult                   == other.metallic_mult;
    }
    
    static MaterialKey make_key(const PBRMaterial& mat)
    {
        MaterialKey key{};
        key.base_color = mat.base_color;
        key.emissive     = mat.emissive;
        key.normal     = mat.normal;
        key.occlusion_roughness_metallic  = mat.occlusion_roughness_metallic;
        key.base_color_mult = mat.base_color_mult;
        key.emissive_mult     = mat.emissive_mult;
        key.occlusion_mult     = mat.occlusion_mult;
        key.roughness_mult  = mat.roughness_mult;
        key.metallic_mult  = mat.metallic_mult;
        return key;
    }

};

export struct MaterialKeyHash
{
    size_t operator()(const MaterialKey& k) const
    {
        size_t h = 0;

        hash_combine(h, k.base_color.id);
        hash_combine(h, k.emissive.id);
        hash_combine(h, k.normal.id);
        hash_combine(h, k.occlusion_roughness_metallic.id);

        return h;
    }

private:
    template<typename T>
    static void hash_combine(size_t& seed, const T& v)
    {
        seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};


export struct RenderMaterial
{
    RenderResourceInstance* resource = nullptr;
    
    MaterialKey key;
};


export struct RenderObject_PointLight
{
    glm::vec3 position;
    glm::vec4 color;
    float intensity;
    float attenuation;
};