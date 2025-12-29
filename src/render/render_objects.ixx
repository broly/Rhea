export module render:render_objects;

import :handle_types;
import rhmath;
import assets;
import glm;
import <string>;


export struct MaterialKey
{
    glm::vec4 base_color;
    float metallic;
    float roughness;

    TextureHandle albedo;
    TextureHandle normal;
    TextureHandle occlusion;

    bool operator==(const MaterialKey& other) const
    {
        return
            base_color == other.base_color &&
            albedo     == other.albedo &&
            metallic   == other.metallic   &&
            roughness  == other.roughness  &&
            normal     == other.normal     &&
            occlusion  == other.occlusion;
    }
    
    static MaterialKey make_key(const PBRMaterial& mat)
    {
        MaterialKey key{};
        key.albedo     = mat.albedo;
        key.base_color = mat.base_color;
        key.metallic   = mat.metallic;
        key.roughness  = mat.roughness;
        key.normal     = mat.normal;
        key.occlusion  = mat.occlusion;
        return key;
    }

};

export struct MaterialKeyHash
{
    size_t operator()(const MaterialKey& k) const
    {
        size_t h = 0;

        hash_combine(h, k.base_color.x);
        hash_combine(h, k.base_color.y);
        hash_combine(h, k.base_color.z);
        hash_combine(h, k.base_color.w);

        hash_combine(h, k.metallic);
        hash_combine(h, k.roughness);

        hash_combine(h, k.albedo.id);
        hash_combine(h, k.normal.id);
        hash_combine(h, k.occlusion.id);

        return h;
    }

private:
    template<typename T>
    static void hash_combine(size_t& seed, const T& v)
    {
        seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};



export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
    MaterialKey material;
    std::string debug_name;
};

export struct RenderMaterial
{
    RBDescriptorSet descriptor;      // Persistent
    RBDescriptorSetLayout layout;

    RBBufferHandle material_ubo;     // optional
    RBImageHandle albedo;
    RBImageHandle normal;
    RBImageHandle occlusion;
    
    MaterialKey key;
};

