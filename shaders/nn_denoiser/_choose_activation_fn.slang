import nn_lib;

#ifndef NN_ACT_NONE
    #define NN_ACT_NONE 0
#endif
#ifndef NN_ACT_GELU
    #define NN_ACT_GELU 1
#endif
#ifndef NN_ACT_RELU
    #define NN_ACT_RELU 2
#endif


#ifndef ACT
    #define ACT NN_ACT_GELU
    #warning "Activation function not chosen. Falling back to NN_ACT_GELU"
#endif


#if ACT == NN_ACT_NONE
    typedef NNActFn::None ActivationFn;
#elif ACT == NN_ACT_RELU
    typedef NNActFn::ReLU ActivationFn;
#elif ACT == NN_ACT_GELU
    typedef NNActFn::GeLU ActivationFn;
#else
    #error "Unknown activation function
#endif