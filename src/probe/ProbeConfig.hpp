#pragma once

#include <QPointF>
#include <QString>

#include <vector>

namespace chroma::probe {

struct PatchCoordinate {
    QString block;
    QPointF image;   // 源图坐标（像素）
};

struct ProbeConfig {
    QString imagePath;
    QString coordinatePath;
    QString outputPath;
    std::vector<PatchCoordinate> patches;

    /// configPath: 17_ramp_config.json 的绝对路径
    /// repoRoot:   仓库根目录（用于解析 /assets、./assets 这类仓库相对路径）
    bool load(const QString& configPath, const QString& repoRoot, QString* error);
};

} // namespace chroma::probe
