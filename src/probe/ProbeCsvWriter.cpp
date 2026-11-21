#include "probe/ProbeCsvWriter.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <iomanip>
#include <sstream>

namespace chroma::probe {

bool ProbeCsvWriter::write(const QString& path, const std::vector<ProbeValue>& values,
                           QString* error)
{
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        *error = QStringLiteral("无法创建输出目录: %1").arg(info.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("无法打开输出文件: %1").arg(path);
        return false;
    }

    QTextStream stream(&file);
    stream << "block,r,g,b\n";
    for (const ProbeValue& value : values) {
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << std::setprecision(9)
             << value.r << ',' << value.g << ',' << value.b;
        stream << value.block << ',' << QString::fromStdString(line.str()) << '\n';
    }

    if (!file.commit()) {
        *error = QStringLiteral("写入输出文件失败: %1").arg(path);
        return false;
    }
    return true;
}

} // namespace chroma::probe
