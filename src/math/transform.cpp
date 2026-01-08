module rhmath:transform;
glm::mat4 Transform::matrix() const
{
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position.glm());
    glm::mat4 R = glm::mat4_cast(rotation.glm());
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale.glm());
    // glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale.x, scale.y, -scale.z));
        
    return T * R * S;
}
