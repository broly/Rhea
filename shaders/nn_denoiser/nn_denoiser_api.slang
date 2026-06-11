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
            if (s * 4 + 0 < NUM_CHANNELS) 
                v.x = result[s * 4 + 0];
            if (s * 4 + 1 < NUM_CHANNELS) 
                v.y = result[s * 4 + 1];
            if (s * 4 + 2 < NUM_CHANNELS) 
                v.z = result[s * 4 + 2];
            if (s * 4 + 3 < NUM_CHANNELS) 
                v.w = result[s * 4 + 3];
            int act_index = nn_get_act_out(s);
            nn_store_act(act_index, pixel, v);
        }
    }
}