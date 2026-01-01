module framework:camera;

void Camera::set_position(glm::vec3 in_position) {
    transform.set_position(in_position);
}

void Camera::set_euler_rotation(glm::vec3 in_rotation) {
    auto quat = glm::quat(glm::vec3(in_rotation.x, in_rotation.y, in_rotation.z));
    transform.set_rotation(quat);
}

void Camera::set_rotation(glm::quat in_rotation) {
    transform.set_rotation(in_rotation);
}

glm::mat4 Camera::view() const {
    return transform.get_view();
}

glm::mat4 Camera::projection(float aspect) const {
    glm::mat4 p = glm::perspective(fov, aspect, near_plane, far_plane);
    p[1][1] *= -1; // Vulkan NDC
    return p;
}