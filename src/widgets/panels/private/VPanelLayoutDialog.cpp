// SPDX-License-Identifier: MIT

#include "VPanelLayoutDialog_p.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

namespace {

constexpr qreal ChooserWindowScale = 0.60;
constexpr int MinimumChooserWidth = 480;
constexpr int MinimumChooserHeight = 320;

QColor mix(const QColor& from, const QColor& to, const qreal amount) {
    const qreal progress = std::clamp(amount, 0.0, 1.0);
    const auto interpolate = [progress](const qreal first, const qreal second) {
        return first + (second - first) * progress;
    };
    return QColor::fromRgbF(static_cast<float>(interpolate(from.redF(), to.redF())),
                            static_cast<float>(interpolate(from.greenF(), to.greenF())),
                            static_cast<float>(interpolate(from.blueF(), to.blueF())),
                            static_cast<float>(interpolate(from.alphaF(), to.alphaF())));
}

QColor contrastingText(const QColor& background) {
    return background.lightnessF() > 0.58F ? QColor(24, 24, 26) : QColor(255, 255, 255);
}

QString panelText(const char* source) {
    return QCoreApplication::translate("VPanelManager", source);
}

QPainterPath panelPath(const QRectF& rect, const qreal radius, const QRectF& normalizedRect) {
    constexpr qreal EdgeTolerance = 0.0001;
    const qreal boundedRadius =
        std::clamp(radius, 0.0, std::min(rect.width(), rect.height()) * 0.5);
    const qreal topLeftRadius =
        normalizedRect.left() <= EdgeTolerance && normalizedRect.top() <= EdgeTolerance
            ? boundedRadius
            : 0.0;
    const qreal topRightRadius =
        normalizedRect.right() >= 1.0 - EdgeTolerance && normalizedRect.top() <= EdgeTolerance
            ? boundedRadius
            : 0.0;
    const qreal bottomRightRadius = normalizedRect.right() >= 1.0 - EdgeTolerance &&
                                            normalizedRect.bottom() >= 1.0 - EdgeTolerance
                                        ? boundedRadius
                                        : 0.0;
    const qreal bottomLeftRadius =
        normalizedRect.left() <= EdgeTolerance && normalizedRect.bottom() >= 1.0 - EdgeTolerance
            ? boundedRadius
            : 0.0;

    QPainterPath path;
    path.moveTo(rect.left() + topLeftRadius, rect.top());
    path.lineTo(rect.right() - topRightRadius, rect.top());
    if (topRightRadius > 0.0) {
        path.quadTo(rect.topRight(), QPointF(rect.right(), rect.top() + topRightRadius));
    }
    path.lineTo(rect.right(), rect.bottom() - bottomRightRadius);
    if (bottomRightRadius > 0.0) {
        path.quadTo(rect.bottomRight(), QPointF(rect.right() - bottomRightRadius, rect.bottom()));
    }
    path.lineTo(rect.left() + bottomLeftRadius, rect.bottom());
    if (bottomLeftRadius > 0.0) {
        path.quadTo(rect.bottomLeft(), QPointF(rect.left(), rect.bottom() - bottomLeftRadius));
    }
    path.lineTo(rect.left(), rect.top() + topLeftRadius);
    if (topLeftRadius > 0.0) {
        path.quadTo(rect.topLeft(), QPointF(rect.left() + topLeftRadius, rect.top()));
    }
    path.closeSubpath();
    return path;
}

class VPanelPreviewButton final : public QAbstractButton {
  public:
    explicit VPanelPreviewButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    void setPanelState(VPanelState state) {
        state_ = std::move(state);
        setObjectName(QStringLiteral("vPanelPreview_%1").arg(state_.id));
        setProperty("vPanelId", state_.id);
        setProperty("panelNumber", state_.number);
        setProperty("expanded", state_.expanded);
        setAccessibleName(panelText("Panel %1").arg(state_.number));
        setAccessibleDescription({});
        update();
    }

  protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void focusInEvent(QFocusEvent* event) override {
        keyboardFocusVisible_ = event->reason() == Qt::TabFocusReason ||
                                event->reason() == Qt::BacktabFocusReason ||
                                event->reason() == Qt::ShortcutFocusReason;
        update();
        QAbstractButton::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
        keyboardFocusVisible_ = false;
        update();
        QAbstractButton::focusOutEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        keyboardFocusVisible_ = false;
        QAbstractButton::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        keyboardFocusVisible_ = true;
        QAbstractButton::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const VkTheme& theme = VkThemeManager::instance()->theme();
        const auto& colors = theme.colors();
        const auto& metrics = theme.metrics();
        const bool dark = theme.effectiveAppearance() == VkAppearance::Dark;

        QColor fill;
        QColor badgeFill;
        if (state_.expanded) {
            fill = mix(colors.elevatedBackground, colors.accent, dark ? 0.24 : 0.14);
            badgeFill = colors.accent;
        } else {
            fill = mix(colors.contentBackground, colors.controlFill, dark ? 0.72 : 0.84);
            badgeFill = mix(colors.controlFill, colors.textTertiary, dark ? 0.34 : 0.20);
        }
        if (hovered_) {
            fill = mix(fill, state_.expanded ? colors.accentHovered : colors.controlFillHovered,
                       state_.expanded ? 0.20 : 0.42);
        }
        if (isDown()) {
            fill =
                mix(fill, state_.expanded ? colors.accentPressed : colors.controlFillPressed, 0.42);
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cardRect(QPointF(0.0, 0.0), QSizeF(size()));
        const qreal radius = std::min<qreal>(metrics.menuCornerRadius,
                                             std::min(cardRect.width(), cardRect.height()) * 0.18);
        const QPainterPath cardPath = panelPath(cardRect, radius, state_.normalizedRect);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(cardPath);

        if (!state_.expanded) {
            painter.save();
            painter.setClipPath(cardPath);
            QColor hatch = colors.textTertiary;
            hatch.setAlphaF(dark ? 0.11F : 0.07F);
            painter.setPen(QPen(hatch, 1.0));
            constexpr int hatchSpacing = 14;
            for (int offset = -height(); offset < width(); offset += hatchSpacing) {
                painter.drawLine(QPoint(offset, height()), QPoint(offset + height(), 0));
            }
            painter.restore();
        }

        constexpr qreal EdgeTolerance = 0.0001;
        painter.setPen(QPen(colors.separator, std::max<qreal>(1.0, metrics.borderWidth)));
        if (state_.normalizedRect.left() > EdgeTolerance) {
            painter.drawLine(QPointF(0.5, 0.0), QPointF(0.5, cardRect.height()));
        }
        if (state_.normalizedRect.top() > EdgeTolerance) {
            painter.drawLine(QPointF(0.0, 0.5), QPointF(cardRect.width(), 0.5));
        }

        const qreal preferredBadgeExtent =
            qBound(24.0, theme.typography().body.pointSizeF() * 2.25, 34.0);
        const qreal badgeExtent =
            std::max<qreal>(0.0, std::min(preferredBadgeExtent,
                                          std::min(cardRect.width(), cardRect.height()) - 16.0));
        if (badgeExtent > 0.0) {
            const QRectF badgeRect(cardRect.center().x() - badgeExtent * 0.5,
                                   cardRect.center().y() - badgeExtent * 0.5, badgeExtent,
                                   badgeExtent);
            painter.setPen(Qt::NoPen);
            painter.setBrush(badgeFill);
            painter.drawEllipse(badgeRect);
            painter.setFont(theme.typography().bodyEmphasized);
            painter.setPen(contrastingText(badgeFill));
            painter.drawText(badgeRect, Qt::AlignCenter, QString::number(state_.number));
        }

        if (hasFocus() && keyboardFocusVisible_) {
            QPen focusPen(colors.focusRing, std::max<qreal>(2.0, metrics.focusRingWidth));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(focusPen);
            const QRectF focusRect = cardRect.adjusted(3.0, 3.0, -3.0, -3.0);
            painter.drawPath(
                panelPath(focusRect, std::max<qreal>(1.0, radius - 3.0), state_.normalizedRect));
        }
    }

  private:
    VPanelState state_;
    bool hovered_ = false;
    bool keyboardFocusVisible_ = false;
};

} // namespace

class VPanelLayoutCanvas final : public QWidget {
  public:
    explicit VPanelLayoutCanvas(std::function<void(const QString&)> togglePanel,
                                QWidget* parent = nullptr)
        : QWidget(parent), togglePanel_(std::move(togglePanel)) {
        setObjectName(QStringLiteral("vPanelLayoutCanvas"));
        setMinimumHeight(210);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPanelStates(const QList<VPanelState>& states) {
        states_ = states;

        QSet<QString> liveIds;
        for (const VPanelState& state : states_) {
            liveIds.insert(state.id);
            VPanelPreviewButton* button = buttons_.value(state.id);
            if (button == nullptr) {
                button = new VPanelPreviewButton(this);
                buttons_.insert(state.id, button);
                connect(button, &QAbstractButton::clicked, this,
                        [this, id = state.id] { togglePanel_(id); });
                button->show();
            }
            button->setPanelState(state);
        }
        for (auto iterator = buttons_.begin(); iterator != buttons_.end();) {
            if (!liveIds.contains(iterator.key())) {
                delete iterator.value();
                iterator = buttons_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        relayout();
    }

    void retranslate() {
        for (const VPanelState& state : std::as_const(states_)) {
            if (VPanelPreviewButton* button = buttons_.value(state.id)) {
                button->setPanelState(state);
            }
        }
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        relayout();
    }

  private:
    void relayout() {
        if (states_.isEmpty() || rect().isEmpty()) {
            return;
        }

        const QRectF previewRect(rect());
        for (const VPanelState& state : states_) {
            VPanelPreviewButton* button = buttons_.value(state.id);
            if (button == nullptr) {
                continue;
            }
            const QRectF normalized =
                state.normalizedRect.isValid() ? state.normalizedRect : QRectF(0.0, 0.0, 1.0, 1.0);
            const int left = qRound(previewRect.left() + normalized.left() * previewRect.width());
            const int top = qRound(previewRect.top() + normalized.top() * previewRect.height());
            const int right = qRound(previewRect.left() + normalized.right() * previewRect.width());
            const int bottom =
                qRound(previewRect.top() + normalized.bottom() * previewRect.height());
            button->setGeometry(left, top, std::max(0, right - left), std::max(0, bottom - top));
            button->raise();
        }
    }

    QList<VPanelState> states_;
    QHash<QString, VPanelPreviewButton*> buttons_;
    std::function<void(const QString&)> togglePanel_;
};

VPanelLayoutDialog::VPanelLayoutDialog(QWidget* owner,
                                       std::function<void(const QString&)> togglePanel)
    : QDialog(owner, Qt::Popup | Qt::FramelessWindowHint), owner_(owner) {
    setObjectName(QStringLiteral("vPanelLayoutDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setSizeGripEnabled(false);
    // Qt::Popup closes on native outside clicks. The application filter gives synthetic and
    // embedded event paths the same cross-platform behavior without adding window chrome.
    QApplication::instance()->installEventFilter(this);

    auto* contentLayout = new QVBoxLayout(this);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    canvas_ = new VPanelLayoutCanvas(
        [this, togglePanel = std::move(togglePanel)](const QString& id) {
            togglePanel(id);
            accept();
        },
        this);
    contentLayout->addWidget(canvas_);

    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
            [this](quint64, const VkThemeChanges changes) {
                if (changes.testFlag(VkThemeChange::Colors) ||
                    changes.testFlag(VkThemeChange::Metrics) ||
                    changes.testFlag(VkThemeChange::Typography)) {
                    update();
                    canvas_->update();
                }
            });
}

VPanelLayoutDialog::~VPanelLayoutDialog() {
    if (QApplication::instance() != nullptr) {
        QApplication::instance()->removeEventFilter(this);
    }
}

void VPanelLayoutDialog::setPanelStates(const QList<VPanelState>& states) {
    canvas_->setPanelStates(states);
}

void VPanelLayoutDialog::present() {
    const QSize ownerSize =
        (owner_ != nullptr ? owner_->size() : QSize(900, 600)).expandedTo(QSize(1, 1));
    qreal scale =
        std::max({ChooserWindowScale, static_cast<qreal>(MinimumChooserWidth) / ownerSize.width(),
                  static_cast<qreal>(MinimumChooserHeight) / ownerSize.height()});
    if (QScreen* screen = owner_ != nullptr ? owner_->screen() : QApplication::primaryScreen()) {
        scale = std::min(
            scale, std::min(screen->availableGeometry().width() * 0.90 / ownerSize.width(),
                            screen->availableGeometry().height() * 0.90 / ownerSize.height()));
    }
    resize(qRound(ownerSize.width() * scale), qRound(ownerSize.height() * scale));

    if (owner_ != nullptr) {
        const QRect ownerGeometry(owner_->mapToGlobal(QPoint(0, 0)), owner_->size());
        move(ownerGeometry.center() - QPoint(width() / 2, height() / 2));
    }
    show();
    raise();
    activateWindow();
}

void VPanelLayoutDialog::paintEvent(QPaintEvent*) {
    const VkTheme& theme = VkThemeManager::instance()->theme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.colors().elevatedBackground);
    const QRectF background = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.drawRoundedRect(background, theme.metrics().menuCornerRadius,
                            theme.metrics().menuCornerRadius);
}

void VPanelLayoutDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        canvas_->retranslate();
    }
}

bool VPanelLayoutDialog::eventFilter(QObject* watched, QEvent* event) {
    if (isVisible() && event != nullptr &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::NonClientAreaMouseButtonPress)) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect globalGeometry(mapToGlobal(QPoint(0, 0)), size());
        if (!globalGeometry.contains(mouseEvent->globalPosition().toPoint())) {
            reject();
            return true;
        }
    }
    if (isVisible() && event != nullptr && event->type() == QEvent::ApplicationDeactivate) {
        reject();
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace vkui
