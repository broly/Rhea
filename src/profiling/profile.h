#pragma once

import profile;

#define DO_PROFILE 1


#define PROFILE_COMBINE(a,b) a##b

#define PROFILE_VAR_NAME(a,b) PROFILE_COMBINE(a,b)

#ifdef DO_PROFILE

#define PROFILE(...) \
    auto PROFILE_VAR_NAME(__PROF_,__LINE__) = Profile(__VA_ARGS__)

#else 
#define PROFILE(...)\
    do {} while (false)
#endif