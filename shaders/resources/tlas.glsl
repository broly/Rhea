#ifndef RESOURCES_TLAS
#define RESOURCES_TLAS

#ifndef SET_TLAS
    #define SET_TLAS
    #error "SET_TLAS must be provided"
#endif
#ifndef BINDING_TLAS_TLAS
    #define BINDING_TLAS_TLAS
    #error "BINDING_TLAS_TLAS must be provided"
#endif

layout(set = SET_TLAS, binding = BINDING_TLAS_TLAS)
uniform accelerationStructureEXT u_tlas;

#endif  // RESOURCES_TLAS