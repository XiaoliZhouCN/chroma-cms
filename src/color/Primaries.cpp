#include "color/Primaries.hpp"

namespace chroma::color
{
Xyz whitePoint(const Primaries &primaries)
{
    // Y 归一化为 1；X = x/y，Z = (1 - x - y)/y。
    return {primaries.wx / primaries.wy,
            1.0,
            (1.0 - primaries.wx - primaries.wy) / primaries.wy};
}
} // namespace chroma::color
