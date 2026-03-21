#ifndef RESOURCES_TLAS
#define RESOURCES_TLAS

#ifndef SET_TLAS
    #define SET_TLAS 0
    #error "SET_TLAS definition is missing. Provide this resource: tlas"
#endif
#ifndef BINDING_TLAS_TLAS
    #define BINDING_TLAS_TLAS 0
    #error "BINDING_TLAS_TLAS definition is missing. Provide this resource: tlas"
#endif

layout(set = SET_TLAS, binding = BINDING_TLAS_TLAS)
uniform accelerationStructureEXT u_tlas;

#endif  // RESOURCES_TLAS