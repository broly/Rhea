export module rhmath:math_utils;

import <cstdint>;
import <memory>;


export namespace math
{
    
    float half_to_float(uint16_t h)
    {
        const uint32_t sign = (h & 0x8000u) << 16;
        uint32_t mantissa = h & 0x03FFu;
        uint32_t exp = h & 0x7C00u;

        if (exp == 0x7C00u) // Inf / NaN
        {
            exp = 0x3FC00u;
        }
        else if (exp != 0)
        {
            exp += 0x1C000u;
        }
        else if (mantissa != 0)
        {
            exp = 0x1C400u;
            do
            {
                mantissa <<= 1;
                exp -= 0x400u;
            } while ((mantissa & 0x400u) == 0);
            mantissa &= 0x3FFu;
        }

        uint32_t f = sign | ((exp | mantissa) << 13);
        float result;
        memcpy(&result, &f, sizeof(float));
        return result;
    }
}