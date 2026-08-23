// SPDX-License-Identifier: MIT

#include "NerdSymbolIcon.h"

#include <QApplication>
#include <QCache>
#include <QFile>
#include <QIconEngine>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPalette>
#include <QPixmapCache>
#include <QSvgRenderer>
#include <QtMath>
#include <algorithm>
#include <memory>

namespace {

qreal normalizedDevicePixelRatio(const qreal value) {
    return qIsFinite(value) && value > 0.0 ? std::clamp(value, 1.0, 16.0) : 1.0;
}

std::shared_ptr<QSvgRenderer> rendererForPack(const QString& pack) {
    static QMutex mutex;
    static QCache<QString, std::shared_ptr<QSvgRenderer>> renderers(8);

    const QMutexLocker locker(&mutex);
    if (const auto* cached = renderers.object(pack)) {
        return *cached;
    }

    QFile source(QStringLiteral(":/icon-chosen/catalog/%1").arg(pack));
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    auto renderer = std::make_shared<QSvgRenderer>(source.readAll());
    if (!renderer->isValid()) {
        return {};
    }
    renderers.insert(pack, new std::shared_ptr<QSvgRenderer>(renderer));
    return renderer;
}

QRectF mappedElementBounds(const QSvgRenderer& renderer, const QString& element,
                           const QRectF& destination) {
    const QRectF viewBox = renderer.viewBoxF();
    const QRectF elementBounds = renderer.boundsOnElement(element);
    if (viewBox.isEmpty() || elementBounds.isEmpty() || destination.isEmpty()) {
        return {};
    }

    const qreal scale = std::min(destination.width() / viewBox.width(),
                                 destination.height() / viewBox.height());
    const QSizeF canvasSize(viewBox.width() * scale, viewBox.height() * scale);
    const QRectF canvas(QPointF(destination.center().x() - canvasSize.width() / 2.0,
                               destination.center().y() - canvasSize.height() / 2.0),
                        canvasSize);
    return QRectF(canvas.left() + (elementBounds.left() - viewBox.left()) * scale,
                  canvas.top() + (elementBounds.top() - viewBox.top()) * scale,
                  elementBounds.width() * scale, elementBounds.height() * scale);
}

class NerdSymbolIconEngine final : public QIconEngine {
  public:
    NerdSymbolIconEngine(QString pack, QString element)
        : pack_(std::move(pack)), element_(std::move(element)) {}

    [[nodiscard]] QIconEngine* clone() const override {
        return new NerdSymbolIconEngine(*this);
    }

    [[nodiscard]] QString key() const override {
        return QStringLiteral("icon-chosen-nerd-svg");
    }

    [[nodiscard]] QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
        const int extent = std::min(size.width(), size.height());
        return {extent, extent};
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, const QIcon::Mode mode,
                                 const QIcon::State state) override {
        return render(size, 1.0, mode, state);
    }

    [[nodiscard]] QPixmap scaledPixmap(const QSize& size, const QIcon::Mode mode,
                                       const QIcon::State state, const qreal scale) override {
        QSize logicalSize = size;
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        if (scale > 0.0 && !qFuzzyCompare(scale, 1.0)) {
            logicalSize = QSize(std::max(1, qRound(size.width() / scale)),
                                std::max(1, qRound(size.height() / scale)));
        }
#endif
        return render(logicalSize, scale, mode, state);
    }

    void paint(QPainter* painter, const QRect& rect, const QIcon::Mode mode,
               const QIcon::State state) override {
        if (painter == nullptr || rect.isEmpty()) {
            return;
        }
        const QSize logicalSize = actualSize(rect.size(), mode, state);
        const qreal dpr =
            painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap image = render(logicalSize, dpr, mode, state);
        const QRect target(QPoint(rect.center().x() - logicalSize.width() / 2,
                                  rect.center().y() - logicalSize.height() / 2),
                           logicalSize);
        painter->drawPixmap(target, image);
    }

  private:
    [[nodiscard]] QPixmap render(const QSize& requestedSize, const qreal requestedDpr,
                                 const QIcon::Mode mode, const QIcon::State state) const {
        if (requestedSize.isEmpty()) {
            return {};
        }

        const int extent = std::min(requestedSize.width(), requestedSize.height());
        const QSize logicalSize(extent, extent);
        const qreal dpr = normalizedDevicePixelRatio(requestedDpr);
        const QPalette palette = QApplication::palette();
        QPalette::ColorGroup group =
            mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const QColor color = mode == QIcon::Selected
                                 ? palette.color(group, QPalette::HighlightedText)
                                 : palette.color(group, QPalette::Text);
        const QString cacheKey = QStringLiteral("icon-chosen.v2.%1.%2.%3.%4.%5.%6.%7")
                                     .arg(pack_, element_)
                                     .arg(logicalSize.width())
                                     .arg(qRound64(dpr * 1024.0))
                                     .arg(static_cast<int>(mode))
                                     .arg(static_cast<int>(state))
                                     .arg(palette.cacheKey());

        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) {
            return cached;
        }

        const std::shared_ptr<QSvgRenderer> renderer = rendererForPack(pack_);
        if (!renderer || !renderer->elementExists(element_)) {
            return {};
        }

        const QSize physicalSize(std::max(1, qCeil(logicalSize.width() * dpr)),
                                 std::max(1, qCeil(logicalSize.height() * dpr)));
        QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter imagePainter(&image);
        imagePainter.setRenderHint(QPainter::Antialiasing);
        const QRectF target = mappedElementBounds(*renderer, element_, QRectF(image.rect()));
        if (target.isEmpty()) {
            return {};
        }
        // QSvgRenderer stretches an isolated element to the supplied rectangle. Mapping the
        // element's original bounds through the pack's fixed viewBox preserves both its aspect
        // ratio and the optical padding established by the catalog generator.
        renderer->render(&imagePainter, element_, target);
        imagePainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        imagePainter.fillRect(image.rect(), color);
        imagePainter.end();

        QPixmap result = QPixmap::fromImage(std::move(image));
        result.setDevicePixelRatio(dpr);
        QPixmapCache::insert(cacheKey, result);
        return result;
    }

    QString pack_;
    QString element_;
};

} // namespace

QIcon nerdSymbolIcon(const QString& pack, const QString& element) {
    return QIcon(new NerdSymbolIconEngine(pack, element));
}
