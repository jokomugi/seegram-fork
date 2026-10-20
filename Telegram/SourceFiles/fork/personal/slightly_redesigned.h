#pragma once

#include "ui/rp_widget.h"
#include <vector>

namespace Settings {
struct IconDescriptor;
} // namespace Settings

namespace Ui {
class SettingsButton;
} // namespace Ui

namespace Fork::Personal {

inline constexpr auto kSlightlyRedesignedId = "slightly-redesigned";

[[nodiscard]] bool CheckSidebarIntegration();

class CompactMenuActions final : public Ui::RpWidget {
public:
	explicit CompactMenuActions(QWidget *parent);
	[[nodiscard]] not_null<Ui::SettingsButton*> addButton(
		rpl::producer<QString> text,
		::Settings::IconDescriptor descriptor);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	std::vector<not_null<Ui::SettingsButton*>> _buttons;

};

} // namespace Fork::Personal
