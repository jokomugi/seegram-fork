#include "history/view/media/history_view_web_page.h"

#include "fork/seetg/seetg_gift_estimate.h"
#include "fork/seetg/seetg_settings.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "main/main_session.h"
#include "history/history.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>

#include "styles/style_chat.h"

namespace HistoryView {

void WebPage::releaseGiftEstimate() const {
	_giftLifetime.destroy();
	_giftEstimate = nullptr;
	_giftSlug.clear();
	_giftTon = QImage();
}

void WebPage::ensureGiftEstimate() const {
	const auto slug = _data->uniqueGift ? _data->uniqueGift->slug : QString();
	const auto weak = base::make_weak(this);
	const auto repaint = [=] {
		if (weak) {
			_data->session().data().requestViewRepaint(parent());
		}
	};
	if (_giftSlug != slug) {
		releaseGiftEstimate();
		_giftSlug = slug;
		if (!slug.isEmpty()) {
			history()->owner().registerHeavyViewPart(_parent);
			Fork::SeeTg::EnabledValue(Fork::SeeTg::Feature::GiftDetails)
			| rpl::skip(1)
			| rpl::on_next([=] {
				const auto notify = repaint;
				releaseGiftEstimate();
				notify();
			}, _giftLifetime);
		}
	}
	if (slug.isEmpty() || _giftEstimate
		|| !Fork::SeeTg::Enabled(Fork::SeeTg::Feature::GiftDetails)) {
		return;
	}
	_giftEstimate = Fork::SeeTg::GiftDetails::Watch(&_data->session(), slug);
	if (_giftEstimate) {
		_giftEstimate->changes() | rpl::on_next(repaint, _giftLifetime);
	}
}

void WebPage::drawGiftEstimate(Painter &p, const PaintContext &context) const {
	ensureGiftEstimate();
	if (!_data->uniqueGift || !_giftEstimate
		|| !Fork::SeeTg::Enabled(Fork::SeeTg::Feature::GiftDetails)) {
		return;
	}
	const auto estimate = Fork::SeeTg::GiftDetails::Estimate(_giftEstimate->data());
	const auto range = Fork::SeeTg::GiftDetails::PreviewText(estimate);
	if (range.isEmpty()) {
		return;
	}
	const auto confidence = estimate[u"confidence"_q].toString();
	const auto level = confidence == u"high" ? u"high"_q
		: confidence == u"medium" ? u"probably"_q
		: confidence == u"low" ? u"maybe"_q : QString();
	const auto font = st::msgDateFont;
	const auto iconSize = font->height * 3 / 4;
	const auto gap = font->spacew;
	const auto logo = _giftEstimate->logo();
	const auto logoWidth = logo.isNull() ? 0 : font->height + gap;
	const auto textWidth = font->width(range) + gap + iconSize
		+ (level.isEmpty() ? 0 : gap + font->width(level));
	const auto size = QSize(
		logoWidth + textWidth + 2 * st::msgDateImgPadding.x(),
		font->height + 2 * st::msgDateImgPadding.y());
	if (size.width() + 2 * st::msgDateImgDelta > _attach->width()
		|| size.height() + 2 * st::msgDateImgDelta > _attach->height()) {
		return;
	}
	const auto quality = PainterHighQualityEnabler(p);
	const auto rect = QRect(
		QPoint(
			_attach->width() - size.width() - st::msgDateImgDelta,
			_attach->height() - size.height() - st::msgDateImgDelta),
		size);
	const auto imageStyle = context.imageStyle();
	Ui::FillRoundRect(
		p,
		rect.x(),
		rect.y(),
		rect.width(),
		rect.height(),
		imageStyle->msgDateImgBg,
		imageStyle->msgDateImgBgCorners);
	auto x = rect.x() + st::msgDateImgPadding.x();
	const auto y = rect.y() + st::msgDateImgPadding.y();
	if (!logo.isNull()) {
		p.save();
		auto clip = QPainterPath();
		clip.addEllipse(QRectF(x, y, font->height, font->height));
		p.setClipPath(clip, Qt::IntersectClip);
		p.drawImage(QRect(x, y, font->height, font->height), logo);
		p.restore();
		x += logoWidth;
	}
	p.setFont(font);
	p.setPen(context.st->msgDateImgFg());
	p.drawText(x, y + font->ascent, range);
	x += font->width(range) + gap;
	const auto pixels = QSize(iconSize, iconSize) * style::DevicePixelRatio();
	if (_giftTon.size() != pixels) {
		_giftTon = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
		_giftTon.fill(Qt::transparent);
		QPainter painter(&_giftTon);
		QSvgRenderer renderer(u":/fork/gifts/ton.svg"_q);
		renderer.render(&painter);
	}
	auto glyph = _giftTon;
	{
		QPainter painter(&glyph);
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(glyph.rect(), context.st->msgDateImgFg()->c);
	}
	p.drawImage(QRect(x, y + (font->height - iconSize) / 2, iconSize, iconSize), glyph);
	if (!level.isEmpty()) {
		p.drawText(x + iconSize + gap, y + font->ascent, level);
	}
}

QString WebPage::giftAdditionalInfo(QString result) const {
	if (_giftEstimate && Fork::SeeTg::Enabled(Fork::SeeTg::Feature::GiftDetails)) {
		const auto estimate = Fork::SeeTg::GiftDetails::Estimate(_giftEstimate->data());
		const auto range = Fork::SeeTg::GiftDetails::PreviewText(estimate);
		if (!range.isEmpty()) {
			result += u" · see.tg "_q + range + u" TON"_q;
			const auto confidence = estimate[u"confidence"_q].toString();
			if (confidence == u"high" || confidence == u"medium" || confidence == u"low") {
				result += u" · "_q + confidence;
			}
		}
	}
	return result;
}

} // namespace HistoryView
