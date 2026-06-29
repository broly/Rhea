module nn_denoiser_api;


import nn_denoiser_resource;
import nn_lib;
import channel_writer;

namespace denoiser
{

    public void present_writer<uint NUM_CHANNELS>(int2 pixel, inout ChannelWriter<NUM_CHANNELS> writer)
    {
        static const uint OUT_SLICES = (NUM_CHANNELS + 3) / 4;
        float[NUM_CHANNELS] result = writer.output();
        [ForceUnroll] 
        for (int s = 0; s < OUT_SLICES; ++s)
        {
            float4 v = float4(0, 0, 0, 0);
            [ForceUnroll]
            for (int index = 0; index < 4; index++)
            {
                if (s * 4 + index < NUM_CHANNELS) 
                    v[index] = result[s * 4 + index];
            }
            int act_index = NN::get_activation_output(s);
            NN::store_activation(act_index, pixel, v);
        }
    }
}