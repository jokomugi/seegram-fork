#include "fork/personal/release_policy.h"

#include <iostream>

using Fork::Personal::CompareReleases;
using Fork::Personal::ReleaseRelation;

int main() {
	if (CompareReleases("v7.3-1", "v7.3-2") != ReleaseRelation::Newer
		|| CompareReleases("v7.3-1", "v7.3-1") != ReleaseRelation::Current
		|| CompareReleases("v7.3-2", "v7.3-1") != ReleaseRelation::Older
		|| CompareReleases("v7.3-20", "v7.4-1") != ReleaseRelation::Newer
		|| CompareReleases("v7.3-1", "v7.3-beta") != ReleaseRelation::Invalid) {
		std::cerr << "FAIL: stable release comparison must distinguish base versions and build counters\n";
		return 1;
	}
	std::cout << "PASS: manual update check recognizes newer stable releases\n";
}
