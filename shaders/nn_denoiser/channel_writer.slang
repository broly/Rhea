module channel_writer;


public struct ChannelWriter<uint size>
{
    uint cursor;
    float data[size];
    
    public __init()
    {
        cursor = 0;
    }
    
    [mutating]
    public void push(float channel)
    {
        data[cursor] = channel;
        cursor += 1;
    }
    
    [mutating]
    public void push(float2 channel)
    {
        data[cursor] = channel.r;
        data[cursor + 1] = channel.g;
        cursor += 2;
    }
        
    [mutating]
    public void push(float3 channel)
    {
        data[cursor] = channel.r;
        data[cursor + 1] = channel.g;
        data[cursor + 2] = channel.b;
        cursor += 3;
    }
    
    [mutating]
    public void push(float4 channel)
    {
        data[cursor] = channel.r;
        data[cursor + 1] = channel.g;
        data[cursor + 2] = channel.b;
        data[cursor + 3] = channel.a;
        cursor += 4;
    }
    
    
    
    [mutating]
    public void seek(uint index)
    {
        cursor = index;
    }
    
    
    public float[size] output()
    {
        return data;
    }
}

public void operator<<<uint size>(__ref ChannelWriter<size> lhs, float ch)
{
    lhs.push(ch);
}
    
public void operator<<<uint size>(__ref ChannelWriter<size> lhs, float2 ch)
{
    lhs.push(ch);
}

public void operator<<<uint size>(__ref ChannelWriter<size> lhs, float3 ch)
{
    lhs.push(ch);
}

public void operator<<<uint size>(__ref ChannelWriter<size> lhs, float4 ch)
{
    lhs.push(ch);
}

