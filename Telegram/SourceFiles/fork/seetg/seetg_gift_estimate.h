#pragma once

#include "fork/seetg/seetg_gift_estimate_values.h"
#include <QtGui/QImage>
#include <rpl/event_stream.h>

namespace Main {
class Session;
} // namespace Main

namespace Fork::SeeTg::GiftDetails {

class Snapshot final {
public:
	Snapshot(not_null<Main::Session*> session, QString slug);
	~Snapshot();
	[[nodiscard]] QJsonObject data() const;
	[[nodiscard]] QImage logo() const;
	[[nodiscard]] rpl::producer<> changes() const;
	void request();
	[[nodiscard]] bool idle() const;

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

};

[[nodiscard]] std::shared_ptr<Snapshot> Watch(
	not_null<Main::Session*> session,
	const QString &slug);
} // namespace Fork::SeeTg::GiftDetails
