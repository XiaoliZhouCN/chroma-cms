// 不需要顶点缓冲：用 SV_VertexID 现算三个顶点，覆盖整个视口。

struct VsOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VsOut VSMain(uint vertexId : SV_VertexID)
{
    // 三个顶点在 UV 空间是 (0,0)、(2,0)、(0,2)，
    // 映射到裁剪空间后是一个盖住整个屏幕的大三角形（多余部分被裁掉）。
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VsOut output;
    // output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

Texture2D<float4> g_texture : register(t0);
SamplerState      g_sampler : register(s0);

float4 PSMain(VsOut input) : SV_Target
{
    return g_texture.Sample(g_sampler, input.uv);
}