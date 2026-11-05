// M1 着色器：全屏三角形 + 保持比例采样。
// 明确不做色彩管理：采样值直接写入 R10G10B10A2_UNORM 后台缓冲。
// 见 Docs/COLOR_MANAGEMENT.md §3（Pass 1/Pass 2 在 M2 才引入）。

cbuffer ImageConstants : register(b0)
{
    float2 g_uvScale;   // 图像在屏幕 UV 空间的缩放（contain-fit）
    float2 g_uvOffset;  // 居中偏移
};

Texture2D<float4> g_image : register(t0);
SamplerState g_sampler : register(s0);

struct VsOut
{
    float4 position : SV_Position;
    float2 screenUv : TEXCOORD0;
};

VsOut VSMain(uint vertexId : SV_VertexID)
{
    // 一个覆盖全屏的三角形：(0,0) (2,0) (0,2)
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VsOut output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.screenUv = uv;
    return output;
}

float4 PSMain(VsOut input) : SV_Target
{
    float2 imageUv = (input.screenUv - g_uvOffset) / g_uvScale;

    if (any(imageUv < 0.0) || any(imageUv > 1.0))
    {
        // 留边区域：中性深灰，便于判断构图是否居中
        return float4(0.07, 0.07, 0.08, 1.0);
    }

    return float4(g_image.Sample(g_sampler, imageUv).rgb, 1.0);
}
