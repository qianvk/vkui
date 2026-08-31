// SPDX-License-Identifier: MIT

#include "private/VkLiquidGlassRenderer_p.h"

#include <QtCore/QChildEvent>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QLinearGradient>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <algorithm>
#include <utility>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

namespace vkui {
namespace {

constexpr qsizetype MaximumCaptureEntries = 8;

VLiquidGlassStyle sanitizedStyle(VLiquidGlassStyle style) noexcept {
    style.cornerRadius = std::max<qreal>(-1.0, style.cornerRadius);
    style.blurRadius = std::clamp(style.blurRadius, 0.0, 64.0);
    style.refractionHeight = std::clamp(style.refractionHeight, 0.0, 32.0);
    style.refractionAmount = std::clamp(style.refractionAmount, 0.0, 32.0);
    style.chromaticAberration = std::clamp(style.chromaticAberration, 0.0, 8.0);
    style.saturation = std::clamp(style.saturation, 0.0, 2.0);
    style.tintOpacity = std::clamp(style.tintOpacity, 0.0, 1.0);
    style.opticalEdgeIntensity = std::clamp(style.opticalEdgeIntensity, 0.0, 1.0);
    switch (style.quality) {
    case VLiquidGlassQuality::Automatic:
    case VLiquidGlassQuality::Reduced:
    case VLiquidGlassQuality::Balanced:
    case VLiquidGlassQuality::High:
        break;
    default:
        style.quality = VLiquidGlassQuality::Automatic;
        break;
    }
    return style;
}

qreal resolvedCornerRadius(const VLiquidGlassStyle& style, const QRectF& bounds) noexcept {
    const qreal maximum = std::max<qreal>(0.0, std::min(bounds.width(), bounds.height()) * 0.5);
    return style.cornerRadius < 0.0 ? maximum : std::clamp(style.cornerRadius, 0.0, maximum);
}

QColor glassTint() {
    const VkTheme& theme = VkThemeManager::instance()->theme();
    return theme.effectiveAppearance() == VkAppearance::Dark ? QColor(43, 44, 49)
                                                             : QColor(235, 238, 244);
}

VLiquidGlassStyle styleForThemePreference(VLiquidGlassStyle style) noexcept {
    constexpr qreal FullyTintedOpacity = 0.62;
    const qreal tint =
        normalizedLiquidGlassTintLevel(VkThemeManager::instance()->liquidGlassTintLevel());
    style.tintOpacity =
        std::lerp(style.tintOpacity, std::max(style.tintOpacity, FullyTintedOpacity), tint);
    return style;
}

} // namespace

VLiquidGlassStyle VLiquidGlassStyle::regular() noexcept {
    return {};
}

VLiquidGlassStyle VLiquidGlassStyle::clear() noexcept {
    VLiquidGlassStyle style;
    style.blurRadius = 0.0;
    style.refractionHeight = 14.0;
    style.refractionAmount = 28.0;
    style.chromaticAberration = 1.5;
    style.saturation = 1.16;
    style.tintOpacity = 0.04;
    style.quality = VLiquidGlassQuality::High;
    return style;
}

VLiquidGlassStyle VLiquidGlassStyle::control() noexcept {
    VLiquidGlassStyle style = regular();
    style.opticalEdgeIntensity = 0.0;
    return style;
}

VLiquidGlassStyle VLiquidGlassStyle::popup(const qreal cornerRadius) noexcept {
    VLiquidGlassStyle style = regular();
    style.cornerRadius = cornerRadius;
    // Transient information surfaces suppress high-frequency backdrop detail so text remains
    // readable without replacing the live material with an opaque fill.
    style.blurRadius = 20.0;
    style.refractionHeight = 0.0;
    style.refractionAmount = 0.0;
    style.chromaticAberration = 0.0;
    style.saturation = 0.82;
    style.tintOpacity = 0.40;
    style.opticalEdgeIntensity = 0.0;
    return style;
}

class VLiquidGlassBackdropPrivate final : public QObject {
  public:
    struct CaptureEntry final {
        QRect sourceRect;
        QSize sampleSize;
        QImage image;
    };

    explicit VLiquidGlassBackdropPrivate(VLiquidGlassBackdrop* owner) : QObject(owner), q(owner) {}

    void setSourceWidget(QWidget* source) {
        if (sourceWidget == source) {
            return;
        }
        clearObservedWidgets();
        QObject::disconnect(sourceDestroyedConnection);
        sourceWidget = source;
        if (sourceWidget) {
            sourceDestroyedConnection = connect(sourceWidget, &QObject::destroyed, q, [this] {
                sourceWidget = nullptr;
                clearObservedWidgets();
                captures.clear();
                Q_EMIT q->sourceWidgetChanged(nullptr);
                Q_EMIT q->invalidated();
            });
        }
        invalidateNow();
        Q_EMIT q->sourceWidgetChanged(sourceWidget);
    }

    void invalidateNow() {
        invalidationPending = false;
        captures.clear();
        Q_EMIT q->invalidated();
    }

    void scheduleInvalidation() {
        if (invalidationPending) {
            return;
        }
        invalidationPending = true;
        QTimer::singleShot(0, q, [this] { invalidateNow(); });
    }

    VkLiquidGlassFrame capture(const QWidget& surface, const int padding, const qreal sampleScale) {
        const bool recursivelyContained = sourceWidget && sourceWidget->isAncestorOf(&surface) &&
                                          sourceWidget->window() == surface.window();
        if (!sourceWidget || !sourceWidget->isVisible() || sourceWidget->size().isEmpty() ||
            sampleScale <= 0.0 || sourceWidget == &surface || recursivelyContained) {
            return {};
        }
        if (observedWidgets.isEmpty()) {
            observeWidgetTree(sourceWidget);
        }

        const QPoint surfaceOrigin = sourceWidget->mapFromGlobal(surface.mapToGlobal(QPoint(0, 0)));
        const QRect sourceRect(surfaceOrigin - QPoint(padding, padding),
                               surface.size() + QSize(padding * 2, padding * 2));
        const QSize sampleSize(std::max(1, qCeil(sourceRect.width() * sampleScale)),
                               std::max(1, qCeil(sourceRect.height() * sampleScale)));
        for (const CaptureEntry& entry : std::as_const(captures)) {
            if (entry.sourceRect == sourceRect && entry.sampleSize == sampleSize) {
                return {entry.image, padding, sampleScale};
            }
        }

        QImage captureImage(sampleSize, QImage::Format_ARGB32_Premultiplied);
        captureImage.fill(sourceWidget->palette().color(sourceWidget->backgroundRole()));

        const QRect boundedRect = sourceRect.intersected(sourceWidget->rect());
        if (!boundedRect.isEmpty()) {
            captureInProgress = true;
            const QPixmap sourcePixmap = sourceWidget->grab(boundedRect);
            captureInProgress = false;
            if (!sourcePixmap.isNull()) {
                const QPointF offset = (boundedRect.topLeft() - sourceRect.topLeft()) * sampleScale;
                const QSizeF scaledSize(boundedRect.width() * sampleScale,
                                        boundedRect.height() * sampleScale);
                QPainter painter(&captureImage);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(QRectF(offset, scaledSize), sourcePixmap.toImage());
            }
        }

        if (captures.size() >= MaximumCaptureEntries) {
            captures.removeFirst();
        }
        captures.append({sourceRect, sampleSize, captureImage});
        return {captureImage, padding, sampleScale};
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event == nullptr || captureInProgress) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::Destroy) {
            observedWidgets.remove(static_cast<QWidget*>(watched));
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::ChildAdded) {
            const auto* childEvent = static_cast<QChildEvent*>(event);
            QPointer<QWidget> child = qobject_cast<QWidget*>(childEvent->child());
            if (child) {
                QTimer::singleShot(0, q, [this, child] {
                    if (child) {
                        observeWidgetTree(child);
                    }
                });
            }
        }
        switch (event->type()) {
        case QEvent::Paint:
        case QEvent::UpdateRequest:
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            scheduleInvalidation();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void observeWidgetTree(QWidget* root) {
        if (root == nullptr || observedWidgets.contains(root)) {
            return;
        }
        root->installEventFilter(this);
        observedWidgets.insert(root, connect(root, &QObject::destroyed, q,
                                             [this, root] { observedWidgets.remove(root); }));
        const auto descendants = root->findChildren<QWidget*>();
        for (QWidget* descendant : descendants) {
            if (!observedWidgets.contains(descendant)) {
                descendant->installEventFilter(this);
                observedWidgets.insert(
                    descendant, connect(descendant, &QObject::destroyed, q, [this, descendant] {
                        observedWidgets.remove(descendant);
                    }));
            }
        }
    }

    void clearObservedWidgets() {
        const auto widgets = observedWidgets;
        observedWidgets.clear();
        for (auto iterator = widgets.cbegin(); iterator != widgets.cend(); ++iterator) {
            QObject::disconnect(iterator.value());
            QWidget* widget = iterator.key();
            if (widget) {
                widget->removeEventFilter(this);
            }
        }
    }

  public:
    VLiquidGlassBackdrop* q = nullptr;
    QPointer<QWidget> sourceWidget;
    QMetaObject::Connection sourceDestroyedConnection;
    QList<CaptureEntry> captures;
    QHash<QWidget*, QMetaObject::Connection> observedWidgets;
    bool invalidationPending = false;
    bool captureInProgress = false;
};

VLiquidGlassBackdrop::VLiquidGlassBackdrop(QWidget* sourceWidget, QObject* parent)
    : QObject(parent), d(std::make_unique<VLiquidGlassBackdropPrivate>(this)) {
    d->setSourceWidget(sourceWidget);
}

VLiquidGlassBackdrop::~VLiquidGlassBackdrop() = default;

void VLiquidGlassBackdrop::setSourceWidget(QWidget* sourceWidget) {
    d->setSourceWidget(sourceWidget);
}

QWidget* VLiquidGlassBackdrop::sourceWidget() const noexcept {
    return d->sourceWidget;
}

void VLiquidGlassBackdrop::invalidate() {
    d->invalidateNow();
}

class VLiquidGlassSurfacePrivate final {
  public:
    explicit VLiquidGlassSurfacePrivate(VLiquidGlassSurface* owner) : q(owner) {}

    void setBackdrop(VLiquidGlassBackdrop* nextBackdrop) {
        if (backdrop == nextBackdrop) {
            return;
        }
        QObject::disconnect(backdropInvalidatedConnection);
        QObject::disconnect(backdropDestroyedConnection);
        backdrop = nextBackdrop;
        if (backdrop) {
            backdropInvalidatedConnection = QObject::connect(
                backdrop, &VLiquidGlassBackdrop::invalidated, q, [this] { invalidate(); });
            backdropDestroyedConnection =
                QObject::connect(backdrop, &QObject::destroyed, q, [this] {
                    backdrop = nullptr;
                    invalidate();
                    Q_EMIT q->backdropChanged(nullptr);
                });
        }
        invalidate();
        Q_EMIT q->backdropChanged(backdrop);
    }

    void invalidate() {
        material = {};
        materialDirty = true;
        q->update();
    }

    void rebuildMaterial() {
        materialDirty = false;
        material = {};
        if (!enabled || !VkThemeManager::instance()->liquidGlassEnabled() || !backdrop ||
            q->size().isEmpty()) {
            return;
        }
        const VLiquidGlassStyle effectiveStyle = styleForThemePreference(style);
        const int padding = VkLiquidGlassRenderer::capturePadding(effectiveStyle);
        const qreal scale =
            VkLiquidGlassRenderer::sampleScale(effectiveStyle.quality, q->devicePixelRatioF());
        const VkLiquidGlassFrame frame = backdrop->d->capture(*q, padding, scale);
        material = VkLiquidGlassRenderer::render(frame, q->size(), effectiveStyle, glassTint());
    }

    void paint(QPainter& painter) {
        if (!enabled) {
            return;
        }
        if (materialDirty) {
            rebuildMaterial();
        }

        const QRectF bounds = QRectF(q->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (bounds.isEmpty()) {
            return;
        }
        painter.save();
        const qreal radius = resolvedCornerRadius(style, bounds);
        QPainterPath path;
        path.addRoundedRect(bounds, radius, radius);

        painter.setRenderHint(QPainter::Antialiasing);
        const auto* manager = VkThemeManager::instance();
        if (!manager->liquidGlassEnabled()) {
            const VkTheme& theme = manager->theme();
            painter.fillPath(path, theme.colors().elevatedBackground);
            if (style.drawsBorder) {
                painter.setPen(QPen(theme.colors().border, theme.metrics().borderWidth));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
            }
            painter.restore();
            return;
        }
        painter.save();
        painter.setClipPath(path);
        if (!material.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(bounds, material);
        } else {
            QColor fallback = glassTint();
            const bool dark =
                VkThemeManager::instance()->theme().effectiveAppearance() == VkAppearance::Dark;
            fallback.setAlphaF(dark ? 0.68F : 0.76F);
            painter.fillPath(path, fallback);
        }
        painter.restore();

        const bool dark =
            VkThemeManager::instance()->theme().effectiveAppearance() == VkAppearance::Dark;
        painter.setBrush(Qt::NoBrush);
        const qreal edgeIntensity = style.opticalEdgeIntensity;
        if (edgeIntensity > 0.0) {
            const auto edgeColor = [edgeIntensity](const int alpha) {
                return QColor(255, 255, 255, qRound(alpha * edgeIntensity));
            };
            QLinearGradient specular(bounds.topLeft(), bounds.bottomLeft());
            specular.setColorAt(0.0, edgeColor(dark ? 118 : 168));
            specular.setColorAt(0.34, edgeColor(dark ? 36 : 54));
            specular.setColorAt(0.66, edgeColor(dark ? 20 : 30));
            specular.setColorAt(1.0, edgeColor(dark ? 48 : 68));
            painter.setPen(QPen(specular, 1.0));
            painter.drawPath(path);
        }

        if (style.drawsBorder) {
            // Draw the neutral rim last so directional highlights never erase one side.
            painter.setPen(QPen(dark ? QColor(255, 255, 255, 44) : QColor(0, 0, 0, 32), 1.0));
            painter.drawPath(path);
        }

        const QRectF innerBounds = bounds.adjusted(1.0, 1.0, -1.0, -1.0);
        if (edgeIntensity > 0.0 && !innerBounds.isEmpty()) {
            QPainterPath innerPath;
            innerPath.addRoundedRect(innerBounds, std::max<qreal>(0.0, radius - 1.0),
                                     std::max<qreal>(0.0, radius - 1.0));
            painter.setPen(
                QPen(QColor(255, 255, 255, qRound((dark ? 30 : 62) * edgeIntensity)), 1.0));
            painter.drawPath(innerPath);
        }
        painter.restore();
    }

    VLiquidGlassSurface* q = nullptr;
    QPointer<VLiquidGlassBackdrop> backdrop;
    QMetaObject::Connection backdropInvalidatedConnection;
    QMetaObject::Connection backdropDestroyedConnection;
    VLiquidGlassStyle style = VLiquidGlassStyle::regular();
    QImage material;
    bool enabled = true;
    bool materialDirty = true;
};

VLiquidGlassSurface::VLiquidGlassSurface(QWidget* parent)
    : QWidget(parent), d(std::make_unique<VLiquidGlassSurfacePrivate>(this)) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
            [this](quint64, const VkThemeChanges changes) {
                if (changes.testFlag(VkThemeChange::Colors)) {
                    d->invalidate();
                }
            });
    connect(VkThemeManager::instance(), &VkThemeManager::liquidGlassEnabledChanged, this,
            [this] { d->invalidate(); });
    connect(VkThemeManager::instance(), &VkThemeManager::liquidGlassTintLevelChanged, this,
            [this] { d->invalidate(); });
}

VLiquidGlassSurface::~VLiquidGlassSurface() = default;

void VLiquidGlassSurface::setBackdrop(VLiquidGlassBackdrop* backdrop) {
    d->setBackdrop(backdrop);
}

VLiquidGlassBackdrop* VLiquidGlassSurface::backdrop() const noexcept {
    return d->backdrop;
}

void VLiquidGlassSurface::setGlassStyle(const VLiquidGlassStyle& style) {
    const VLiquidGlassStyle resolved = sanitizedStyle(style);
    if (d->style == resolved) {
        return;
    }
    d->style = resolved;
    d->invalidate();
    Q_EMIT glassStyleChanged();
}

VLiquidGlassStyle VLiquidGlassSurface::glassStyle() const noexcept {
    return d->style;
}

void VLiquidGlassSurface::setGlassEnabled(const bool enabled) {
    if (d->enabled == enabled) {
        return;
    }
    d->enabled = enabled;
    d->invalidate();
    Q_EMIT glassEnabledChanged(enabled);
}

bool VLiquidGlassSurface::isGlassEnabled() const noexcept {
    return d->enabled;
}

void VLiquidGlassSurface::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    d->paint(painter);
}

void VLiquidGlassSurface::paintMaterial(QPainter& painter) {
    d->paint(painter);
}

bool VLiquidGlassSurface::event(QEvent* event) {
    if (event != nullptr) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::ParentChange:
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
            d->invalidate();
            break;
        default:
            break;
        }
    }
    return QWidget::event(event);
}

} // namespace vkui
