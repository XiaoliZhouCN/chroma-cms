#include "render/skia/SkiaRenderer.hpp"

#include "render/skia/SkiaGpuBackend.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/gpu/ganesh/GrDirectContext.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace chroma::render {
namespace {

std::string readTextFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) { throw std::runtime_error("打开 SkSL 失败: " + path); }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) { throw std::runtime_error("SkSL 文件为空: " + path); }
    return content;
}

} // namespace

struct SkiaRenderer::Impl {
    std::unique_ptr<SkiaGpuBackend> backend;

    sk_sp<GrDirectContext> gr;
    sk_sp<SkSurface> surface;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t imageWidth = 0;
    std::uint32_t imageHeight = 0;
    int quarterTurns = 0;
    float zoom = 1.0f;
    bool initialized = false;

    sk_sp<SkRuntimeEffect> imageEffect;
    sk_sp<SkImage> image;
};

SkiaRenderer::SkiaRenderer() : m_impl(std::make_unique<Impl>()) {}
SkiaRenderer::~SkiaRenderer() = default;

const char *SkiaRenderer::backendName() const
{
    return m_impl->backend ? m_impl->backend->backendName() : "Skia";
}

bool SkiaRenderer::initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (m_impl->initialized) { return true; }
    if (nativeWindow == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("SkiaRenderer::initialize 参数无效");
    }

    m_impl->width = width;
    m_impl->height = height;

    // 1) 平台后端：D3D12 / Metal 的原生上下文与离屏目标
    m_impl->backend = createSkiaGpuBackend();
    if (!m_impl->backend->initialize(nativeWindow, width, height)) {
        throw std::runtime_error("SkiaGpuBackend::initialize 失败");
    }

    // 2) Ganesh 上下文由 backend 创建（D3D12: MakeD3D / Metal: MakeMetal）
    m_impl->gr = m_impl->backend->makeGaneshContext();
    if (!m_impl->gr) {
        throw std::runtime_error("SkiaGpuBackend::makeGaneshContext 失败");
    }

    // 3) SkSL：运行时从 <exe目录>/shaders/fullscreen.sksl 读取并编译
    const std::string skSL = readTextFile(m_impl->backend->shaderPath("fullscreen.sksl"));
    SkRuntimeEffect::Result result = SkRuntimeEffect::MakeForShader(SkString(skSL.c_str()));
    if (!result.effect) {
        throw std::runtime_error("SkSL 编译失败: " + std::string(result.errorText.c_str()));
    }
    m_impl->imageEffect = result.effect;

    createOffscreenSurface();

    m_impl->initialized = true;
    std::fprintf(stderr, "[skia] backend = %s, milestone = %d\n",
                 m_impl->backend->backendName(), SK_MILESTONE);
    return true;
}

void SkiaRenderer::createOffscreenSurface()
{
    m_impl->surface = m_impl->backend->createSurface(
        static_cast<int>(m_impl->width), static_cast<int>(m_impl->height));
    if (!m_impl->surface) {
        throw std::runtime_error("SkiaGpuBackend::createSurface 失败");
    }
}

void SkiaRenderer::drawFrame()
{
    SkCanvas *canvas = m_impl->surface->getCanvas();
    canvas->clear(SkColor4f{0.02f, 0.02f, 0.02f, 1.0f});

    if (m_impl->image == nullptr || m_impl->imageEffect == nullptr) { return; }

    const float surfaceW = static_cast<float>(m_impl->width);
    const float surfaceH = static_cast<float>(m_impl->height);
    const float imageW = static_cast<float>(m_impl->imageWidth);
    const float imageH = static_cast<float>(m_impl->imageHeight);
    const float imageAspect = (imageH > 0.0f) ? (imageW / imageH) : 1.0f;
    const float surfaceAspect = surfaceW / surfaceH;

    const int q = m_impl->quarterTurns & 3;
    const float c = (q == 0) ? 1.0f : (q == 2) ? -1.0f : 0.0f;
    const float s = (q == 1) ? 1.0f : (q == 3) ? -1.0f : 0.0f;
    const float invZoom = (m_impl->zoom > 0.0f) ? (1.0f / m_impl->zoom) : 1.0f;

    // 1) contain-fit 视口（90°/270° 时宽高比翻转）
    const float effectiveAspect = ((q & 1) != 0) ? (1.0f / imageAspect) : imageAspect;
    float viewWidth = surfaceW;
    float viewHeight = surfaceH;
    if (effectiveAspect > surfaceAspect) {
        viewHeight = surfaceW / effectiveAspect;
    } else {
        viewWidth = surfaceH * effectiveAspect;
    }
    const float viewLeft = (surfaceW - viewWidth) * 0.5f;
    const float viewTop = (surfaceH - viewHeight) * 0.5f;

    // 2) 设备像素 -> 图片像素 的仿射矩阵（旋转/缩放/视口全部编进来）
    const float cc = c * invZoom;
    const float ss = s * invZoom;
    const float u00 = cc;
    const float u01 = -ss / imageAspect;
    const float u10 = ss * imageAspect;
    const float u11 = cc;

    const float rx = viewLeft / viewWidth;
    const float ry = viewTop / viewHeight;

    const float a00 = imageW * u00 / viewWidth;
    const float a01 = imageW * u01 / viewHeight;
    const float a10 = imageH * u10 / viewWidth;
    const float a11 = imageH * u11 / viewHeight;
    const float bx = imageW * (0.5f - 0.5f * (u00 + u01) - (u00 * rx + u01 * ry));
    const float by = imageH * (0.5f - 0.5f * (u10 + u11) - (u10 * rx + u11 * ry));

    SkMatrix deviceToImage;
    deviceToImage.setAll(a00, a01, bx, a10, a11, by, 0.0f, 0.0f, 1.0f);

    // 3) makeShader 的 localMatrix 语义是"shader 局部空间 -> 绘制局部空间"，
    //    所以这里传 设备像素->图片像素 的逆矩阵（实测方向，否则图像会缩成一小块）。
    SkMatrix localMatrix;
    if (!deviceToImage.invert(&localMatrix)) { return; }

    // 4) 色彩 uniform
    struct ImageUniforms {
        float imageSize[2];
        float outputScale;
        float xyzToTarget[9]; // float3x3：9 个 float 紧凑排列（每列 3 个）
    };
    ImageUniforms uniforms{};
    uniforms.imageSize[0] = imageW;
    uniforms.imageSize[1] = imageH;

    float xyzToTargetRows[3][3] = {};
#if defined(_WIN32)
    uniforms.outputScale = 0.8f;
    xyzToTargetRows[0][0] = 3.240970f;
    xyzToTargetRows[0][1] = -1.537383f;
    xyzToTargetRows[0][2] = -0.498611f;
    xyzToTargetRows[1][0] = -0.969244f;
    xyzToTargetRows[1][1] = 1.875968f;
    xyzToTargetRows[1][2] = 0.041555f;
    xyzToTargetRows[2][0] = 0.055630f;
    xyzToTargetRows[2][1] = -0.203977f;
    xyzToTargetRows[2][2] = 1.056972f;
#elif defined(__APPLE__)
    uniforms.outputScale = 1.0f;
    xyzToTargetRows[0][0] = 2.4934969119f;
    xyzToTargetRows[0][1] = -0.9313836179f;
    xyzToTargetRows[0][2] = -0.4027107845f;
    xyzToTargetRows[1][0] = -0.8294889696f;
    xyzToTargetRows[1][1] = 1.7626640603f;
    xyzToTargetRows[1][2] = 0.0236246858f;
    xyzToTargetRows[2][0] = 0.0358458302f;
    xyzToTargetRows[2][1] = -0.0761723893f;
    xyzToTargetRows[2][2] = 0.9568845240f;
#endif
    for (int col = 0; col < 3; ++col) {
        uniforms.xyzToTarget[col * 3 + 0] = xyzToTargetRows[0][col];
        uniforms.xyzToTarget[col * 3 + 1] = xyzToTargetRows[1][col];
        uniforms.xyzToTarget[col * 3 + 2] = xyzToTargetRows[2][col];
    }

    sk_sp<SkData> data = SkData::MakeWithCopy(&uniforms, sizeof(uniforms));

    sk_sp<SkShader> imageShader = m_impl->image->makeShader(
        SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kLinear));
    if (imageShader == nullptr) {
        std::fprintf(stderr, "[skia] image->makeShader 失败\n");
        return;
    }

    SkRuntimeEffect::ChildPtr children[] = {SkRuntimeEffect::ChildPtr(imageShader)};
    sk_sp<SkShader> shader = m_impl->imageEffect->makeShader(
        std::move(data), SkSpan<const SkRuntimeEffect::ChildPtr>(children, 1), &localMatrix);
    if (shader == nullptr) {
        std::fprintf(stderr, "[skia] makeShader(图像) 失败，检查 uniform/child 是否匹配\n");
        return;
    }

    // 5) 与 D3D12 的 viewport/scissor 对齐：只画在 contain-fit 视口内，
    //    否则放大后图像会溢出到留边区域。
    canvas->save();
    canvas->clipRect(SkRect::MakeLTRB(viewLeft, viewTop, viewLeft + viewWidth, viewTop + viewHeight));

    SkPaint paint;
    paint.setShader(std::move(shader));
    canvas->drawRect(SkRect::MakeWH(surfaceW, surfaceH), paint);

    canvas->restore();
}
void SkiaRenderer::present()
{
    if (!m_impl->initialized) { return; }

    if (!m_impl->backend->beginFrame()) {
        return;
    }

    // 1) Skia 画到离屏纹理，并提交（与原生队列同一队列，FIFO 保序）
    drawFrame();
    m_impl->gr->flushAndSubmit();

    // 2) 平台 backend 负责把离屏内容拷贝到后台缓冲并 present
    m_impl->backend->endFrame(*m_impl->surface);
}

void SkiaRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_impl->initialized || width == 0 || height == 0) { return; }
    if (width == m_impl->width && height == m_impl->height) { return; }

    m_impl->surface.reset();
    m_impl->backend->resize(width, height);
    m_impl->width = width;
    m_impl->height = height;
    createOffscreenSurface();
}

// S1 先只记录尺寸，S2 再做 UNORM16 -> F16 与 drawImage
void SkiaRenderer::setImage(std::uint32_t width, std::uint32_t height, const std::uint16_t *rgba16)
{
    if (!m_impl->initialized || rgba16 == nullptr || width == 0 || height == 0) { return; }

    // UNORM16 数据直接当 kR16G16B16A16_unorm 用：采样时 Skia 自动归一化到 [0,1]，
    // 不需要在 CPU 侧手动转 F16（省一次 2048² 的转换）。
    const SkImageInfo info = SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height),
                                               kR16G16B16A16_unorm_SkColorType, kOpaque_SkAlphaType);
    const SkPixmap pixmap(info, rgba16, info.minRowBytes());
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
    if (image == nullptr) {
        std::fprintf(stderr, "[skia] RasterFromPixmapCopy 失败\n");
        return;
    }

    m_impl->image = image;
    m_impl->imageWidth = width;
    m_impl->imageHeight = height;
    std::fprintf(stderr, "[skia] image = %u x %u (UNORM16 -> kR16G16B16A16_unorm)\n", width, height);
}

void SkiaRenderer::setQuarterTurns(int quarterTurns)
{ m_impl->quarterTurns = ((quarterTurns % 4) + 4) % 4; }

void SkiaRenderer::setZoom(float zoom) { m_impl->zoom = (zoom > 0.0f) ? zoom : 1.0f; }

} // namespace chroma::render