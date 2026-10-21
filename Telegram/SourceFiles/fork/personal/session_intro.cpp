#include "fork/personal/session_import.h"

#include "intro/intro_qr.h"
#include "intro/intro_widget.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "window/window_controller.h"

#include "styles/style_intro.h"

namespace Intro {

void Widget::setupImportLinks(not_null<Window::Controller*> controller) {
	_importLinks = Fork::Personal::SessionImport::CreateLinks(
		this,
		&_account->domain(),
		controller->uiShow());
	_importLinks->hide();
}

void Widget::updateImportLinks() {
	if (_importLinks) {
		_importLinks->setVisible(getStep()->hasCover() && _nextShown && !_showAnimation);
		_importLinks->moveToLeft(
			(width() - _importLinks->width()) / 2,
			_next->y() + _next->height() + st::normalFont->height / 2);
		_importLinks->raise();
	}
}

namespace details {

void QrWidget::setupImportControls() {
	_importScroll = Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea);
	_loginLinks = _importScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	_skip = _loginLinks->add(
		object_ptr<Ui::LinkButton>(
			_loginLinks,
			tr::lng_intro_qr_phone(tr::now)),
		{ 0, 0, 0, st::normalFont->height / 2 },
		Qt::AlignHCenter);
	const auto imports = Fork::Personal::SessionImport::CreateLinks(
		_loginLinks,
		&account().domain(),
		getData()->controller->uiShow());
	_loginLinks->add(
		object_ptr<Ui::VerticalLayout>::fromRaw(imports),
		style::al_justify);
	rpl::combine(sizeValue(), _loginLinks->heightValue()) | rpl::on_next([=](QSize size, int height) {
		const auto top = contentTop() + st::introQrSkipTop;
		_importScroll->setGeometry(
			0,
			top,
			size.width(),
			std::max(0, std::min(height, size.height() - top)));
		_loginLinks->resizeToWidth(size.width());
	}, _importScroll->lifetime());
	_importScroll->show();
}

void QrWidget::setupImportPasskey() {
	_passkey = _loginLinks->insert(
		1,
		object_ptr<Ui::LinkButton>(
			_loginLinks,
			tr::lng_intro_qr_passkey(tr::now)),
		{ 0, 0, 0, st::normalFont->height / 2 },
		Qt::AlignHCenter);
}

} // namespace details
} // namespace Intro
