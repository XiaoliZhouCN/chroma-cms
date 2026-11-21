#pragma once

#include <QString>

#include <vector>

namespace chroma::probe {

struct ProbeValue {
    QString block;
    double r{};
    double g{};
    double b{};
};

class ProbeCsvWriter {
public:
    static bool write(const QString& path, const std::vector<ProbeValue>& values,
                      QString* error);
};

} // namespace chroma::probe
