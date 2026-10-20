#pragma once

#include <string_view>

namespace Fork::Personal {

enum class ReleaseRelation { Current, Newer, Older, Invalid };

[[nodiscard]] ReleaseRelation CompareReleases(std::string_view current, std::string_view latest);

} // namespace Fork::Personal
