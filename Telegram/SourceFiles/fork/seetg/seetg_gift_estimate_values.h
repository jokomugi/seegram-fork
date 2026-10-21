#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace Fork::SeeTg::GiftDetails {

[[nodiscard]] QJsonObject Estimate(const QJsonObject &data);
[[nodiscard]] QString Amount(double value);
[[nodiscard]] QString PreviewText(const QJsonObject &estimate);

} // namespace Fork::SeeTg::GiftDetails
