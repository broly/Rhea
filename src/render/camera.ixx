export module framework:camera;

import glm;

import rhmath;

export class Camera
{
public:
    bool active;
    
    Transform transform;
    
    float fov = glm::radians(60.0f);
    float near_plane = 0.1f;
    float far_plane = 100.f;
    
    void set_position(glm::vec3 in_position);
    void set_euler_rotation(glm::vec3 in_rotation);
    void set_rotation(glm::quat in_rotation);
    
    [[nodiscard]] 
    glm::mat4 view() const;
    
    [[nodiscard]] 
    glm::mat4 projection(float aspect) const;
    
};
