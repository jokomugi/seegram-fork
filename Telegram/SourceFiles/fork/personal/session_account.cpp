#include "main/main_domain.h"

#include "fork/account_limits.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/details/storage_settings_scheme.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

namespace Main {

Account *Domain::importAccount(
		const QByteArray &authorization,
		const MTPUser &self,
		MTP::Environment environment) {
	if (accountsAuthedCount() >= maxAccounts() || self.type() != mtpc_user
		|| !self.c_user().is_self() || self.c_user().is_bot()) {
		return nullptr;
	}
	const auto id = UserId(self.c_user().vid().v);
	for (const auto &entry : _accounts) {
		if (entry.account->sessionExists()
			&& entry.account->session().userId() == id
			&& entry.account->mtp().environment() == environment) {
			return nullptr;
		}
	}
	auto index = 0;
	auto added = std::unique_ptr<Account>();
	while (index < Fork::Accounts::kNoLimit) {
		if (!_local->accountIndexReserved(index)
			&& !ranges::contains(_accounts, index, &AccountWithIndex::index)) {
			added = std::make_unique<Account>(this, _dataName, index);
			if (added->local().importStorageVacant()) {
				break;
			}
			added.reset();
		}
		++index;
	}
	if (!added) {
		return nullptr;
	}
	added->setMtpAuthorization(authorization);
	const auto raw = added.get();
	_accounts.push_back({ .index = index, .account = std::move(added) });
	if (!_local->commitImportedAccount(
			raw,
			index,
			authorization,
			MTP::Config(environment).serialize())) {
		_accounts.pop_back();
		return nullptr;
	}
	raw->setSessionUserId(UserId());
	raw->start(std::make_unique<MTP::Config>(environment));
	raw->createSession(self);
	raw->local().writeSelf();
	watchSession(raw);
	_accountsChanges.fire({});
	return raw;
}

} // namespace Main

namespace Storage {

using namespace details;

namespace {

QString ImportRoot(const QString &accountPath) {
	return QDir::cleanPath(accountPath + u".."_q) + QChar('/');
}

} // namespace

bool Account::importStorageVacant() const {
	if (QFileInfo::exists(_basePath) || QFileInfo::exists(_databasePath)) {
		return false;
	}
	for (const auto suffix : { 's', '0', '1' }) {
		if (QFileInfo::exists(ImportRoot(_basePath) + ToFilePart(_dataNameKey) + QChar(suffix))) {
			return false;
		}
	}
	return true;
}

bool Account::writeImportedAuthorization(
		const QByteArray &serialized,
		const QByteArray &config) {
	Expects(_localKey != nullptr);
	FileWriteDescriptor file(ToFilePart(_dataNameKey), ImportRoot(_basePath), true);
	EncryptedDescriptor data(sizeof(quint32) + Serialize::bytearraySize(serialized));
	data.stream << quint32(dbiMtpAuthorization) << serialized;
	file.writeEncrypted(data, _localKey);
	if (!file.finish()) {
		return false;
	}
	FileWriteDescriptor settings(u"config"_q, _basePath, true);
	EncryptedDescriptor configuration(Serialize::bytearraySize(config));
	configuration.stream << config;
	settings.writeEncrypted(configuration, _localKey);
	return settings.finish();
}

void Account::discardImportedAuthorization() {
	for (const auto suffix : { 's', '0', '1' }) {
		QFile::remove(ImportRoot(_basePath) + ToFilePart(_dataNameKey) + QChar(suffix));
		QFile::remove(_basePath + u"config"_q + QChar(suffix));
	}
	QDir().rmdir(_basePath);
}

bool Domain::commitImportedAccount(
		not_null<Main::Account*> account,
		int index,
		const QByteArray &authorization,
		const QByteArray &config) {
	account->prepareToStartAdded(_localKey);
	if (!account->local().writeImportedAuthorization(authorization, config)) {
		account->local().discardImportedAuthorization();
		return false;
	}
	const auto before = _loadedAccounts;
	const auto profiles = _accountProfiles;
	_loadedAccounts.emplace(index);
	if (restrictedProfile()) {
		for (auto &profile : _accountProfiles) {
			if (profile.id == _activeProfile) {
				profile.accounts.emplace(index);
			}
		}
	}
	if (!writeKeyData(*_keyData, true)) {
		_loadedAccounts = before;
		_accountProfiles = profiles;
		account->local().discardImportedAuthorization();
		return false;
	}
	return true;
}

} // namespace Storage
