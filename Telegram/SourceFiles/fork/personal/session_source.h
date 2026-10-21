#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <vector>
#include <memory>

namespace Fork::Personal::SessionImport {

enum class Error {
	None,
	ReadFailed,
	UnsupportedFormat,
	Corrupt,
	LiveDatabase,
	MissingKey,
	PasscodeRequired,
	UnsafeArchive,
	AmbiguousRoot,
	Duplicate,
	Bot,
};

struct Key {
	int dc = 0;
	QByteArray bytes;
};

struct Candidate {
	QString name;
	QString username;
	quint64 userId = 0;
	int mainDc = 0;
	bool test = false;
	std::vector<Key> keys;
	Error error = Error::None;
};

class Reservation final {
public:
	[[nodiscard]] static std::unique_ptr<Reservation> Acquire(const Candidate &candidate);
	~Reservation();
	Reservation(const Reservation&) = delete;
	Reservation &operator=(const Reservation&) = delete;

private:
	explicit Reservation(Candidate candidate);
	Candidate _candidate;

};

struct Result {
	std::vector<Candidate> accounts;
	Error error = Error::None;
};

[[nodiscard]] Result ReadSessions(const QStringList &paths);
[[nodiscard]] Result ReadTdata(
	const QString &path,
	const QByteArray &passcode = {});
[[nodiscard]] Candidate ReadAuthorization(const QByteArray &serialized);
[[nodiscard]] QByteArray SerializeAuthorization(const Candidate &candidate);
[[nodiscard]] bool SameAuthorization(const Candidate &a, const Candidate &b);

} // namespace Fork::Personal::SessionImport
