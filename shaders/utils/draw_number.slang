
float sdBox(float2 p, float2 b)
{
    float2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float segment(float2 uv, float2 pos, float2 size)
{
    float d = sdBox(uv - pos, size);
    return 1.0 - smoothstep(0.0, 0.015, d);
}

float DrawDigit(float2 uv, int digit)
{
    float v = 0.0;

    bool seg[7];

    [unroll]
    for (int i = 0; i < 7; i++)
        seg[i] = false;

    switch (digit)
    {
        case 0: seg[0]=seg[1]=seg[2]=seg[4]=seg[5]=seg[6]=true; break;
        case 1: seg[2]=seg[5]=true; break;
        case 2: seg[0]=seg[2]=seg[3]=seg[4]=seg[6]=true; break;
        case 3: seg[0]=seg[2]=seg[3]=seg[5]=seg[6]=true; break;
        case 4: seg[1]=seg[2]=seg[3]=seg[5]=true; break;
        case 5: seg[0]=seg[1]=seg[3]=seg[5]=seg[6]=true; break;
        case 6: seg[0]=seg[1]=seg[3]=seg[4]=seg[5]=seg[6]=true; break;
        case 7: seg[0]=seg[2]=seg[5]=true; break;

        case 8:
            [unroll]
            for (int i = 0; i < 7; i++)
                seg[i] = true;
            break;

        case 9: seg[0]=seg[1]=seg[2]=seg[3]=seg[5]=seg[6]=true; break;
    }

    float2 hSize = float2(0.24, 0.06);
    float2 vSize = float2(0.06, 0.24);

    if (seg[0]) v += segment(uv, float2(0.0,  0.70), hSize);
    if (seg[3]) v += segment(uv, float2(0.0,  0.00), hSize);
    if (seg[6]) v += segment(uv, float2(0.0, -0.70), hSize);

    if (seg[1]) v += segment(uv, float2(-0.28,  0.35), vSize);
    if (seg[2]) v += segment(uv, float2( 0.28,  0.35), vSize);
    if (seg[4]) v += segment(uv, float2(-0.28, -0.35), vSize);
    if (seg[5]) v += segment(uv, float2( 0.28, -0.35), vSize);

    return saturate(v);
}

// Draw number over existing image
float3 DrawNumber99(
    float2 uv,
    float3 background,
    int number,
    float2 screenPos, // 0..1
    float scale,
    float3 color)
{
    number = clamp(number, 0, 99);

    int tens = number / 10;
    int ones = number % 10;

    // local space
    float2 p = uv;
    p.y = 1.0 - p.y;
    
    p -= screenPos;

    // aspect correction
    p.x *= 16.0 / 9.0;

    p /= scale;

    float mask = 0.0;

    // left digit
    {
        float2 d = p;
        d.x += 0.42;

        mask += DrawDigit(d, tens);
    }

    // right digit
    {
        float2 d = p;
        d.x -= 0.42;

        mask += DrawDigit(d, ones);
    }

    mask = saturate(mask);

    // alpha blend over background
    return lerp(background, color, mask);
}

// Draw number over existing image
float3 DrawNumber9999(
    float2 uv,
    float3 background,
    int number,
    float2 screenPos, // 0..1
    float scale,
    float3 color)
{
    number = clamp(number, 0, 9999);

    int d0 = (number / 1000) % 10;
    int d1 = (number / 100)  % 10;
    int d2 = (number / 10)   % 10;
    int d3 =  number % 10;

    // local space
    float2 p = uv;
    p.y = 1.0 - p.y;

    p -= screenPos;

    // aspect correction
    p.x *= 16.0 / 9.0;

    p /= scale;

    float mask = 0.0;

    // spacing between digits
    float spacing = 0.42;

    // digit 1 (thousands)
    {
        float2 d = p;
        d.x += spacing * 3.0;
        mask += DrawDigit(d, d0);
    }

    // digit 2 (hundreds)
    {
        float2 d = p;
        d.x += spacing;
        mask += DrawDigit(d, d1);
    }

    // digit 3 (tens)
    {
        float2 d = p;
        d.x -= spacing;
        mask += DrawDigit(d, d2);
    }

    // digit 4 (ones)
    {
        float2 d = p;
        d.x -= spacing * 3.0;
        mask += DrawDigit(d, d3);
    }

    mask = saturate(mask);

    // alpha blend over background
    return lerp(background, color, mask);
}