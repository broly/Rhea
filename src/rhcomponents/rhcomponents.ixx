export module rhcomponents;

// Render components of scene entities (MeshRenderer, SkinnedMesh, Camera, Light, ReflectionCapture,
// MeshCollider) and their sync to the SceneView processors: install_render_components.
export import :mesh;
export import :skinned_mesh;
export import :camera;
export import :light;
export import :reflection_capture;
export import :render_sync;
export import :scene_view_proxy.camera;
export import :scene_view_proxy.light;
export import :scene_view_proxy.mesh;
export import :scene_view_proxy.reflection_capture;
