#include "fork/personal/release_policy.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>

namespace Fork::Personal {
namespace {

using Version = std::array<std::uint32_t, 5>;

std::optional<Version> Parse(std::string_view value) {
	if (value.size() < 2 || value.size() > 64 || value.front() != 'v') {
		return std::nullopt;
	}
	value.remove_prefix(1);
	auto result = Version();
	auto index = std::size_t(0);
	while (!value.empty()) {
		const auto end = value.find_first_of(".-");
		const auto part = value.substr(0, end);
		const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result[index]);
		if (part.empty() || parsed.ec != std::errc()
			|| parsed.ptr != part.data() + part.size()) {
			return std::nullopt;
		}
		if (end == std::string_view::npos) {
			return result;
		}
		if (index == 4 || (value[end] == '.' && index == 3)) {
			return std::nullopt;
		}
		index = value[end] == '-' ? 4 : index + 1;
		value.remove_prefix(end + 1);
	}
	return std::nullopt;
}

} // namespace

ReleaseRelation CompareReleases(std::string_view current, std::string_view latest) {
	const auto base = Parse(current);
	const auto candidate = Parse(latest);
	if (!base || !candidate) {
		return ReleaseRelation::Invalid;
	}
	return *candidate == *base ? ReleaseRelation::Current
		: *candidate > *base ? ReleaseRelation::Newer
		: ReleaseRelation::Older;
}

} // namespace Fork::Personal
