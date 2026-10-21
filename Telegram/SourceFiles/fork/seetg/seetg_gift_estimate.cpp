#include "fork/seetg/seetg_gift_estimate.h"

#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_visuals.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <cmath>
#include <map>
#include <deque>

namespace Fork::SeeTg::GiftDetails {
namespace {

constexpr auto kRetry = 30 * 1000;
constexpr auto kMaxRecords = 64;
constexpr auto kMaxParallel = 4;

struct Store {
	std::map<QString, std::shared_ptr<Snapshot>> records;
	int running = 0;
	std::deque<Fn<void()>> waiting;
};

std::map<Main::Session*, std::shared_ptr<Store>> Stores;

void Pump(const std::shared_ptr<Store> &store) {
	while (store->running < kMaxParallel && !store->waiting.empty()) {
		auto next = std::move(store->waiting.front());
		store->waiting.pop_front();
		next();
	}
}

} // namespace

struct Snapshot::Impl final : QObject {
	Impl(not_null<Main::Session*> session, QString slug);
	void load();
	void complete(QJsonObject data, bool success);

	const base::weak_ptr<Main::Session> session;
	std::weak_ptr<Store> store;
	const QString slug;
	QJsonObject data;
	QImage logo;
	crl::time nextAttempt = 0;
	bool pending = false;
	bool queued = false;
	rpl::event_stream<> updates;
	rpl::lifetime lifetime;

};

Snapshot::Impl::Impl(not_null<Main::Session*> session, QString slug)
: session(base::make_weak(session))
, store(Stores.at(session))
, slug(std::move(slug)) {
	EnabledValue(Feature::GiftDetails) | rpl::on_next([=](bool enabled) {
		if (!enabled) {
			data = {};
			logo = {};
			nextAttempt = 0;
			updates.fire({});
		}
	}, lifetime);
}

void Snapshot::Impl::load() {
	if (!session || pending || !Enabled(Feature::GiftDetails) || crl::now() < nextAttempt) {
		return;
	}
	const auto split = slug.lastIndexOf('-');
	const auto number = slug.mid(split + 1).toInt();
	if (split <= 0 || number <= 0) {
		return;
	}
	const auto shared = store.lock();
	if (!shared) {
		return;
	}
	const auto record = shared->records.find(slug);
	if (record == shared->records.end() || record->second.use_count() == 1) {
		return;
	}
	if (shared->running >= kMaxParallel) {
		if (!queued) {
			queued = true;
			shared->waiting.push_back(crl::guard(this, [=] {
				queued = false;
				load();
			}));
		}
		return;
	}
	pending = true;
	++shared->running;
	if (logo.isNull() && Enabled(Feature::GiftDetails)) {
		Visuals::Image(Visuals::UserpicUrl(u"seetgbot"_q), crl::guard(this, [=](QImage image) {
			if (Enabled(Feature::GiftDetails)) {
				logo = std::move(image);
				updates.fire({});
			}
		}));
	}
	Api::Query(
		session.get(),
		u"query GiftDetails($slug:String!,$num:Int!){ searchGifts(filter:{slug:$slug,numberFrom:$num,numberTo:$num},first:1){items{estimate{ton low high confidence collectionFloor modelFloor candidates{kind ton} number{premiumTon} updatedAt}}} marketListings(slug:$slug,num:$num){market amount currency link}}"_q,
		{ { u"slug"_q, slug.left(split) }, { u"num"_q, number } },
		crl::guard(this, [=](const QJsonObject &result) { complete(result, true); }),
		crl::guard(this, [=](const Api::Error&) { complete({}, false); }));
}

void Snapshot::Impl::complete(QJsonObject result, bool success) {
	pending = false;
	const auto shared = store.lock();
	if (shared) {
		--shared->running;
	}
	nextAttempt = success ? 0 : crl::now() + kRetry;
	data = Enabled(Feature::GiftDetails) ? std::move(result) : QJsonObject();
	updates.fire({});
	if (shared) {
		Pump(shared);
	}
}

Snapshot::Snapshot(not_null<Main::Session*> session, QString slug)
: _impl(std::make_unique<Impl>(session, std::move(slug))) {
}

Snapshot::~Snapshot() = default;

bool Snapshot::idle() const {
	return !_impl->pending && !_impl->queued;
}

QJsonObject Snapshot::data() const {
	return Enabled(Feature::GiftDetails) ? _impl->data : QJsonObject();
}

QImage Snapshot::logo() const {
	return Enabled(Feature::GiftDetails) ? _impl->logo : QImage();
}

rpl::producer<> Snapshot::changes() const {
	return _impl->updates.events();
}

void Snapshot::request() {
	QTimer::singleShot(0, _impl.get(), [impl = _impl.get()] { impl->load(); });
}

std::shared_ptr<Snapshot> Watch(not_null<Main::Session*> session, const QString &slug) {
	auto &store = Stores[session];
	if (!store) {
		store = std::make_shared<Store>();
		session->lifetime().add([session] { Stores.erase(session); });
	}
	if (const auto found = store->records.find(slug); found != store->records.end()) {
		found->second->request();
		return found->second;
	}
	for (auto i = store->records.begin(); store->records.size() >= kMaxRecords && i != store->records.end();) {
		if (i->second.use_count() == 1 && i->second->idle()) {
			i = store->records.erase(i);
		} else {
			++i;
		}
	}
	if (store->records.size() >= kMaxRecords) {
		return nullptr;
	}
	auto result = std::make_shared<Snapshot>(session, slug);
	store->records.emplace(slug, result);
	result->request();
	return result;
}

} // namespace Fork::SeeTg::GiftDetails
