#pragma once

#include "settings/settings_type.h"

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Fork::Personal {

void StartModules();
void AddPluginsButton(::Settings::Builder::SectionBuilder &builder);
void AddIntroPluginsButton(
	not_null<Ui::VerticalLayout*> content,
	not_null<Window::Controller*> window);

} // namespace Fork::Personal
