#include "fork/personal/session_source.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <sqlite3.h>
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace {

void Require(bool value) {
	if (!value) {
		std::cerr << "Session source behavior failed\n";
		std::exit(1);
	}
}

} // namespace

int main(int argc, char **argv) {
	auto app = QCoreApplication(argc, argv);
	if (argc > 1) {
		for (auto i = 1; i != argc; ++i) {
			const auto path = QString::fromLocal8Bit(argv[i]);
			const auto result = path.endsWith(QString::fromLatin1(".session"))
				? Fork::Personal::SessionImport::ReadSessions({ path })
				: Fork::Personal::SessionImport::ReadTdata(path);
			const auto valid = std::count_if(result.accounts.begin(), result.accounts.end(), [](const auto &candidate) {
				return candidate.error == Fork::Personal::SessionImport::Error::None;
			});
			std::cout << "Source " << i << ": result=" << int(result.error)
				<< " accounts=" << result.accounts.size() << " valid=" << valid << "\n";
		}
		return 0;
	}
	auto temporary = QTemporaryDir();
	Require(temporary.isValid());
	const auto path = temporary.filePath(QString::fromLatin1("test.session"));
	auto database = static_cast<sqlite3*>(nullptr);
	Require(sqlite3_open(path.toUtf8().constData(), &database) == SQLITE_OK);
	Require(sqlite3_exec(database,
		"CREATE TABLE version (version INTEGER); INSERT INTO version VALUES (7);"
		"CREATE TABLE sessions (dc_id INTEGER, server_address TEXT, port INTEGER, auth_key BLOB, takeout_id INTEGER);"
		"INSERT INTO sessions VALUES (2,'149.154.167.51',443,zeroblob(256),NULL);",
		nullptr,
		nullptr,
		nullptr) == SQLITE_OK);
	sqlite3_close(database);
	const auto before = [&] {
		auto file = QFile(path);
		Require(file.open(QIODevice::ReadOnly));
		return file.readAll();
	}();
	const auto result = Fork::Personal::SessionImport::ReadSessions({ path });
	Require(result.error == Fork::Personal::SessionImport::Error::None);
	Require(result.accounts.size() == 1);
	const auto &account = result.accounts.front();
	Require(account.error == Fork::Personal::SessionImport::Error::None);
	Require(account.userId == 0 && account.name.isEmpty());
	Require(account.mainDc == 2 && !account.test);
	Require(account.keys.size() == 1 && account.keys.front().bytes.size() == 256);
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly) && file.readAll() == before);
	using namespace Fork::Personal::SessionImport;
	auto restored = ReadAuthorization(SerializeAuthorization(account));
	Require(restored.error == Error::None && restored.keys.size() == 1);
	{
		auto lease = Reservation::Acquire(account);
		Require(bool(lease));
		Require(!Reservation::Acquire(account));
		auto other = account;
		other.test = true;
		Require(!Reservation::Acquire(other));
		other.keys.front().bytes.fill('z');
		Require(bool(Reservation::Acquire(other)));
	}
	Require(bool(Reservation::Acquire(account)));
	auto truncated = SerializeAuthorization(account);
	truncated.chop(1);
	Require(ReadAuthorization(truncated).error == Error::Corrupt);
	auto otherEnvironment = restored;
	otherEnvironment.test = true;
	Require(SameAuthorization(account, otherEnvironment));
	otherEnvironment.keys.front().bytes.fill('x');
	otherEnvironment.userId = 123;
	auto sameId = otherEnvironment;
	sameId.test = false;
	sameId.keys.front().bytes.fill('y');
	Require(!SameAuthorization(otherEnvironment, sameId));
	Require(SameAuthorization(sameId, sameId));
	const auto wal = path + QString::fromLatin1("-wal");
	{
		auto output = QFile(wal);
		Require(output.open(QIODevice::WriteOnly));
		Require(output.write("live") == 4);
	}
	Require(ReadSessions({ path }).accounts.front().error == Error::LiveDatabase);
	Require(QFile::remove(wal));
	Require(sqlite3_open(path.toUtf8().constData(), &database) == SQLITE_OK);
	Require(sqlite3_exec(database, "UPDATE sessions SET server_address='2001:67c:4e8:f002::a'", nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(database);
	Require(ReadSessions({ path }).accounts.front().error == Error::None);
	const auto pyroPath = temporary.filePath(QString::fromLatin1("pyro.session"));
	Require(sqlite3_open(pyroPath.toUtf8().constData(), &database) == SQLITE_OK);
	Require(sqlite3_exec(database,
		"CREATE TABLE version (number INTEGER); INSERT INTO version VALUES (2);"
		"CREATE TABLE sessions (dc_id INTEGER,test_mode INTEGER,user_id INTEGER,auth_key BLOB,is_bot INTEGER);"
		"INSERT INTO sessions VALUES (2,0,123,zeroblob(256),0);"
		"CREATE TABLE peers (id INTEGER,username TEXT); INSERT INTO peers VALUES (123,'fixture');",
		nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(database);
	const auto mixed = ReadSessions({ path, pyroPath });
	Require(mixed.accounts.size() == 2 && mixed.accounts.back().error == Error::None);
	Require(mixed.accounts.back().userId == 123);
	Require(mixed.accounts.back().username == QString::fromLatin1("fixture"));
	Require(sqlite3_open(pyroPath.toUtf8().constData(), &database) == SQLITE_OK);
	Require(sqlite3_exec(database, "UPDATE sessions SET is_bot=1", nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(database);
	Require(ReadSessions({ pyroPath }).accounts.front().error == Error::Bot);
	const auto invalidZip = temporary.filePath(QString::fromLatin1("invalid.zip"));
	{
		auto output = QFile(invalidZip);
		Require(output.open(QIODevice::WriteOnly));
		Require(output.write("PK") == 2);
	}
	Require(ReadTdata(invalidZip).error == Error::UnsafeArchive);
	const auto fixtures = QString::fromUtf8(FIXTURES_PATH) + QLatin1Char('/');
	const auto encrypted = fixtures + QString::fromLatin1("encrypted-tdata.zip");
	Require(ReadTdata(encrypted).error == Error::PasscodeRequired);
	Require(ReadTdata(encrypted, QByteArray("wrong")).error == Error::PasscodeRequired);
	const auto full = ReadTdata(encrypted, QByteArray("fixture"));
	Require(full.error == Error::None && full.accounts.size() == 2);
	Require(full.accounts.front().error == Error::None && full.accounts.front().userId == 123);
	Require(full.accounts.front().name == QString::fromLatin1("Fixture 1"));
	const auto restricted = ReadTdata(encrypted, QByteArray("profile"));
	Require(restricted.error == Error::None && restricted.accounts.size() == 1);
	Require(restricted.accounts.front().userId == 456 && restricted.accounts.front().error == Error::None);
	const auto rotated = ReadTdata(fixtures + QString::fromLatin1("rotated-tdata.zip"), QByteArray("fixture"));
	Require(rotated.error == Error::None && rotated.accounts.size() == 1);
	Require(rotated.accounts.front().userId == 456 && rotated.accounts.front().error == Error::None);
	Require(ReadTdata(fixtures + QString::fromLatin1("traversal.zip")).error == Error::UnsafeArchive);
	Require(ReadTdata(fixtures + QString::fromLatin1("multiple-roots.zip")).error == Error::AmbiguousRoot);
	std::cout << "Session source checks passed: read-only, mixed formats, WAL, bots, duplicates, truncation, ZIP bounds\n";
}
