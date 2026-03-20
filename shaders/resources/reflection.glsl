#ifndef RESOURCES_REFLECTION
#define RESOURCES_REFLECTION

#ifndef SET_IBL
    #define SET_IBL 0 
    #error "SET_IBL must be provided"
#endif
#ifndef BINDING_SAMPLER_IRRADIANCE
    #define BINDING_SAMPLER_IRRADIANCE 0 
    #error "BINDING_SAMPLER_IRRADIANCE must be provided"
#endif
#ifndef BINDING_SAMPLER_PREFILTERED_ENV
    #define BINDING_SAMPLER_PREFILTERED_ENV 0 
    #error "BINDING_SAMPLER_PREFILTERED_ENV must be provided"
#endif
#ifndef BINDING_SAMPLER_BRDF_LUT
    #define BINDING_SAMPLER_BRDF_LUT 0 
    #error "BINDING_SAMPLER_BRDF_LUT must be provided"
#endif

layout(set = SET_IBL, binding = BINDING_SAMPLER_IRRADIANCE) 
uniform samplerCube u_irradiance;

layout(set = SET_IBL, binding = BINDING_SAMPLER_PREFILTERED_ENV) 
uniform samplerCube u_prefilter_map;

layout(set = SET_IBL, binding = BINDING_SAMPLER_BRDF_LUT) 
uniform sampler2D u_brdf_lut;



vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
    pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


#endif // RESOURCES_REFLECTION