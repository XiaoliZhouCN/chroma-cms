#include "render/skia/SkiaRenderer.hpp"

#include "render/skia/SkiaGpuBackend.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkPaint.h"
#include "include/core/SkImageInfo.h"
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
    // 初始化后端
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
    // step 1: 取离屏 canvas 并清屏。clear 色同时会作为 contain-fit 留边/图外区域的底色。
    SkCanvas *canvas = m_impl->surface->getCanvas();
    canvas->clear(SkColor4f{0.0f, 0.0f, 0.0f, 1.0f});

    // 没有图像或 SkSL 还没编译好：只保留清屏，直接返回。
    if (m_impl->image == nullptr || m_impl->imageEffect == nullptr) { return; }

    // step 2: 准备几何参数。
    //   surface* 是离屏目标尺寸（= native 纹理尺寸，物理像素）；
    //   image*   是源图尺寸；
    //   q/c/s    是 90° 步进旋转的 cos/sin；invZoom 是缩放倒数（采样空间是"缩小"的）。
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

    // step 3: contain-fit 视口。
    //   旋转 90°/270° 时图片的显示宽高比会翻转，所以用 effectiveAspect；
    //   先按比例缩到画面内，再居中，空出来的区域就是 letterbox 留边。
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

    // step 4: 合成"设备像素 -> 图片像素"的仿射矩阵。
    //   把三步合进一个矩阵：
    //     设备像素 -> contain-fit 视口内的 0..1 坐标
    //     以视口中心为原点做 旋转 + 缩放（invZoom）
    //     再映射到图片像素坐标（乘 imageSize）
    //   矩阵按行存；aspect 归一化在 x 方向做，旋转/缩放才能保持不拉伸。
    SkMatrix deviceToImage;
    deviceToImage.setAll(a00, a01, bx, a10, a11, by, 0.0f, 0.0f, 1.0f);

    // step 5: 取逆矩阵作为 shader 的 localMatrix。
    //   Skia 的 localMatrix 语义是"shader 局部空间 -> 绘制局部空间"；
    //   我们要的是相反的"设备像素 -> 图片像素"，所以必须取逆。
    SkMatrix localMatrix;
    if (!deviceToImage.invert(&localMatrix)) { return; }

    // step 5.1: 色彩 uniform。
    //   只有 3 项：图片尺寸（shader 里做边界判断）、outputScale（scRGB 缩放）、
    //   xyzToTarget（XYZ -> 目标色域矩阵，Windows=BT.709，macOS=Display-P3）。
    //   矩阵这里按行给，填 uniform 时转成列主序（SkSL 矩阵是列主序）。
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

    // step 5.2: 把 uniform 打包成 SkData。
    //   必须与 .sksl 里的声明顺序/大小完全一致（当前 48 B；float3x3 = 9 个 float 紧凑排列）。
    sk_sp<SkData> data = SkData::MakeWithCopy(&uniforms, sizeof(uniforms));

    // step 5.3: 创建图像子 shader（采样源）。
    //   clamp 边界 + 线性过滤；它是 runtime effect 的 child shader，不是最终 shader。
    sk_sp<SkShader> imageShader = m_impl->image->makeShader(
        SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kLinear));
    if (imageShader == nullptr) {
        std::fprintf(stderr, "[skia] image->makeShader 失败\n");
        return;
    }

    // step 6: 用 SkSL 程序 + uniform + 子 shader + 逆矩阵，生成最终 SkShader。
    //   失败时必须返回并打日志，否则 paint 拿到空 shader 会退化成纯黑。
    SkRuntimeEffect::ChildPtr children[] = {SkRuntimeEffect::ChildPtr(imageShader)};
    sk_sp<SkShader> shader = m_impl->imageEffect->makeShader(
        std::move(data), SkSpan<const SkRuntimeEffect::ChildPtr>(children, 1), &localMatrix);
    if (shader == nullptr) {
        std::fprintf(stderr, "[skia] makeShader(图像) 失败，检查 uniform/child 是否匹配\n");
        return;
    }

    // step 7: 与 D3D12/Metal 的 viewport/scissor 对齐。
    //   只允许画在 contain-fit 视口内；否则 zoom 放大后图像会溢出到留边区域。
    canvas->save();
    canvas->clipRect(SkRect::MakeLTRB(viewLeft, viewTop, viewLeft + viewWidth, viewTop + viewHeight));

    // step 8: 绘制。
    //   对整块 surface 画一个矩形：shader 内部负责"像素 -> 图片坐标 + 边界判断 + 色彩转换"。
    SkPaint paint;
    paint.setShader(std::move(shader));
    canvas->drawRect(SkRect::MakeWH(surfaceW, surfaceH), paint);

    // step 9: 恢复 canvas 状态（结束 step 7 的裁剪）。
    canvas->restore();
}

void SkiaRenderer::present()
{
    // step 0: 未初始化直接返回。
    if (!m_impl->initialized) { return; }

    // step 1: 平台 backend 开始一帧（D3D12: 取 backbuffer/RTV；Metal: 取 drawable）。
    //   失败（例如 nextDrawable 返回 nil）就跳过这一帧，不能继续提交。
    if (!m_impl->backend->beginFrame()) {
        return;
    }

    // step 2: Skia 把这一帧画到离屏 surface。此时只是录制命令，GPU 还没执行。
    drawFrame();
    
    // step 3: 提交这一帧。
    //   flush 阶段：Ganesh 把 canvas ops 转成 GrOps；首次采样图片时实例化懒纹理：
    //     创建 GPU 纹理 + 把 CPU 像素 memcpy 进 upload/staging buffer + 录制 upload 拷贝命令；
    //   submit 阶段：把命令交给 GPU（D3D12 ExecuteCommandLists / Metal commit）。
    //   注意：flushAndSubmit() 默认只提交、不等待 GPU 完成；GPU 的 copy/draw 是异步执行的。
    //   之后帧若命中纹理缓存，不再重复实例化/上传。
    m_impl->gr->flushAndSubmit();
    // m_impl->gr->flushAndSubmit(GrSyncCpu::kYes);


    // step 4: 平台 backend 结束一帧。
    //   把离屏纹理拷贝到后台缓冲（D3D12: CopyResource；Metal: blit），处理状态/同步并 present。
    //   因为同一队列 FIFO，拷贝一定在 step 3 的 Skia 提交之后执行，内容已就绪。
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

bool SkiaRenderer::readPixels(const std::vector<PixelPoint>& points,
                              std::vector<RgbaF32>& out)
{
    if (!m_impl->initialized || !m_impl->surface || !m_impl->gr || points.empty()) {
        return false;
    }

    // Probe 读取的是 present 后的 offscreen surface。
    // readPixels 前显式同步，避免读到上一帧或未完成的 GPU 内容。
    m_impl->gr->flushAndSubmit(GrSyncCpu::kYes);

    const int width = static_cast<int>(m_impl->width);
    const int height = static_cast<int>(m_impl->height);
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kRGBA_F32_SkColorType, kPremul_SkAlphaType);
    std::vector<float> pixels(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u);
    if (!m_impl->surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0)) {
        std::fprintf(stderr, "[skia] surface->readPixels 失败\n");
        return false;
    }

    out.clear();
    out.reserve(points.size());
    for (const PixelPoint& point : points) {
        RgbaF32 value{};
        if (point.x < static_cast<std::uint32_t>(width) &&
            point.y < static_cast<std::uint32_t>(height)) {
            const std::size_t index =
                (static_cast<std::size_t>(point.y) * static_cast<std::size_t>(width) +
                 point.x) * 4u;
            value = {pixels[index + 0], pixels[index + 1],
                     pixels[index + 2], pixels[index + 3]};
        }
        out.push_back(value);
    }
    return true;
}

void SkiaRenderer::setQuarterTurns(int quarterTurns)
{ m_impl->quarterTurns = ((quarterTurns % 4) + 4) % 4; }

void SkiaRenderer::setZoom(float zoom) { m_impl->zoom = (zoom > 0.0f) ? zoom : 1.0f; }

} // namespace chroma::render