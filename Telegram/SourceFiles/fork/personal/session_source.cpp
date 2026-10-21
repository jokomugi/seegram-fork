#include "fork/personal/session_source.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtCore/QtEndian>
#include <QtNetwork/QHostAddress>
#include <openssl/aes.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <sqlite3.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <set>

namespace Fork::Personal::SessionImport {
namespace {

constexpr auto kMaxFile = 32 * 1024 * 1024;
constexpr auto kMaxArchive = 256 * 1024 * 1024;
constexpr auto kMaxEntries = 4096;
constexpr auto kMaxAccounts = 32;

std::mutex ReservationMutex;
std::set<const Reservation*> Reservations;

class Reader {
public:
	explicit Reader(QByteArray bytes);
	quint32 integer();
	quint64 wide();
	QByteArray raw(int size);
	QByteArray blob();
	QString string();
	bool ok() const;
	bool end() const;

private:
	QByteArray _bytes;
	qsizetype _position = 0;
	bool _ok = true;

};

Reader::Reader(QByteArray bytes)
: _bytes(std::move(bytes)) {
}

quint32 Reader::integer() {
	const auto bytes = raw(4);
	return _ok ? qFromBigEndian<quint32>(bytes.constData()) : 0;
}

quint64 Reader::wide() {
	const auto bytes = raw(8);
	return _ok ? qFromBigEndian<quint64>(bytes.constData()) : 0;
}

QByteArray Reader::raw(int size) {
	if (!_ok || size < 0 || size > _bytes.size() - _position) {
		_ok = false;
		return {};
	}
	const auto result = _bytes.mid(_position, size);
	_position += size;
	return result;
}

QByteArray Reader::blob() {
	const auto size = integer();
	return size == 0xffffffffU ? QByteArray() : raw(size <= kMaxFile ? int(size) : -1);
}

QString Reader::string() {
	const auto value = blob();
	if ((value.size() % 2) || value.size() > 8192) {
		_ok = false;
		return {};
	}
	auto result = QString();
	for (auto i = 0; i < value.size(); i += 2) {
		result.append(QChar(qFromBigEndian<quint16>(value.constData() + i)));
	}
	return result;
}

bool Reader::ok() const {
	return _ok;
}

bool Reader::end() const {
	return _ok && _position == _bytes.size();
}

using Files = std::function<QByteArray(const QString&)>;
using Modified = std::function<qint64(const QString&)>;

QByteArray ReadFile(const QString &path, int limit = kMaxFile) {
	const auto info = QFileInfo(path);
	if (!info.isFile() || info.isSymLink() || info.size() > limit) {
		return {};
	}
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.read(limit + 1) : QByteArray();
}

QByteArray Hash(const QByteArray &value, QCryptographicHash::Algorithm algorithm) {
	return QCryptographicHash::hash(value, algorithm);
}

QByteArray LocalKey(const QByteArray &passcode, const QByteArray &salt) {
	const auto hash = Hash(salt + passcode + salt, QCryptographicHash::Sha512);
	auto result = QByteArray(256, Qt::Uninitialized);
	const auto ok = PKCS5_PBKDF2_HMAC(
		hash.constData(),
		int(hash.size()),
		reinterpret_cast<const unsigned char*>(salt.constData()),
		int(salt.size()),
		passcode.isEmpty() ? 1 : 100000,
		EVP_sha512(),
		int(result.size()),
		reinterpret_cast<unsigned char*>(result.data()));
	return ok == 1 ? result : QByteArray();
}

QByteArray PasscodeKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		quint32 kind,
		quint32 memory,
		quint32 iterations,
		quint32 parallel) {
	if (salt.size() < 8 || salt.size() > 256 || passcode.isEmpty()) {
		return {};
	}
	auto result = QByteArray(256, Qt::Uninitialized);
	const auto out = reinterpret_cast<unsigned char*>(result.data());
	if (kind == 2) {
		if (memory < 2 || memory > 262144 || (memory & (memory - 1))
			|| !iterations || iterations > 16 || !parallel || parallel > 4) {
			return {};
		}
		return EVP_PBE_scrypt(
			passcode.constData(),
			passcode.size(),
			reinterpret_cast<const unsigned char*>(salt.constData()),
			salt.size(),
			memory,
			iterations,
			parallel,
			256 * 1024 * 1024,
			out,
			result.size()) == 1 ? result : QByteArray();
	}
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
	if (kind != 1 || !parallel || parallel > 16 || memory < 8 * parallel
		|| memory > 262144 || !iterations || iterations > 64) {
		return {};
	}
	const auto algorithm = std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)>(
		EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr),
		EVP_KDF_free);
	const auto context = std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)>(
		algorithm ? EVP_KDF_CTX_new(algorithm.get()) : nullptr,
		EVP_KDF_CTX_free);
	auto threads = quint32(1);
	auto params = std::array{
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<char*>(passcode.constData()), passcode.size()),
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(salt.constData()), salt.size()),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memory),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &parallel),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
		OSSL_PARAM_construct_end(),
	};
	return context && EVP_KDF_derive(context.get(), out, result.size(), params.data()) == 1
		? result : QByteArray();
#else
	return {};
#endif // OPENSSL_VERSION_NUMBER >= 0x30200000L
}

QByteArray Decrypt(const QByteArray &encrypted, const QByteArray &key) {
	if (key.size() != 256 || encrypted.size() <= 16 || (encrypted.size() % 16)) {
		return {};
	}
	const auto message = encrypted.first(16);
	const auto a = Hash(message + key.mid(8, 32), QCryptographicHash::Sha1);
	const auto b = Hash(key.mid(40, 16) + message + key.mid(56, 16), QCryptographicHash::Sha1);
	const auto c = Hash(key.mid(72, 32) + message, QCryptographicHash::Sha1);
	const auto d = Hash(message + key.mid(104, 32), QCryptographicHash::Sha1);
	const auto aesKey = a.first(8) + b.mid(8, 12) + c.mid(4, 12);
	auto iv = a.mid(8, 12) + b.first(8) + c.mid(16, 4) + d.first(8);
	auto cipher = AES_KEY();
	if (AES_set_decrypt_key(reinterpret_cast<const unsigned char*>(aesKey.constData()), 256, &cipher) != 0) {
		return {};
	}
	auto result = QByteArray(encrypted.size() - 16, Qt::Uninitialized);
	AES_ige_encrypt(
		reinterpret_cast<const unsigned char*>(encrypted.constData() + 16),
		reinterpret_cast<unsigned char*>(result.data()),
		result.size(),
		&cipher,
		reinterpret_cast<unsigned char*>(iv.data()),
		AES_DECRYPT);
	if (CRYPTO_memcmp(Hash(result, QCryptographicHash::Sha1).constData(), message.constData(), 16) != 0) {
		return {};
	}
	const auto length = qFromLittleEndian<quint32>(result.constData());
	return length >= 4 && length <= result.size() && length > result.size() - 16
		? result.mid(4, length - 4) : QByteArray();
}

QByteArray Envelope(const Files &files, const QString &name, const Modified &modified) {
	auto suffixes = std::array{ 's', '1', '0' };
	if (modified && modified(name + QLatin1Char('0')) > modified(name + QLatin1Char('1'))) {
		std::swap(suffixes[1], suffixes[2]);
	}
	for (const auto suffix : suffixes) {
		const auto bytes = files(name + QLatin1Char(suffix));
		if (bytes.size() < 24 || bytes.size() > kMaxFile || !bytes.startsWith("TDF$")) {
			continue;
		}
		const auto payload = bytes.mid(8, bytes.size() - 24);
		auto length = QByteArray(4, Qt::Uninitialized);
		qToLittleEndian<quint32>(payload.size(), length.data());
		const auto digest = Hash(payload + length + bytes.mid(4, 4) + bytes.first(4), QCryptographicHash::Md5);
		if (digest == bytes.last(16)) {
			return payload;
		}
	}
	return {};
}

QString DataName(int index) {
	const auto name = QByteArray("data") + (index ? QByteArray("#") + QByteArray::number(index + 1) : QByteArray());
	const auto md5 = Hash(name, QCryptographicHash::Md5);
	auto value = qFromLittleEndian<quint64>(md5.constData());
	auto result = QString();
	for (auto i = 0; i != 16; ++i) {
		result.append(QLatin1Char("0123456789ABCDEF"[value & 15]));
		value >>= 4;
	}
	return result;
}

bool KnownDc(int dc, const QString &address, int port, bool &test) {
	if (dc < 1 || dc > 5 || (port != 443 && port != 80 && port != 5222)) {
		return false;
	}
	const auto ipv6 = std::array{
		"2001:b28:f23d:f001::a", "2001:67c:4e8:f002::a", "2001:b28:f23d:f003::a",
		"2001:67c:4e8:f004::a", "2001:b28:f23f:f005::a",
	};
	const auto testIpv6 = std::array{
		"2001:b28:f23d:f001::e", "2001:67c:4e8:f002::e", "2001:b28:f23d:f003::e",
	};
	const auto host = QHostAddress(address);
	if (host.protocol() == QAbstractSocket::IPv6Protocol) {
		if (host == QHostAddress(QString::fromLatin1(ipv6[dc - 1]))) {
			test = false;
			return true;
		}
		if (dc <= 3 && host == QHostAddress(QString::fromLatin1(testIpv6[dc - 1]))) {
			test = true;
			return true;
		}
		return false;
	}
	const auto production = std::array{ "149.154.175.50", "149.154.167.51", "149.154.175.100", "149.154.167.91", "149.154.171.5" };
	const auto testing = std::array{ "149.154.175.10", "149.154.167.40", "149.154.175.117" };
	if (address == QLatin1String(production[dc - 1])
		|| (dc == 1 && address == QLatin1String("149.154.175.53"))
		|| (dc == 2 && (address == QLatin1String("149.154.167.50") || address == QLatin1String("95.161.76.100")))
		|| (dc == 4 && address == QLatin1String("149.154.167.92"))) {
		test = false;
		return true;
	}
	if (dc <= 3 && address == QLatin1String(testing[dc - 1])) {
		test = true;
		return true;
	}
	return false;
}

struct StatementDeleter {
	void operator()(sqlite3_stmt *statement) const;
};

void StatementDeleter::operator()(sqlite3_stmt *statement) const {
	sqlite3_finalize(statement);
}

using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

Statement Query(sqlite3 *database, const char *sql) {
	auto statement = static_cast<sqlite3_stmt*>(nullptr);
	if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
		return {};
	}
	return Statement(statement);
}

QString Text(sqlite3_stmt *statement, int column) {
	const auto length = sqlite3_column_bytes(statement, column);
	const auto value = sqlite3_column_text(statement, column);
	return value && length <= 4096
		? QString::fromUtf8(reinterpret_cast<const char*>(value), length) : QString();
}

Candidate Session(const QString &path) {
	auto result = Candidate();
	const auto fail = [&](Error error) {
		result.keys.clear();
		result.error = error;
		return result;
	};
	const auto header = ReadFile(path);
	if (header.size() < 100 || !header.startsWith(QByteArray("SQLite format 3\0", 16))) {
		return fail(Error::UnsupportedFormat);
	}
	for (const auto suffix : { "-wal", "-journal" }) {
		if (QFileInfo(path + QLatin1String(suffix)).size() > 0) {
			return fail(Error::LiveDatabase);
		}
	}
	auto raw = static_cast<sqlite3*>(nullptr);
	const auto uri = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toEncoded()
		+ "?mode=ro&immutable=1";
	const auto opened = sqlite3_open_v2(uri.constData(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX, nullptr);
	const auto database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(raw, sqlite3_close);
	if (opened != SQLITE_OK) {
		return fail(Error::Corrupt);
	}
#ifdef SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION
	sqlite3_db_config(raw, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr);
#endif // SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION
	sqlite3_db_config(raw, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
	sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, kMaxFile);
	sqlite3_limit(raw, SQLITE_LIMIT_SQL_LENGTH, 4096);
	sqlite3_limit(raw, SQLITE_LIMIT_COLUMN, 64);
	auto budget = 10000;
	sqlite3_progress_handler(raw, 1000, [](void *context) {
		return int(--*static_cast<int*>(context) <= 0);
	}, &budget);
	if (sqlite3_exec(raw, "PRAGMA query_only=ON; PRAGMA trusted_schema=OFF", nullptr, nullptr, nullptr) != SQLITE_OK) {
		return fail(Error::Corrupt);
	}
	const auto check = Query(raw, "PRAGMA quick_check(1)");
	if (!check || sqlite3_step(check.get()) != SQLITE_ROW || Text(check.get(), 0) != QLatin1String("ok")) {
		return fail(Error::Corrupt);
	}
	const auto telethon = Query(raw, "SELECT version FROM version");
	const auto pyrogram = telethon ? Statement() : Query(raw, "SELECT number FROM version");
	const auto version = telethon ? telethon.get() : pyrogram.get();
	if (!version || sqlite3_step(version) != SQLITE_ROW) {
		return fail(Error::UnsupportedFormat);
	}
	const auto supported = sqlite3_column_int(version, 0) == (telethon ? 7 : 2);
	if (!supported || sqlite3_step(version) != SQLITE_DONE) {
		return fail(Error::UnsupportedFormat);
	}
	const auto session = Query(raw, telethon
		? "SELECT dc_id,server_address,port,auth_key FROM sessions"
		: "SELECT dc_id,test_mode,user_id,auth_key,is_bot FROM sessions");
	if (!session || sqlite3_step(session.get()) != SQLITE_ROW) {
		return fail(Error::MissingKey);
	}
	result.mainDc = sqlite3_column_int(session.get(), 0);
	if (result.mainDc < 1 || result.mainDc > 5) {
		return fail(Error::UnsupportedFormat);
	}
	if (telethon) {
		if (!KnownDc(result.mainDc, Text(session.get(), 1), sqlite3_column_int(session.get(), 2), result.test)) {
			return fail(Error::UnsupportedFormat);
		}
	} else {
		const auto test = sqlite3_column_int(session.get(), 1);
		const auto id = sqlite3_column_int64(session.get(), 2);
		if ((test != 0 && test != 1) || id <= 0 || sqlite3_column_int(session.get(), 4)) {
			return fail(sqlite3_column_int(session.get(), 4) ? Error::Bot : Error::UnsupportedFormat);
		}
		result.test = test;
		result.userId = id;
	}
	if (sqlite3_column_bytes(session.get(), 3) != 256) {
		return fail(Error::MissingKey);
	}
	result.keys.push_back({ result.mainDc, QByteArray(static_cast<const char*>(sqlite3_column_blob(session.get(), 3)), 256) });
	if (sqlite3_step(session.get()) != SQLITE_DONE) {
		return fail(Error::UnsupportedFormat);
	}
	if (telethon) {
		const auto self = Query(raw, "SELECT hash FROM entities WHERE id=0");
		if (self && sqlite3_step(self.get()) == SQLITE_ROW && sqlite3_column_int64(self.get(), 0) > 0) {
			result.userId = sqlite3_column_int64(self.get(), 0);
		}
	}
	if (result.userId) {
		const auto hint = Query(raw, telethon
			? "SELECT name,username FROM entities WHERE id=?1"
			: "SELECT NULL,username FROM peers WHERE id=?1");
		if (hint) {
			sqlite3_bind_int64(hint.get(), 1, result.userId);
			if (sqlite3_step(hint.get()) == SQLITE_ROW) {
				result.name = Text(hint.get(), 0);
				result.username = Text(hint.get(), 1);
			}
		}
	}
	return result;
}

void ReadSelfHint(const Files &files, const QString &name, const QByteArray &key, Candidate &candidate, const Modified &modified) {
	auto map = Reader(Envelope(files, name + QString::fromLatin1("/map"), modified));
	const auto salt = map.blob();
	const auto wrapped = map.blob();
	auto body = Reader(Decrypt(map.blob(), key));
	if (!map.ok() || body.integer() != 0x15) {
		return;
	}
	auto self = Reader(body.blob());
	const auto peer = self.wide();
	const auto id = peer & ~(quint64(0x80) << 48);
	if (id != candidate.userId || self.wide() != 0x77ffffffffffffffULL) {
		return;
	}
	const auto version = self.integer();
	const auto photoId = self.wide();
	const auto locationTag = self.integer();
	if (version > 3 || locationTag != 0x80000000U) {
		return;
	}
	const auto location = self.blob();
	const auto video = self.integer();
	const auto firstName = self.string();
	const auto lastName = self.string();
	const auto phone = self.string();
	const auto username = self.string();
	if (self.ok()) {
		candidate.name = (firstName + QLatin1Char(' ') + lastName).trimmed();
		candidate.username = username;
	}
}

Result Tdata(const Files &files, const QByteArray &passcode, const Modified &modified) {
	auto keyData = Reader(Envelope(files, QString::fromLatin1("key_data"), modified));
	const auto salt = keyData.blob();
	const auto wrapped = keyData.blob();
	const auto encryptedInfo = keyData.blob();
	if (!keyData.ok() || salt.size() != 32) {
		return { {}, Error::Corrupt };
	}
	auto key = Decrypt(wrapped, LocalKey(passcode, salt));
	auto profiles = QByteArray();
	if (!keyData.end()) {
		const auto bytes = keyData.blob();
		auto trailer = Reader(bytes);
		const auto magic = trailer.integer();
		if (magic == 0x4b443200) {
			const auto version = trailer.integer();
			const auto committed = trailer.integer();
			const auto count = trailer.integer();
			if (version > 2 || count > 2) {
				return { {}, Error::UnsupportedFormat };
			}
			for (auto i = 0U; i != count; ++i) {
				const auto kind = trailer.integer();
				const auto generation = trailer.integer();
				const auto memory = trailer.integer();
				const auto iterations = trailer.integer();
				const auto parallel = trailer.integer();
				const auto wrapSalt = trailer.blob();
				const auto encrypted = trailer.blob();
				if (generation == committed && !passcode.isEmpty() && trailer.ok()) {
					key = Decrypt(encrypted, PasscodeKey(passcode, wrapSalt, kind, memory, iterations, parallel));
				}
			}
			if (!trailer.end()) {
				return { {}, Error::Corrupt };
			}
			if (!keyData.end()) {
				profiles = keyData.blob();
			}
		} else {
			profiles = bytes;
		}
	}
	if (!keyData.end()) {
		return { {}, Error::Corrupt };
	}
	auto profileId = QByteArray();
	if (key.size() != 256 && !profiles.isEmpty()) {
		auto profileSlots = Reader(profiles);
		const auto count = profileSlots.integer();
		if (count > 16) {
			return { {}, Error::Corrupt };
		}
		for (auto i = 0U; i != count; ++i) {
			const auto slotSalt = profileSlots.blob();
			const auto encrypted = profileSlots.blob();
			if (slotSalt.size() != 32 || encrypted.size() > 1024) {
				return { {}, Error::Corrupt };
			}
			auto decrypted = Reader(Decrypt(encrypted, LocalKey(passcode, slotSalt)));
			const auto localKey = decrypted.raw(256);
			const auto id = decrypted.blob();
			if (decrypted.end() && id.size() == 16 && !passcode.isEmpty()) {
				key = localKey;
				profileId = id;
			}
		}
		if (!profileSlots.end()) {
			return { {}, Error::Corrupt };
		}
	}
	if (key.size() != 256) {
		return { {}, Error::PasscodeRequired };
	}
	auto info = Reader(Decrypt(encryptedInfo, key));
	const auto count = info.integer();
	if (!count || count > kMaxAccounts) {
		return { {}, Error::Corrupt };
	}
	auto indices = std::vector<int>();
	for (auto i = 0U; i != count; ++i) {
		const auto index = int(info.integer());
		if (index < 0 || index > 1023 || std::find(indices.begin(), indices.end(), index) != indices.end() || !info.ok()) {
			return { {}, Error::Corrupt };
		}
		indices.push_back(index);
	}
	if (!profileId.isEmpty()) {
		const auto active = info.integer();
		if (info.integer() != 0x53475031) {
			return { {}, Error::Corrupt };
		}
		const auto count = info.integer();
		if (count > 16) {
			return { {}, Error::Corrupt };
		}
		auto selected = QSet<int>();
		auto found = false;
		for (auto i = 0U; i != count; ++i) {
			const auto id = info.blob();
			const auto name = info.string();
			const auto accounts = info.integer();
			if (id.size() != 16 || accounts > kMaxAccounts) {
				return { {}, Error::Corrupt };
			}
			for (auto j = 0U; j != accounts; ++j) {
				const auto index = int(info.integer());
				if (id == profileId) {
					selected.insert(index);
					found = true;
				}
			}
			const auto salt = info.blob();
			const auto wrapped = info.blob();
		}
		if (!info.ok() || !found) {
			return { {}, Error::Corrupt };
		}
		std::erase_if(indices, [&](int index) { return !selected.contains(index); });
	}
	auto result = Result();
	for (const auto index : indices) {
		const auto name = DataName(index);
		auto mtp = Reader(Envelope(files, name, modified));
		auto body = Reader(Decrypt(mtp.blob(), key));
		if (body.integer() != 0x4b) {
			result.accounts.push_back(Candidate{ .error = Error::UnsupportedFormat });
			continue;
		}
		auto candidate = ReadAuthorization(body.blob());
		const auto configName = name + QString::fromLatin1("/config");
		const auto configBytes = Envelope(files, configName, modified);
		const auto haveConfig = !files(configName + QLatin1Char('s')).isEmpty()
			|| !files(configName + QLatin1Char('0')).isEmpty()
			|| !files(configName + QLatin1Char('1')).isEmpty();
		if (haveConfig && configBytes.isEmpty()) {
			candidate.error = Error::Corrupt;
		} else if (!configBytes.isEmpty()) {
			auto config = Reader(configBytes);
			auto configData = Reader(Decrypt(config.blob(), key));
			auto fields = Reader(configData.blob());
			const auto version = fields.integer();
			const auto environment = fields.integer();
			if (config.ok() && fields.ok() && version <= 10 && environment <= 1) {
				candidate.test = environment == 1;
			} else {
				candidate.error = Error::UnsupportedFormat;
			}
		}
		ReadSelfHint(files, name, key, candidate, modified);
		result.accounts.push_back(std::move(candidate));
	}
	return result;
}

Result Archive(const QByteArray &bytes, const QByteArray &passcode) {
	const auto read16 = [&](qsizetype offset) {
		return qFromLittleEndian<quint16>(bytes.constData() + offset);
	};
	const auto read32 = [&](qsizetype offset) {
		return qFromLittleEndian<quint32>(bytes.constData() + offset);
	};
	auto end = qsizetype(-1);
	for (auto i = bytes.size() - 22; i >= std::max(qsizetype(0), bytes.size() - 65557); --i) {
		if (read32(i) == 0x06054b50U && i + 22 + read16(i + 20) == bytes.size()) {
			end = i;
			break;
		}
	}
	if (end < 0 || read16(end + 4) || read16(end + 6)
		|| read16(end + 8) != read16(end + 10)) {
		return { {}, Error::UnsafeArchive };
	}
	const auto count = read16(end + 10);
	const auto directorySize = read32(end + 12);
	auto offset = qsizetype(read32(end + 16));
	if (!count || count > kMaxEntries || offset + directorySize != end) {
		return { {}, Error::UnsafeArchive };
	}
	const auto directoryStart = offset;
	auto ranges = std::vector<std::pair<qsizetype, qsizetype>>();
	auto files = QMap<QString, QByteArray>();
	auto modified = QMap<QString, qint64>();
	auto names = QSet<QString>();
	auto roots = QSet<QString>();
	auto total = quint64(0);
	for (auto i = 0; i != count; ++i) {
		if (offset < 0 || offset + 46 > end || read32(offset) != 0x02014b50U) {
			return { {}, Error::UnsafeArchive };
		}
		const auto stamp = (quint32(read16(offset + 14)) << 16) | read16(offset + 12);
		const auto flags = read16(offset + 8);
		const auto method = read16(offset + 10);
		const auto crc = read32(offset + 16);
		const auto compressed = read32(offset + 20);
		const auto size = read32(offset + 24);
		const auto nameSize = read16(offset + 28);
		const auto extra = read16(offset + 30);
		const auto comment = read16(offset + 32);
		const auto attributes = read32(offset + 38);
		const auto local = qsizetype(read32(offset + 42));
		if (offset + 46 + nameSize + extra + comment > end || nameSize > 512 || !nameSize
			|| flags & 1 || (method != 0 && method != 8) || (attributes >> 16 & 0170000) == 0120000
			|| size > kMaxFile || (total += size) > kMaxArchive) {
			return { {}, Error::UnsafeArchive };
		}
		const auto nameBytes = bytes.mid(offset + 46, nameSize);
		const auto name = QString::fromUtf8(nameBytes);
		const auto components = name.split(QLatin1Char('/'));
		if (name.contains(QChar(0)) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':'))
			|| name.toUtf8() != nameBytes
			|| name.contains(QString::fromLatin1("//"))
			|| name.startsWith(QLatin1Char('/')) || components.size() > 8
			|| components.contains(QString::fromLatin1("..")) || components.contains(QString::fromLatin1("."))
			|| names.contains(name.toCaseFolded())) {
			return { {}, Error::UnsafeArchive };
		}
		names.insert(name.toCaseFolded());
		offset += 46 + nameSize + extra + comment;
		if (name.endsWith(QLatin1Char('/'))) {
			continue;
		}
		if (local < 0 || local + 30 > directoryStart || read32(local) != 0x04034b50U
			|| read16(local + 6) != flags || read16(local + 8) != method) {
			return { {}, Error::UnsafeArchive };
		}
		const auto dataOffset = local + 30 + read16(local + 26) + read16(local + 28);
		if (dataOffset + compressed > directoryStart || dataOffset > directoryStart
			|| bytes.mid(local + 30, read16(local + 26)) != nameBytes) {
			return { {}, Error::UnsafeArchive };
		}
		for (const auto &[from, to] : ranges) {
			if (local < to && dataOffset + compressed > from) {
				return { {}, Error::UnsafeArchive };
			}
		}
		ranges.emplace_back(local, dataOffset + compressed);
		auto output = QByteArray(std::max(size, 1U), Qt::Uninitialized);
		if (method == 0) {
			if (size != compressed) {
				return { {}, Error::UnsafeArchive };
			}
			output = bytes.mid(dataOffset, size);
		} else {
			auto stream = z_stream();
			stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData() + dataOffset));
			stream.avail_in = compressed;
			stream.next_out = reinterpret_cast<Bytef*>(output.data());
			stream.avail_out = output.size();
			if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
				return { {}, Error::UnsafeArchive };
			}
			const auto status = inflate(&stream, Z_FINISH);
			inflateEnd(&stream);
			if (status != Z_STREAM_END || stream.total_out != size || stream.total_in != compressed) {
				return { {}, Error::UnsafeArchive };
			}
		}
		output.resize(size);
		if (crc32(0, reinterpret_cast<const Bytef*>(output.constData()), output.size()) != crc) {
			return { {}, Error::UnsafeArchive };
		}
		if (components.last() == QLatin1String("key_datas")
			|| components.last() == QLatin1String("key_data0") || components.last() == QLatin1String("key_data1")) {
			roots.insert(name.left(name.size() - components.last().size()));
		}
		files.insert(name, std::move(output));
		modified.insert(name, stamp);
	}
	if (roots.size() != 1) {
		return { {}, roots.isEmpty() ? Error::UnsupportedFormat : Error::AmbiguousRoot };
	}
	const auto root = *roots.begin();
	return Tdata(
		[&](const QString &name) { return files.value(root + name); },
		passcode,
		[&](const QString &name) { return modified.value(root + name); });
}

} // namespace

Reservation::Reservation(Candidate candidate)
: _candidate(std::move(candidate)) {
}

Reservation::~Reservation() {
	const auto lock = std::scoped_lock(ReservationMutex);
	Reservations.erase(this);
}

std::unique_ptr<Reservation> Reservation::Acquire(const Candidate &candidate) {
	const auto lock = std::scoped_lock(ReservationMutex);
	for (const auto other : Reservations) {
		if (SameAuthorization(candidate, other->_candidate)) {
			return nullptr;
		}
	}
	auto result = std::unique_ptr<Reservation>(new Reservation(candidate));
	Reservations.insert(result.get());
	return result;
}

Result ReadSessions(const QStringList &paths) {
	if (paths.isEmpty() || paths.size() > kMaxAccounts) {
		return { {}, Error::UnsupportedFormat };
	}
	auto result = Result();
	for (const auto &path : paths) {
		result.accounts.push_back(Session(path));
	}
	return result;
}

Result ReadTdata(const QString &path, const QByteArray &passcode) {
	const auto info = QFileInfo(path);
	if (info.isDir() && !info.isSymLink()) {
		const auto root = info.canonicalFilePath() + QLatin1Char('/');
		return Tdata([&](const QString &name) {
			const auto child = QFileInfo(root + name);
			return child.canonicalFilePath().startsWith(root) ? ReadFile(child.filePath()) : QByteArray();
		}, passcode, [&](const QString &name) {
			return QFileInfo(root + name).lastModified().toMSecsSinceEpoch();
		});
	}
	const auto bytes = ReadFile(path, kMaxArchive);
	return bytes.isEmpty() ? Result{ {}, Error::ReadFailed } : Archive(bytes, passcode);
}

Candidate ReadAuthorization(const QByteArray &serialized) {
	auto reader = Reader(serialized);
	auto result = Candidate();
	const auto first = reader.integer();
	const auto second = reader.integer();
	const auto wide = first == 0xffffffffU && second == 0xffffffffU;
	result.userId = wide ? reader.wide() : first;
	result.mainDc = wide ? int(reader.integer()) : int(second);
	const auto count = reader.integer();
	if (!count || count > 16 || result.mainDc < 1 || result.mainDc > 5) {
		result.error = Error::Corrupt;
		return result;
	}
	auto dcs = QSet<int>();
	for (auto i = 0U; i != count; ++i) {
		const auto dc = int(reader.integer());
		const auto key = reader.raw(256);
		if (dc < 1 || dc > 5 || dcs.contains(dc) || !reader.ok()) {
			result.error = Error::Corrupt;
			result.keys.clear();
			return result;
		}
		dcs.insert(dc);
		result.keys.push_back({ dc, key });
	}
	const auto stale = reader.integer();
	if (stale <= 16) {
		const auto ignored = reader.raw(stale * 260);
	}
	if (!reader.end() || !dcs.contains(result.mainDc) || stale > 16) {
		result.error = Error::Corrupt;
		result.keys.clear();
	}
	return result;
}

QByteArray SerializeAuthorization(const Candidate &candidate) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << quint64(0xffffffffffffffffULL) << candidate.userId << qint32(candidate.mainDc)
		<< qint32(candidate.keys.size());
	for (const auto &key : candidate.keys) {
		stream << qint32(key.dc);
		stream.writeRawData(key.bytes.constData(), int(key.bytes.size()));
	}
	stream << qint32(0);
	return result;
}

bool SameAuthorization(const Candidate &a, const Candidate &b) {
	if (a.test == b.test && a.userId && a.userId == b.userId) {
		return true;
	}
	for (const auto &left : a.keys) {
		for (const auto &right : b.keys) {
			if (left.bytes.size() == 256 && right.bytes.size() == 256
				&& CRYPTO_memcmp(left.bytes.constData(), right.bytes.constData(), 256) == 0) {
				return true;
			}
		}
	}
	return false;
}

} // namespace Fork::Personal::SessionImport
