#pragma once

#include "settings/settings_type.h"

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Fork::Personal {

void StartModules();
void AddPluginsButton(::Settings::Builder::SectionBuilder &builder);

} // namespace Fork::Personal
