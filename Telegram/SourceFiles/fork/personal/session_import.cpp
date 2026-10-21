#include "fork/personal/session_import.h"

#include "fork/personal/session_source.h"
#include "fork/fork_lang.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/sender.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>

#include "styles/style_boxes.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

namespace Fork::Personal::SessionImport {
namespace {

QString Phrase(QString english, QString russian) {
	return Lang::Resolved() == Lang::Language::Russian ? russian : english;
}

QString ErrorText(Error error) {
	switch (error) {
	case Error::None: return QString();
	case Error::LiveDatabase: return Phrase(
		u"Close the source client and make a complete database backup."_q,
		u"Закройте исходный клиент и сделайте полную копию базы."_q);
	case Error::PasscodeRequired: return Phrase(
		u"Local passcode required or incorrect."_q,
		u"Нужен локальный код-пароль или он введён неверно."_q);
	case Error::Duplicate: return Phrase(
		u"Already added or repeated in this selection."_q,
		u"Уже добавлен или повторяется в выборе."_q);
	case Error::Bot: return Phrase(
		u"Bot sessions are not supported."_q,
		u"Сессии ботов не поддерживаются."_q);
	case Error::UnsafeArchive: return Phrase(
		u"Unsafe or unsupported ZIP archive."_q,
		u"Небезопасный или неподдерживаемый ZIP-архив."_q);
	case Error::AmbiguousRoot: return Phrase(
		u"Several tdata roots found. Select one folder."_q,
		u"Найдено несколько tdata. Выберите конкретную папку."_q);
	case Error::MissingKey: return Phrase(
		u"No complete authorization key found."_q,
		u"Полный ключ авторизации не найден."_q);
	case Error::ReadFailed: return Phrase(
		u"Could not read the selected source."_q,
		u"Не удалось прочитать выбранный источник."_q);
	case Error::Corrupt: return Phrase(
		u"The source is incomplete or damaged."_q,
		u"Источник повреждён или неполон."_q);
	case Error::UnsupportedFormat: return Phrase(
		u"Unsupported source format, version or environment."_q,
		u"Формат, версия или окружение источника не поддерживаются."_q);
	}
	Unexpected("Session import error.");
}

std::vector<Candidate> Existing(not_null<Main::Domain*> domain) {
	auto result = std::vector<Candidate>();
	for (const auto &entry : domain->accounts()) {
		auto account = ReadAuthorization(entry.account->serializeMtpAuthorization());
		account.test = entry.account->mtp().isTestMode();
		result.push_back(std::move(account));
	}
	return result;
}

class ImportRow final : public Ui::VerticalLayout {
public:
	explicit ImportRow(QWidget *parent);
	void setCheckbox(not_null<Ui::Checkbox*> checkbox);

protected:
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	Ui::Checkbox *_checkbox = nullptr;

};

class Import final : public QObject {
public:
	Import(not_null<Ui::GenericBox*> box, not_null<Main::Domain*> domain);
	~Import();
	void read(QStringList paths, bool sessions, QByteArray passcode = {});

private:
	struct Row {
		Candidate candidate;
		Ui::Checkbox *check = nullptr;
		Ui::FlatLabel *status = nullptr;
		bool added = false;
		bool terminal = false;
	};
	void display(Result result);
	void updateButton();
	void add();
	void next();
	void verified(MTPUser self);
	void fail(QString message, bool terminal = false);
	void clearConnection(bool release = true);
	void finish();

	const not_null<Ui::GenericBox*> _box;
	const not_null<Main::Domain*> _domain;
	QStringList _paths;
	bool _sessions = false;
	bool _busy = false;
	bool _closed = false;
	std::vector<Row> _rows;
	QPointer<Ui::RoundButton> _add;
	Ui::FlatLabel *_summary = nullptr;
	std::unique_ptr<MTP::Instance> _connection;
	std::unique_ptr<MTP::Sender> _sender;
	std::unique_ptr<Reservation> _reservation;
	QTimer _timeout;
	base::weak_ptr<Main::Account> _first;
	size_t _index = 0;
	uint64 _generation = 0;

};

ImportRow::ImportRow(QWidget *parent)
: VerticalLayout(parent) {
}

void ImportRow::setCheckbox(not_null<Ui::Checkbox*> checkbox) {
	_checkbox = checkbox;
	setCursor(checkbox->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ImportRow::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())
		&& _checkbox && _checkbox->isEnabled()) {
		_checkbox->setChecked(!_checkbox->checked());
		event->accept();
		return;
	}
	VerticalLayout::mouseReleaseEvent(event);
}

Import::~Import() {
	clearConnection();
}

Import::Import(not_null<Ui::GenericBox*> box, not_null<Main::Domain*> domain)
: QObject(box)
, _box(box)
, _domain(domain) {
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		clearConnection();
		const auto first = _first;
		const auto domain = _domain;
		crl::on_main(domain, [first, domain] {
			if (first && first->sessionExists() && !Core::Quitting()) {
				domain->activate(first.get());
			}
		});
	}, _box->lifetime());
	_timeout.setSingleShot(true);
	QObject::connect(&_timeout, &QTimer::timeout, this, [=] {
		fail(Phrase(
			u"Connection timed out. You can retry."_q,
			u"Время ожидания истекло. Можно повторить."_q));
	});
}

void Import::read(QStringList paths, bool sessions, QByteArray passcode) {
	_paths = paths;
	_sessions = sessions;
	_box->verticalLayout()->clear();
	_box->clearButtons();
	_box->addRow(object_ptr<Ui::FlatLabel>(
		_box,
		Phrase(u"Reading local files…"_q, u"Чтение локальных файлов…"_q),
		st::defaultFlatLabel));
	_box->addButton(tr::lng_cancel(), [=] { _box->closeBox(); });
	const auto done = crl::guard(this, [=](Result result) { display(std::move(result)); });
	crl::async([paths = std::move(paths), sessions, passcode = std::move(passcode), done]() mutable {
		auto result = sessions ? ReadSessions(paths) : ReadTdata(paths.front(), passcode);
		passcode.fill('\0');
		crl::on_main([done, result = std::move(result)]() mutable { done(std::move(result)); });
	});
}

void Import::display(Result result) {
	if (_closed) {
		return;
	}
	_box->verticalLayout()->clear();
	_box->clearButtons();
	_rows.clear();
	if (result.error != Error::None) {
		_box->addRow(object_ptr<Ui::FlatLabel>(_box, ErrorText(result.error), st::defaultFlatLabel));
		if (result.error == Error::PasscodeRequired) {
			auto wrap = object_ptr<Ui::RpWidget>(_box);
			const auto holder = wrap.data();
			const auto input = Ui::CreateChild<Ui::PasswordInput>(
				holder,
				st::defaultInputField,
				tr::lng_passcode_ph());
			input->move(0, 0);
			input->heightValue() | rpl::on_next([=](int height) {
				holder->resize(holder->width(), height);
			}, input->lifetime());
			holder->widthValue() | rpl::on_next([=](int width) {
				input->resize(width, input->height());
			}, input->lifetime());
			_box->addRow(std::move(wrap));
			_box->addButton(tr::lng_continue(), [=] {
				read(_paths, _sessions, input->getLastText().toUtf8());
			});
			input->setFocus();
		}
		_box->addButton(tr::lng_cancel(), [=] { _box->closeBox(); });
		return;
	}
	auto known = Existing(_domain);
	for (auto &candidate : result.accounts) {
		if (candidate.error == Error::None) {
			for (const auto &other : known) {
				if (SameAuthorization(candidate, other)) {
					candidate.error = Error::Duplicate;
					break;
				}
			}
		}
		known.push_back(candidate);
		const auto name = candidate.name.isEmpty()
			? Phrase(u"Account "_q, u"Аккаунт "_q) + QString::number(_rows.size() + 1)
			: candidate.name;
		const auto group = _box->addRow(object_ptr<ImportRow>(_box));
		const auto check = group->add(object_ptr<Ui::Checkbox>(
			group,
			name,
			candidate.error == Error::None,
			st::defaultBoxCheckbox));
		check->setFocusPolicy(Qt::StrongFocus);
		check->setDisabled(candidate.error != Error::None);
		group->setCheckbox(check);
		const auto metadata = (!candidate.username.isEmpty() ? u"@"_q + candidate.username + u" · "_q : QString())
			+ (candidate.userId ? u"ID "_q + QString::number(candidate.userId)
				: Phrase(
					u"Username and ID not found in the file"_q,
					u"Username и ID не найдены в файле"_q));
		const auto label = group->add(object_ptr<Ui::FlatLabel>(group, metadata, st::defaultFlatLabel));
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->setTextColorOverride(st::windowSubTextFg->c);
		const auto status = group->add(object_ptr<Ui::FlatLabel>(group, ErrorText(candidate.error), st::defaultFlatLabel));
		status->setAttribute(Qt::WA_TransparentForMouseEvents);
		status->setTextColorOverride(st::windowSubTextFg->c);
		_rows.push_back({ std::move(candidate), check, status });
		check->checkedChanges() | rpl::on_next([=] { updateButton(); }, check->lifetime());
	}
	Ui::AddSkip(_box->verticalLayout());
	_box->addRow(object_ptr<Ui::FlatLabel>(
		_box,
		Phrase(
			u"These files continue the same Telegram session. Before Add, fully close the app or script using them. Do not start it again while this session is running in SeeGram.\n\nIf both clients connect at once, Telegram may disconnect this session in both. You will need to sign in again by QR code or phone number."_q,
			u"Эти файлы продолжат тот же сеанс Telegram. Перед «Добавить» полностью закройте приложение или скрипт, использующий их. Не запускайте его снова, пока этот сеанс работает в SeeGram.\n\nЕсли оба клиента подключатся одновременно, Telegram может отключить сеанс в обоих. Тогда придётся войти заново по QR-коду или номеру телефона."_q),
		st::defaultFlatLabel));
	_summary = _box->addRow(object_ptr<Ui::FlatLabel>(_box, QString(), st::defaultFlatLabel));
	_add = _box->addButton(rpl::single(Phrase(u"Add"_q, u"Добавить"_q)), [=] { add(); });
	_box->addButton(tr::lng_cancel(), [=] {
		clearConnection();
		_box->closeBox();
	});
	updateButton();
}

void Import::updateButton() {
	if (_add) {
		_add->setDisabled(_busy || !ranges::any_of(_rows, [](const Row &row) {
			return row.check->checked() && !row.added && !row.terminal
				&& row.candidate.error == Error::None;
		}));
	}
}

void Import::add() {
	if (_busy || _closed) {
		return;
	}
	const auto count = ranges::count_if(_rows, [](const Row &row) {
		return row.check->checked() && !row.added && !row.terminal;
	});
	if (count > _domain->maxAccounts() - _domain->accountsAuthedCount()) {
		_summary->setText(Phrase(
			u"Select fewer accounts: the account limit would be exceeded."_q,
			u"Выберите меньше аккаунтов: будет превышен лимит."_q));
		return;
	}
	_busy = true;
	_index = 0;
	for (auto &row : _rows) {
		row.check->setDisabled(true);
	}
	updateButton();
	next();
}

void Import::next() {
	clearConnection();
	while (_index < _rows.size()) {
		auto &row = _rows[_index];
		if (row.check->checked() && !row.added && !row.terminal && row.candidate.error == Error::None) {
			break;
		}
		++_index;
	}
	if (_index == _rows.size()) {
		finish();
		return;
	}
	const auto &candidate = _rows[_index].candidate;
	for (const auto &other : Existing(_domain)) {
		if (SameAuthorization(candidate, other)) {
			fail(ErrorText(Error::Duplicate), true);
			return;
		}
	}
	_reservation = Reservation::Acquire(candidate);
	if (!_reservation) {
		fail(Phrase(
			u"This session is being imported in another window."_q,
			u"Этот сеанс уже добавляется в другом окне."_q));
		return;
	}
	_rows[_index].status->setText(Phrase(
		u"Checking authorization…"_q,
		u"Проверка авторизации…"_q));
	auto fields = MTP::Instance::Fields();
	fields.config = std::make_unique<MTP::Config>(candidate.test ? MTP::Environment::Test : MTP::Environment::Production);
	fields.mainDcId = candidate.mainDc;
	fields.deviceModel = u"SeeGram Personal"_q;
	for (const auto &key : candidate.keys) {
		auto bytes = MTP::AuthKey::Data();
		memcpy(bytes.data(), key.bytes.constData(), bytes.size());
		fields.keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, key.dc, bytes));
	}
	_connection = std::make_unique<MTP::Instance>(MTP::Instance::Mode::Normal, std::move(fields));
	_sender = std::make_unique<MTP::Sender>(_connection.get());
	_timeout.start(30000);
	const auto generation = _generation;
	_sender->request(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())))
	.done(crl::guard(this, [=](const MTPVector<MTPUser> &users) {
		crl::on_main(this, [=] {
			if (_closed || generation != _generation) {
				return;
			}
			if (users.v.isEmpty()) {
				fail(Phrase(
					u"Authorization was not accepted."_q,
					u"Авторизация не подтверждена."_q), true);
			} else {
				verified(users.v.front());
			}
		});
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		const auto type = error.type();
		const auto ignored = MTP::IgnoreError(error);
		crl::on_main(this, [=] {
			if (_closed || generation != _generation) {
				return;
			}
			if (type == u"AUTH_KEY_DUPLICATED") {
				fail(Phrase(
					u"Telegram revoked the shared session after simultaneous use. Sign in again by QR or phone."_q,
					u"Telegram отозвал общий сеанс после одновременного использования. Войдите заново по QR или телефону."_q), true);
			} else if (type == u"SESSION_PASSWORD_NEEDED") {
				fail(Phrase(
					u"Telegram requires a password. Use the standard sign-in flow."_q,
					u"Telegram требует пароль. Используйте штатный вход."_q), true);
			} else if (type == u"AUTH_KEY_UNREGISTERED" || type == u"SESSION_REVOKED" || type == u"SESSION_EXPIRED") {
				fail(Phrase(
					u"Session expired or revoked. Choose another file or sign in normally."_q,
					u"Сеанс истёк или отозван. Выберите другой файл или обычный вход."_q), true);
			} else if (ignored) {
				fail(QString());
			} else {
				fail(Phrase(
					u"Could not verify the session. You can retry."_q,
					u"Не удалось проверить сеанс. Можно повторить."_q));
			}
		});
	})).handleAllErrors().send();
}

void Import::clearConnection(bool release) {
	++_generation;
	_timeout.stop();
	_sender.reset();
	_connection.reset();
	if (release) {
		_reservation.reset();
	}
}

void Import::verified(MTPUser self) {
	if (self.type() != mtpc_user || !self.c_user().is_self() || self.c_user().is_bot()) {
		fail(ErrorText(Error::Bot), true);
		return;
	}
	auto &row = _rows[_index];
	row.candidate.userId = self.c_user().vid().v;
	for (const auto &other : Existing(_domain)) {
		if (SameAuthorization(row.candidate, other)) {
			fail(ErrorText(Error::Duplicate), true);
			return;
		}
	}
	clearConnection(false);
	const auto added = _domain->importAccount(
		SerializeAuthorization(row.candidate),
		self,
		row.candidate.test ? MTP::Environment::Test : MTP::Environment::Production);
	_reservation.reset();
	if (!added) {
		fail(Phrase(
			u"Could not save this account. Existing accounts are unchanged."_q,
			u"Не удалось сохранить аккаунт. Существующие аккаунты сохранены."_q));
		return;
	}
	if (!_first) {
		_first = base::make_weak(added);
	}
	row.added = true;
	row.status->setText(Phrase(u"Added"_q, u"Добавлен"_q));
	++_index;
	next();
}

void Import::fail(QString message, bool terminal) {
	clearConnection();
	_rows[_index].status->setText(std::move(message));
	_rows[_index].terminal = terminal;
	++_index;
	next();
}

void Import::finish() {
	_busy = false;
	for (auto &row : _rows) {
		row.check->setDisabled(row.added || row.terminal || row.candidate.error != Error::None);
	}
	_summary->setText(Phrase(
		u"Finished. Added: "_q,
		u"Готово. Добавлено: "_q)
		+ QString::number(ranges::count_if(_rows, &Row::added)));
	updateButton();
	if (_first) {
		_box->addButton(tr::lng_box_done(), [=] { _box->closeBox(); });
	}
}

void Open(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Domain*> domain,
		std::shared_ptr<Ui::Show> show,
		QStringList paths,
		bool sessions) {
	if (paths.isEmpty()) {
		return;
	}
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Phrase(
			u"Import accounts"_q,
			u"Добавление аккаунтов"_q)));
		box->setWidth(st::boxWidth);
		const auto importer = new Import(box, domain);
		importer->read(paths, sessions);
	}));
}

} // namespace

Ui::VerticalLayout *CreateLinks(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Domain*> domain,
		std::shared_ptr<Ui::Show> show) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(parent);
	for (const auto sessions : { false, true }) {
		const auto button = result->add(object_ptr<Ui::LinkButton>(
			result,
			sessions ? Phrase(
				u"Log in with .session"_q,
				u"Войти через .session"_q)
				: Phrase(u"Log in with tdata"_q, u"Войти через tdata"_q),
			st::introLink),
			{ 0, 0, 0, st::normalFont->height / 2 },
			style::al_center);
		button->setClickedCallback([=] {
			const auto done = crl::guard(parent, [=](QStringList paths) {
				Open(parent, domain, show, std::move(paths), sessions);
			});
			if (sessions) {
				FileDialog::GetOpenPaths(parent.get(), u".session"_q, u"Telegram sessions (*.session)"_q,
					[done](FileDialog::OpenResult &&selection) { done(std::move(selection.paths)); });
			} else {
				ChooseTdata([done](QString path) { done(path.isEmpty() ? QStringList() : QStringList{ path }); });
			}
		});
	}
	result->resizeToWidth(st::introStepWidth);
	return result;
}

} // namespace Fork::Personal::SessionImport
