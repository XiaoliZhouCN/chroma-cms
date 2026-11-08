struct VsOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VsOut VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VsOut output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

cbuffer ShaderParams : register(b0)
{
    float outputScale;   // root constants[0]
    float aspect;        // root constants[1]：imageWidth / imageHeight
    float m00;           // root constants[2..5]：采样逆变换矩阵（已含 1/zoom）
    float m01;
    float m10;
    float m11;
    float reserved0;
    float reserved1;
};

Texture2D<float4> g_texture : register(t0);
SamplerState      g_sampler : register(s0);

// ---- 色彩常量 ----
static const float kPqPeakNits       = 10000.0;  // PQ 编码的峰值亮度
static const float kScRgbRefNits     = 80.0;     // scRGB：1.0 对应 80 nits

// BT.2020 线性 RGB -> CIE XYZ (D65)
static const float3x3 kRec2020ToXYZ =
{
    { 0.636958, 0.144617, 0.168881 },
    { 0.262700, 0.677998, 0.059302 },
    { 0.000000, 0.028073, 1.060985 },
};

// CIE XYZ -> BT.709 线性 RGB (D65)
static const float3x3 kXYZToRec709 =
{
    {  3.240970, -1.537383, -0.498611 },
    { -0.969244,  1.875968,  0.041555 },
    {  0.055630, -0.203977,  1.056972 },
};

// ST.2084 PQ EOTF：码值 -> 相对亮度（1.0 = 10000 nits）
float3 PqEotf(float3 code)
{
    const float m1 = 0.1593017578125;    // 2610/16384
    const float m2 = 78.84375;           // 2523/4096 * 128
    const float c1 = 0.8359375;          // 3424/4096
    const float c2 = 18.8515625;         // 2413/4096 * 32
    const float c3 = 18.6875;            // 2392/4096 * 32

    float3 p = pow(max(code, 0.0), 1.0 / m2);
    float3 numerator = max(p - c1, 0.0);
    float3 denominator = c2 - c3 * p;
    return pow(numerator / max(denominator, 1e-6), 1.0 / m1);
}

// 90° 步进旋转 + 缩放：矩阵已由 CPU 合成到 m00..m11（采样逆变换）
float2 TransformUV(float2 uv)
{
    float2 p = (uv - 0.5) * float2(aspect, 1.0);
    p = float2(m00 * p.x + m01 * p.y,
               m10 * p.x + m11 * p.y);
    return p / float2(aspect, 1.0) + 0.5;
}

float4 PSMain(VsOut input) : SV_Target
{
    const float2 uv = TransformUV(input.uv);

    // 旋转/缩小后落到图片外的像素直接丢弃，露出 clear color
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        discard;
    }

    float3 code = g_texture.Sample(g_sampler, uv).rgb;   // 归一化 PQ 码值

    float3 linear2020 = PqEotf(code);              // 相对亮度，1.0 = 10000 nits
    float3 xyz        = mul(kRec2020ToXYZ, linear2020);
    float3 linear709  = mul(kXYZToRec709, xyz);

    // 负值（超出 BT.709 色域的颜色）裁掉，避免 DWM 出现异常
    linear709 = max(linear709, 0.0);

    // 相对亮度 -> 绝对亮度 -> scRGB 单位（1.0 = 80 nits）
    float3 nits = linear709 * kPqPeakNits;
    float3 scaled = (nits / kScRgbRefNits) * outputScale;
    return float4(scaled, 1.0);
}