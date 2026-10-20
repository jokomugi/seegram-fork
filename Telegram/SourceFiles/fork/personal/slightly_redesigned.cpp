#include "fork/personal/slightly_redesigned.h"

#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"

#include <QtGui/QFocusEvent>
#include <algorithm>
#include <array>

#include "styles/style_info_profile_top_bar.h"
#include "styles/style_menu_icons.h"
#include "styles/style_slightly_redesigned.h"
#include "styles/style_window_main_menu.h"

namespace Fork::Personal {
namespace {

class ActionButton final : public Ui::SettingsButton {
public:
	ActionButton(QWidget *parent, rpl::producer<QString> text);
	QString accessibilityName() override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	QString _label;

};

ActionButton::ActionButton(QWidget *parent, rpl::producer<QString> text)
: SettingsButton(parent, rpl::single(QString()), st::personalSidebarAction) {
	setFocusPolicy(Qt::StrongFocus);
	std::move(text) | rpl::on_next([=](const QString &label) {
		_label = label;
		setAccessibleName(label);
	}, lifetime());
	Ui::InstallTooltip(this, [=] { return _label; });
}

QString ActionButton::accessibilityName() {
	return _label;
}

void ActionButton::paintEvent(QPaintEvent *event) {
	if (hasFocus() && !isDisabled()) {
		auto p = Painter(this);
		paintBg(p, event->rect(), true);
		paintRipple(p, 0, 0);
	} else {
		SettingsButton::paintEvent(event);
	}
}

void ActionButton::focusInEvent(QFocusEvent *event) {
	SettingsButton::focusInEvent(event);
	update();
}

void ActionButton::focusOutEvent(QFocusEvent *event) {
	SettingsButton::focusOutEvent(event);
	update();
}

} // namespace

bool CheckSidebarIntegration() {
	const auto icons = std::array{
		&st::menuIconGroups, &st::menuIconChannel,
		&st::menuIconUserShow, &st::menuIconPhone,
	};
	const auto available = st::mainMenuWidth / int(icons.size());
	return st::personalSidebarCoverHeight
			>= st::infoProfileTopBarStatusTop + st::normalFont->height
		&& std::all_of(icons.begin(), icons.end(), [&](const style::icon *icon) {
			return icon->width() > 0 && icon->height() > 0
				&& icon->width() <= available
				&& icon->height() <= st::personalSidebarAction.height;
		});
}

CompactMenuActions::CompactMenuActions(QWidget *parent) : RpWidget(parent) {
	resizeToWidth(st::mainMenuWidth);
}

not_null<Ui::SettingsButton*> CompactMenuActions::addButton(
		rpl::producer<QString> text,
		::Settings::IconDescriptor descriptor) {
	Expects(_buttons.size() < 4);
	const auto button = Ui::CreateChild<ActionButton>(this, std::move(text));
	auto iconStyle = base::duplicate(st::personalSidebarAction);
	iconStyle.iconLeft = (st::mainMenuWidth / 4 - descriptor.icon->width()) / 2;
	::Settings::AddButtonIcon(button, iconStyle, std::move(descriptor));
	_buttons.push_back(button);
	resizeGetHeight(width());
	button->show();
	return button;
}

int CompactMenuActions::resizeGetHeight(int newWidth) {
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		const auto left = newWidth * i / 4;
		const auto right = newWidth * (i + 1) / 4;
		_buttons[i]->resizeToWidth(right - left);
		_buttons[i]->moveToLeft(left, 0, newWidth);
	}
	return st::personalSidebarAction.height;
}

} // namespace Fork::Personal
