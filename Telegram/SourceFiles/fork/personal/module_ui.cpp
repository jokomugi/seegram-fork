#include "fork/personal/module_ui.h"

#include "fork/personal/identity.h"
#include "fork/personal/module_manager.h"
#include "fork/personal/release_checker.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/rp_widget.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>

namespace Fork::Personal {
namespace {

constexpr auto kPollInterval = 100;

QString Phrase(QString english, QString russian) {
	return Lang::Resolved() == Lang::Language::Russian ? russian : english;
}

QString Title() {
	return Phrase(u"Plugins"_q, u"Плагины"_q);
}

QString StatusText(const ModuleSnapshot &module) {
	const auto state = [&] {
		switch (module.state) {
		case ModuleState::Disabled: return Phrase(u"Off"_q, u"Выключен"_q);
		case ModuleState::Checking: return Phrase(u"Checking…"_q, u"Проверка…"_q);
		case ModuleState::Running: return Phrase(u"Running"_q, u"Работает"_q);
		case ModuleState::Failed: return Phrase(u"Check failed"_q, u"Ошибка проверки"_q);
		case ModuleState::Incompatible:
			return Phrase(u"Incompatible"_q, u"Несовместим"_q);
		}
		Unexpected("Unknown personal module state");
	}();
	const auto reason = [&] {
		switch (module.error) {
		case ModuleError::None: return QString();
		case ModuleError::InterfaceMismatch:
			return Phrase(u"Module requires another app interface."_q,
				u"Модулю требуется другая версия интерфейса приложения."_q);
		case ModuleError::StartFailed:
			return Phrase(u"Could not start the module."_q, u"Не удалось запустить модуль."_q);
		case ModuleError::CheckFailed:
			return Phrase(u"The module rejected its self-check."_q, u"Самопроверка модуля не прошла."_q);
		case ModuleError::Timeout:
			return Phrase(u"The module stopped responding."_q, u"Модуль не отвечает."_q);
		case ModuleError::ProcessExited:
			return Phrase(u"The module process stopped."_q, u"Процесс модуля завершился."_q);
		case ModuleError::InvalidResponse:
			return Phrase(u"The module returned an invalid response."_q, u"Модуль вернул неверный ответ."_q);
		case ModuleError::PreferencesUnavailable:
			return Phrase(u"Could not read or save module settings."_q,
				u"Не удалось прочитать или сохранить настройки модуля."_q);
		}
		Unexpected("Unknown personal module error");
	}();
	return reason.isEmpty() ? state : state + u" · "_q + reason;
}

bool SwitchedOn(const ModuleSnapshot &module) {
	return module.state == ModuleState::Checking || module.state == ModuleState::Running;
}

class Runtime final : public QObject {
public:
	Runtime();
	void setEnabled(const std::string &id, bool enabled);
	ModuleManager manager;
	rpl::event_stream<> changed;

private:
	QTimer _timer;

};

class PluginsSection final : public ::Settings::Section<PluginsSection> {
public:
	PluginsSection(QWidget *parent, not_null<Window::SessionController*> controller);
	rpl::producer<QString> title() override;

};

Runtime::Runtime()
: QObject(QCoreApplication::instance())
, manager({ {
	"diagnostics", "1.0.0",
	QDir::cleanPath(QCoreApplication::applicationDirPath()
		+ u"/../Helpers/SeeGramModuleRunner"_q).toStdString(),
	{ "diagnostics" },
} }, {
	.preferences = (cWorkingDir() + u"tdata/personal_modules.preferences"_q).toStdString(),
	.applicationVersion = std::string(PERSONAL_SEEGRAM_RELEASE) + "/personal-" + kPersonalVersion,
}) {
	_timer.setInterval(kPollInterval);
	QObject::connect(&_timer, &QTimer::timeout, this, [=] {
		const auto previous = manager.modules();
		manager.poll();
		if (previous != manager.modules()) {
			changed.fire({});
		}
	});
	_timer.start();
}

void Runtime::setEnabled(const std::string &id, bool enabled) {
	manager.setEnabled(id, enabled);
	changed.fire({});
}

Runtime &Modules() {
	static auto runtime = new Runtime();
	return *runtime;
}

QString ModuleDescription(std::string_view id) {
	for (const auto &snapshot : Modules().manager.modules()) {
		if (snapshot.id == id) {
			return u"v"_q + QString::fromStdString(snapshot.version)
				+ u" · "_q + StatusText(snapshot);
		}
	}
	return QString();
}

rpl::producer<> Updates() {
	return rpl::single(rpl::empty)
		| rpl::then(rpl::merge(Modules().changed.events(), Lang::Changes()));
}

void ExportReport() {
	const auto report = QByteArray::fromStdString(Modules().manager.diagnosticReport());
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		Phrase(u"Save plugin diagnostics"_q, u"Сохранить диагностику плагинов"_q),
		u"JSON (*.json)"_q,
		u"seegram-personal-diagnostics.json"_q,
		[report](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			auto file = QSaveFile(path);
			const auto saved = file.open(QIODevice::WriteOnly)
				&& file.write(report) == report.size() && file.commit();
			Ui::Toast::Show(saved
				? Phrase(u"Diagnostics saved"_q, u"Диагностика сохранена"_q)
				: Phrase(u"Could not save diagnostics"_q, u"Не удалось сохранить диагностику"_q));
		});
}

void FillPlugins(not_null<Ui::VerticalLayout*> content) {
	Ui::AddSkip(content);
	const auto checker = new ReleaseChecker(content);
	const auto checkButton = ::Settings::AddButtonWithLabel(
		content,
		Updates() | rpl::map([] {
			return Phrase(u"Check SeeGram updates"_q, u"Проверить обновления SeeGram"_q);
		}),
		rpl::single(QString::fromLatin1(PERSONAL_SEEGRAM_RELEASE)),
		st::settingsButtonNoIcon
	);
	checkButton->addClickHandler([=] { checker->check(); });
	checker->value() | rpl::on_next([=](const ReleaseCheck &result) {
		checkButton->setDisabled(result.state == ReleaseCheckState::Checking);
	}, checkButton->lifetime());
	SettingsRows::AddDescription(content, rpl::combine(
		checker->value(),
		rpl::single(rpl::empty) | rpl::then(Lang::Changes())
	) | rpl::map([](const ReleaseCheck &result, auto) {
		switch (result.state) {
		case ReleaseCheckState::Idle:
			return Phrase(u"Check manually for a new SeeGram base release."_q,
				u"Ручная проверка новых релизов исходного SeeGram."_q);
		case ReleaseCheckState::Checking:
			return Phrase(u"Checking GitHub…"_q, u"Проверяем GitHub…"_q);
		case ReleaseCheckState::Failed:
			return Phrase(u"Could not check GitHub. Please try again later."_q,
				u"Не удалось проверить GitHub. Попробуйте позже."_q);
		case ReleaseCheckState::Current:
			return Phrase(u"The SeeGram base is up to date."_q, u"База SeeGram актуальна."_q);
		case ReleaseCheckState::Available:
			return Phrase(u"SeeGram "_q, u"SeeGram "_q) + result.release
				+ Phrase(u" is available. Rebuild the personal fork to keep your plugins."_q,
					u" доступен. Для сохранения плагинов нужно пересобрать персональный форк."_q);
		}
		Unexpected("Unknown personal release check state");
	}));
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	SettingsRows::AddDescription(content, Updates() | rpl::map([] {
		return Phrase(
			u"Built-in plugins are checked before activation. A failed plugin stays off; its settings are kept."_q,
			u"Встроенные плагины проходят проверку перед включением. При ошибке плагин остаётся выключенным, а его настройки сохраняются."_q);
	}));
	for (const auto &module : Modules().manager.modules()) {
		Ui::AddSkip(content);
		const auto id = module.id;
		auto description = Updates() | rpl::map([id] { return ModuleDescription(id); });
		const auto button = SettingsRows::AddToggle(
			content,
			Updates() | rpl::map([] {
				return Phrase(u"Plugin diagnostics"_q, u"Диагностика плагинов"_q);
			}),
			rpl::duplicate(description),
			Updates() | rpl::map([id] {
				for (const auto &snapshot : Modules().manager.modules()) {
					if (snapshot.id == id) {
						return SwitchedOn(snapshot);
					}
				}
				return false;
			}));
		std::move(description) | rpl::on_next([=](const QString &text) {
			button->setAccessibleDescription(text);
		}, button->lifetime());
		button->toggledChanges() | rpl::on_next([id](bool enabled) {
			for (const auto &snapshot : Modules().manager.modules()) {
				if (snapshot.id == id && enabled != SwitchedOn(snapshot)) {
					Modules().setEnabled(id, enabled);
					return;
				}
			}
		}, button->lifetime());
	}
	Ui::AddSkip(content);
	SettingsRows::AddDescription(content, Updates() | rpl::map([] {
		return Phrase(
			u"The diagnostic plugin checks activation and process health. It does not read chats or send messages."_q,
			u"Диагностический плагин проверяет включение и отклик процесса. Он не читает чаты и не отправляет сообщения."_q);
	}));
	Ui::AddSkip(content);
	::Settings::AddButtonWithLabel(
		content,
		Updates() | rpl::map([] {
			return Phrase(u"Export diagnostics"_q, u"Экспорт диагностики"_q);
		}),
		rpl::single(QString()),
		st::settingsButtonNoIcon
	)->addClickHandler([] { ExportReport(); });
	Ui::AddSkip(content);
}

void PluginsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(Updates() | rpl::map([] { return Title(); }));
	box->setWidth(st::boxWideWidth);
	FillPlugins(box->verticalLayout());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

PluginsSection::PluginsSection(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	FillPlugins(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> PluginsSection::title() {
	return Updates() | rpl::map([] { return Title(); });
}

} // namespace

void StartModules() {
	Modules();
}

void AddPluginsButton(::Settings::Builder::SectionBuilder &builder) {
	builder.addSectionButton({
		.title = Updates() | rpl::map([] { return Title(); }),
		.targetSection = PluginsSection::Id(),
		.icon = { &st::menuIconSettings },
	});
}

void AddIntroPluginsButton(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::Controller*> window) {
	::Settings::AddButtonWithLabel(
		content,
		Updates() | rpl::map([] { return Title(); }),
		rpl::single(QString()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] { window->show(Box(PluginsBox)); });
}

} // namespace Fork::Personal
