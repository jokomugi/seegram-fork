#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>
#include <rpl/variable.h>

class QNetworkReply;

namespace Fork::Personal {

enum class ReleaseCheckState { Idle, Checking, Current, Available, Failed };

struct ReleaseCheck {
	ReleaseCheckState state = ReleaseCheckState::Idle;
	QString release;
	bool operator==(const ReleaseCheck&) const = default;
};

class ReleaseChecker final : public QObject {
public:
	explicit ReleaseChecker(QObject *parent);
	void check();
	[[nodiscard]] rpl::producer<ReleaseCheck> value() const;

private:
	void receive();
	void finish();
	QNetworkAccessManager _network;
	QNetworkReply *_reply = nullptr;
	QByteArray _response;
	bool _oversized = false;
	rpl::variable<ReleaseCheck> _value;

};

} // namespace Fork::Personal
