#ifndef RESOURCES_REFLECTION
#define RESOURCES_REFLECTION

layout(set = SET_IBL, binding = BINDING_SAMPLER_IRRADIANCE) 
uniform samplerCube u_irradiance;

layout(set = SET_IBL, binding = BINDING_SAMPLER_PREFILTERED_ENV) 
uniform samplerCube u_prefilter_map;

layout(set = SET_IBL, binding = BINDING_SAMPLER_BRDF_LUT) 
uniform sampler2D u_brdf_lut;

#endif // RESOURCES_REFLECTION