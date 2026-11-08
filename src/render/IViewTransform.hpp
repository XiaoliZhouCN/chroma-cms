#pragma once

namespace chroma::render
{
/// 可选能力：视图变换（90° 步进旋转 / 缩放）。
/// 不是所有后端都必须实现；app 用 dynamic_cast 探测。
class IViewTransform
{
public:
    virtual ~IViewTransform() = default;

    /// 旋转 90° 的整数倍；正方向在屏幕上看是逆时针。
    /// quarterTurns 会按 4 取模，等价于 0/90/180/270 度。
    virtual void setQuarterTurns(int quarterTurns) = 0;

    /// 缩放倍数；1.0 = 原始大小，>1 放大，<1 缩小。
    virtual void setZoom(float zoom) = 0;
};
} // namespace chroma::render