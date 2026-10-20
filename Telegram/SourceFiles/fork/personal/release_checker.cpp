#include "fork/personal/release_checker.h"

#include "fork/personal/release_policy.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Fork::Personal {
namespace {

constexpr auto kTimeoutMs = 15000;
constexpr auto kMaximumResponse = 1024 * 1024;

} // namespace

ReleaseChecker::ReleaseChecker(QObject *parent)
: QObject(parent) {
}

void ReleaseChecker::check() {
	if (_reply) {
		return;
	}
	_response.clear();
	_oversized = false;
	_value = ReleaseCheck{ ReleaseCheckState::Checking, {} };
	auto request = QNetworkRequest(QUrl(u"https://api.github.com/repos/seegram/seegram-desktop/releases/latest"_q));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("User-Agent", "SeeGram-Personal");
	request.setTransferTimeout(kTimeoutMs);
	_reply = _network.get(request);
	QObject::connect(_reply, &QNetworkReply::readyRead, this, [=] { receive(); });
	QObject::connect(_reply, &QNetworkReply::finished, this, [=] { finish(); });
}

void ReleaseChecker::receive() {
	if (!_reply || _oversized) {
		return;
	}
	if (_response.size() + _reply->bytesAvailable() > kMaximumResponse) {
		_oversized = true;
		_reply->abort();
		return;
	}
	_response += _reply->readAll();
}

void ReleaseChecker::finish() {
	const auto reply = _reply;
	if (!reply) {
		return;
	}
	receive();
	auto result = ReleaseCheck{ ReleaseCheckState::Failed, {} };
	if (!_oversized && reply->error() == QNetworkReply::NoError
		&& reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
		const auto document = QJsonDocument::fromJson(_response);
		const auto object = document.object();
		const auto tag = object.value(u"tag_name"_q);
		const auto prerelease = object.value(u"prerelease"_q);
		const auto draft = object.value(u"draft"_q);
		if (document.isObject() && tag.isString() && prerelease.isBool()
			&& !prerelease.toBool() && draft.isBool() && !draft.toBool()) {
			result.release = tag.toString();
			const auto relation = CompareReleases(PERSONAL_SEEGRAM_RELEASE,
				result.release.toStdString());
			if (relation != ReleaseRelation::Invalid) {
				result.state = relation == ReleaseRelation::Newer
					? ReleaseCheckState::Available : ReleaseCheckState::Current;
			}
		}
	}
	_reply = nullptr;
	_response.clear();
	reply->deleteLater();
	_value = std::move(result);
}

rpl::producer<ReleaseCheck> ReleaseChecker::value() const {
	return _value.value();
}

} // namespace Fork::Personal
