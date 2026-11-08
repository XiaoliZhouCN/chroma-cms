#include <metal_stdlib>
using namespace metal;

struct VsOut
{
    float4 position [[position]];
    float2 uv;
};

// 全屏三角形：不传顶点缓冲，用 vertex_id 现算三个顶点。
vertex VsOut VSMain(uint vertexId [[vertex_id]])
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VsOut output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

struct ShaderParams
{
    float outputScale;
    float aspect;      // imageWidth / imageHeight
    float m00;         // 采样逆变换矩阵，已经包含 1/zoom
    float m01;
    float m10;
    float m11;
    float reserved0;
    float reserved1;
};

constant float kPqPeakNits   = 10000.0;
constant float kScRgbRefNits = 80.0;

// BT.2020 线性 RGB -> CIE XYZ (D65)
// MSL float3x3 按列初始化，这里给的是 HLSL 行矩阵的转置。
constant float3x3 kRec2020ToXYZ = float3x3(
    float3(0.636958, 0.262700, 0.000000),
    float3(0.144617, 0.677998, 0.028073),
    float3(0.168881, 0.059302, 1.060985));

// CIE XYZ -> Display-P3 线性 RGB (D65)
// 行矩阵：
//   2.4934969119  -0.9313836179  -0.4027107845
//  -0.8294889696   1.7626640603   0.0236246858
//   0.0358458302  -0.0761723893   0.9568845240
constant float3x3 kXYZToDisplayP3 = float3x3(
    float3( 2.4934969119, -0.8294889696,  0.0358458302),
    float3(-0.9313836179,  1.7626640603, -0.0761723893),
    float3(-0.4027107845,  0.0236246858,  0.9568845240));

// ST.2084 PQ EOTF：归一化码值 -> 相对亮度（1.0 = 10000 nits）
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

// 以中心为原点做旋转 + 缩放。
// 采样坐标用逆变换，所以正角度在屏幕上看起来是逆时针。
// 90° 步进的旋转 + 缩放，已经由 CPU 合成到 m00..m11。
// 这里只做 2×2 矩阵乘，不再算 cos/sin。
float2 TransformUV(float2 uv, constant ShaderParams& params)
{
    float2 p = (uv - 0.5) * float2(params.aspect, 1.0);
    p = float2(params.m00 * p.x + params.m01 * p.y,
               params.m10 * p.x + params.m11 * p.y);
    return p / float2(params.aspect, 1.0) + 0.5;
}

fragment float4 PSMain(VsOut input [[stage_in]],
                       texture2d<float> tex [[texture(0)]],
                       sampler smp [[sampler(0)]],
                       constant ShaderParams& params [[buffer(0)]])
{
    const float2 uv = TransformUV(input.uv, params);

    // 旋转/缩小后落到图片外的像素直接丢弃，露出 render pass 的 clear color。
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        discard_fragment();
    }

    float3 code = tex.sample(smp, uv).rgb;             // 归一化 PQ 码值

    float3 linear2020 = PqEotf(code);                  // 相对亮度，1.0 = 10000 nits
    float3 xyz        = kRec2020ToXYZ * linear2020;
    float3 linearP3   = kXYZToDisplayP3 * xyz;

    // 超出 Display-P3 色域的颜色裁掉负值
    linearP3 = max(linearP3, 0.0);

    // 相对亮度 -> 绝对亮度 -> scRGB 单位（1.0 = 80 nits）
    float3 nits   = linearP3 * kPqPeakNits;
    float3 scaled = (nits / kScRgbRefNits) * params.outputScale;
    return float4(scaled, 1.0);
}

