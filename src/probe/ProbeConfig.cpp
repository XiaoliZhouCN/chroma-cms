#include "probe/ProbeConfig.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QTextStream>

namespace chroma::probe {
namespace {

QString resolveRepoPath(const QString& raw, const QString& repoRoot)
{
    QString path = raw;
    if (path.startsWith("file:")) {
        path = path.mid(5);
    }
    if (path.startsWith("./")) {
        path = path.mid(2);
    }
    if (path.startsWith('/')) {
        path = path.mid(1);   // card_config 中的 /assets 视为仓库相对路径
    }
    const QFileInfo direct(path);
    if (direct.isAbsolute() && direct.exists()) {
        return direct.absoluteFilePath();
    }
    return QDir(repoRoot).filePath(path);
}

bool readJsonObject(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法打开 JSON: %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("JSON 解析失败: %1 (%2)")
                     .arg(path, parseError.errorString());
        return false;
    }
    *object = document.object();
    return true;
}

} // namespace

bool ProbeConfig::load(const QString& configPath, const QString& repoRoot, QString* error)
{
    patches.clear();

    QJsonObject config;
    if (!readJsonObject(configPath, &config, error)) {
        return false;
    }

    const QString rawImage = config.value(QStringLiteral("image")).toString();
    const QString rawCoordinates = config.value(QStringLiteral("loc_config")).toString();
    if (rawImage.isEmpty() || rawCoordinates.isEmpty()) {
        *error = QStringLiteral("card config 缺少 image 或 loc_config: %1").arg(configPath);
        return false;
    }

    imagePath = resolveRepoPath(rawImage, repoRoot);
    coordinatePath = resolveRepoPath(rawCoordinates, repoRoot);
    outputPath = QDir(repoRoot).filePath(QStringLiteral("src/probe/output/card_color.csv"));

    if (!QFileInfo::exists(imagePath)) {
        *error = QStringLiteral("测试图不存在: %1").arg(imagePath);
        return false;
    }
    if (!QFileInfo::exists(coordinatePath)) {
        *error = QStringLiteral("坐标 JSON 不存在: %1").arg(coordinatePath);
        return false;
    }

    QJsonObject coordinates;
    if (!readJsonObject(coordinatePath, &coordinates, error)) {
        return false;
    }
    const QJsonArray patchArray = coordinates.value(QStringLiteral("patches")).toArray();
    if (patchArray.isEmpty()) {
        *error = QStringLiteral("坐标 JSON 缺少 patches: %1").arg(coordinatePath);
        return false;
    }

    for (const QJsonValue& value : patchArray) {
        if (!value.isObject()) {
            *error = QStringLiteral("坐标 JSON 中存在非对象 patch");
            return false;
        }
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QJsonObject image = item.value(QStringLiteral("image")).toObject();
        const double x = image.value(QStringLiteral("x")).toDouble(-1.0);
        const double y = image.value(QStringLiteral("y")).toDouble(-1.0);
        if (name.isEmpty() || x < 0.0 || y < 0.0) {
            *error = QStringLiteral("patch 缺少 name/image.x/image.y: %1").arg(name);
            return false;
        }
        patches.push_back({name, QPointF(x, y)});
    }

    return true;
}

} // namespace chroma::probe
