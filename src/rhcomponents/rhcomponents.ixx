export module rhcomponents;

// Render components of scene entities (MeshRenderer, SkinnedMesh, Camera, Light, ReflectionCapture,
// SkyAtmosphere, VolumetricClouds, VolumetricFog, MeshCollider, BoneAttachment) and their sync to the SceneView processors (render_sync).
export import :mesh;
export import :skinned_mesh;
export import :camera;
export import :light;
export import :reflection_capture;
export import :sky;
export import :render_sync;
export import :attachment;
// AudioSource: a sound where the entity is; the sets of the listener sync and of the sources
export import :audio;
export import :scene_view_proxy.camera;
export import :scene_view_proxy.light;
export import :scene_view_proxy.mesh;
export import :scene_view_proxy.reflection_capture;
export import :scene_view_proxy.sky;
