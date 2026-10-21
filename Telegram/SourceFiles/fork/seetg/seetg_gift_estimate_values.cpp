#include "fork/seetg/seetg_gift_estimate_values.h"

#include <QtCore/QJsonArray>
#include <cmath>

namespace Fork::SeeTg::GiftDetails {

QJsonObject Estimate(const QJsonObject &data) {
	const auto items = data[QLatin1StringView("searchGifts")].toObject()[QLatin1StringView("items")].toArray();
	return items.isEmpty() ? QJsonObject() : items.first().toObject()[QLatin1StringView("estimate")].toObject();
}

QString Amount(double value) {
	auto result = QString::number(value, 'f', value >= 1000 ? 0 : value >= 100 ? 1 : 2);
	if (result.contains('.')) {
		while (result.endsWith('0')) {
			result.chop(1);
		}
		if (result.endsWith('.')) {
			result.chop(1);
		}
	}
	return result;
}

QString PreviewText(const QJsonObject &estimate) {
	const auto price = estimate[QLatin1StringView("ton")].toDouble();
	const auto maximum = estimate[QLatin1StringView("high")].toDouble();
	if (!std::isfinite(price) || price <= 0 || !std::isfinite(maximum) || maximum < price) {
		return {};
	}
	return Amount(price) + QChar(0x2013) + Amount(maximum);
}

} // namespace Fork::SeeTg::GiftDetails
