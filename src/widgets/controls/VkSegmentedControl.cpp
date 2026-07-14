// SPDX-License-Identifier: MIT

#include "../style/private/VkStylePainter_p.h"
#include "../animation/private/VkWidgetAnimation_p.h"
#include "private/VkSegmentedControl_p.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VkSegmentedControl.h>

namespace vkui {

namespace {

constexpr auto explicitAccessibleNameProperty = "_vkui_explicitAccessibleName";

void updateAutomaticAccessibleName(QAbstractButton* button) {
    if (!button || button->property(explicitAccessibleNameProperty).toBool()) {
        return;
    }
    button->setAccessibleName(button->text().isEmpty() ? button->icon().name() : button->text());
}

} // namespace

class VkSegmentButton final : public QAbstractButton {
  public:
    explicit VkSegmentButton(QWidget* parent) : QAbstractButton(parent) {
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(this, &QAbstractButton::pressed, this, [this] { setPressed(true); });
        connect(this, &QAbstractButton::released, this, [this] { setPressed(false); });
    }

    QSize sizeHint() const override {
        const auto& metrics = VkThemeManager::instance()->theme().metrics();
        const QFontMetrics fm(font());
        const int iconExtent =
            icon().isNull() ? 0 : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
        qreal width = metrics.spacing12 * 2.0;
        if (iconExtent > 0) {
            width += iconExtent;
        }
        if (!text().isEmpty()) {
            width += fm.horizontalAdvance(text()) + metrics.spacing4;
            if (iconExtent > 0) {
                width += metrics.spacing6;
            }
        }
        return QSize(qCeil(width), qCeil(metrics.controlHeightRegular));
    }

  protected:
    bool event(QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            m_keyboardFocusVisible = false;
            update();
            break;
        case QEvent::KeyPress:
            if (hasFocus()) {
                m_keyboardFocusVisible = true;
                update();
            }
            break;
        case QEvent::FocusIn:
            m_keyboardFocusVisible =
                VkStylePainter::showsKeyboardFocus(static_cast<QFocusEvent*>(event)->reason());
            update();
            break;
        case QEvent::FocusOut:
            m_keyboardFocusVisible = false;
            setPressed(false);
            break;
        default:
            break;
        }
        return QAbstractButton::event(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event)

        const VkTheme& theme = VkThemeManager::instance()->theme();
        const auto& colors = theme.colors();
        const auto& metrics = theme.metrics();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (m_pressProgress > 0.0) {
            const QColor pressFill = isChecked() ? colors.accentPressed : colors.controlFillPressed;
            const QColor overlay = VkStylePainter::multiplyAlpha(pressFill, m_pressProgress * 0.82);
            painter.setPen(Qt::NoPen);
            painter.setBrush(overlay);
            painter.drawRoundedRect(QRectF(rect()).adjusted(metrics.spacing2, metrics.spacing2,
                                                            -metrics.spacing2, -metrics.spacing2),
                                    metrics.cornerRadiusSmall, metrics.cornerRadiusSmall);
        }

        const int iconExtent =
            icon().isNull() ? 0 : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
        const QFontMetrics fm(font());
        const int gap = iconExtent > 0 && !text().isEmpty() ? qCeil(metrics.spacing6) : 0;
        QRect contentRect =
            rect().adjusted(qCeil(metrics.spacing8), 0, -qCeil(metrics.spacing8), 0);
        const int availableTextWidth = std::max(0, contentRect.width() - iconExtent - gap);
        const int textWidth =
            text().isEmpty() ? 0 : std::min(fm.horizontalAdvance(text()), availableTextWidth);
        const int combinedWidth = iconExtent + gap + textWidth;
        int x = contentRect.center().x() - combinedWidth / 2;
        QRect iconRect;
        QRect textRect;
        if (layoutDirection() == Qt::LeftToRight) {
            if (iconExtent > 0) {
                iconRect =
                    QRect(x, contentRect.center().y() - iconExtent / 2, iconExtent, iconExtent);
                x += iconExtent + gap;
            }
            textRect = QRect(x, contentRect.top(), textWidth, contentRect.height());
        } else {
            if (iconExtent > 0) {
                iconRect = QRect(x + textWidth + gap, contentRect.center().y() - iconExtent / 2,
                                 iconExtent, iconExtent);
            }
            textRect = QRect(x, contentRect.top(), textWidth, contentRect.height());
        }

        const QIcon::Mode iconMode =
            isChecked() ? QIcon::Selected : (!isEnabled() ? QIcon::Disabled : QIcon::Normal);
        const QIcon::State iconState = isChecked() ? QIcon::On : QIcon::Off;
        QColor foreground =
            isChecked() ? VkStylePainter::contrastingText(colors.accent) : colors.textPrimary;
        if (!isEnabled()) {
            foreground =
                isChecked() ? VkStylePainter::multiplyAlpha(foreground, 0.56) : colors.textDisabled;
        }
        if (!iconRect.isEmpty()) {
            painter.save();
            if (!isEnabled() && isChecked()) {
                painter.setOpacity(0.56);
            }
            icon().paint(&painter, iconRect, Qt::AlignCenter, iconMode, iconState);
            painter.restore();
        }

        painter.setFont(theme.typography().body);
        painter.setPen(foreground);
        const QString elided = fm.elidedText(text(), Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextShowMnemonic, elided);

        if (hasFocus() && m_keyboardFocusVisible) {
            VkStylePainter::drawFocusRing(
                painter,
                QRectF(rect()).adjusted(metrics.spacing2, metrics.spacing2, -metrics.spacing2,
                                        -metrics.spacing2),
                metrics.cornerRadiusSmall, VkStylePainter::neutralFocusColor(colors.borderStrong),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
    }

  private:
    void setPressed(bool pressed) {
        const qreal target = pressed ? 1.0 : 0.0;
        if (qFuzzyCompare(m_pressProgress + 1.0, target + 1.0)) {
            return;
        }
        m_pressProgress = target;
        update();
    }

    qreal m_pressProgress = 0.0;
    bool m_keyboardFocusVisible = false;
};

VkSegmentedControlPrivate::VkSegmentedControlPrivate(VkSegmentedControl* owner)
    : QObject(owner), q(owner), group(new QButtonGroup(owner)),
      indicatorAnimation(new VkWidgetAnimation(owner, this)) {
    group->setExclusive(true);
    QObject::connect(group, &QButtonGroup::idClicked, q, [this](int index) {
        q->setCurrentIndex(index);
        if (validIndex(index)) {
            buttons[index]->setFocus(Qt::MouseFocusReason);
            emit q->segmentActivated(index);
        }
    });
}

bool VkSegmentedControlPrivate::validIndex(int index) const noexcept {
    return index >= 0 && index < buttons.size();
}

void VkSegmentedControlPrivate::updateButtonIds() {
    for (int index = 0; index < buttons.size(); ++index) {
        group->setId(buttons[index], index);
    }
}

void VkSegmentedControlPrivate::relayout(bool animateIndicator) {
    if (buttons.isEmpty()) {
        indicatorAnimation->stop();
        indicatorRect = {};
        q->update();
        return;
    }

    const QRect area = q->contentsRect();
    const int count = static_cast<int>(buttons.size());
    const int baseWidth = area.width() / count;
    const int remainder = area.width() % count;
    int logicalX = area.left();
    for (int index = 0; index < count; ++index) {
        const int width = baseWidth + (index < remainder ? 1 : 0);
        const QRect logicalRect(logicalX, area.top(), width, area.height());
        buttons[index]->setGeometry(QStyle::visualRect(q->layoutDirection(), area, logicalRect));
        logicalX += width;
    }
    updateIndicator(animateIndicator);
}

void VkSegmentedControlPrivate::updateIndicator(bool animated) {
    if (!validIndex(currentIndex)) {
        indicatorAnimation->stop();
        indicatorRect = {};
        q->update();
        return;
    }

    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    const qreal inset = std::max<qreal>(metrics.borderWidth, metrics.spacing2);
    const QRectF target =
        QRectF(buttons[currentIndex]->geometry()).adjusted(inset, inset, -inset, -inset);
    if (!animated || indicatorRect.isEmpty()) {
        indicatorAnimation->stop();
        indicatorRect = target;
        q->update();
        return;
    }

    // Retargeting begins at the rendered rectangle, so repeated arrow presses stay continuous.
    const QRectF start = indicatorRect;
    indicatorAnimation->start(
        0.0, 1.0, VkMotionRole::StateTransition,
        [this, start, target](qreal progress) {
            indicatorRect = QRectF(start.topLeft() + (target.topLeft() - start.topLeft()) * progress,
                                   start.size() + (target.size() - start.size()) * progress);
            q->update();
        });
}

void VkSegmentedControlPrivate::moveSelection(int visualDelta, bool activate) {
    if (buttons.isEmpty() || visualDelta == 0) {
        return;
    }
    const int logicalDelta = q->layoutDirection() == Qt::RightToLeft ? -visualDelta : visualDelta;
    const int start = currentIndex >= 0
                          ? currentIndex
                          : (logicalDelta > 0 ? -1 : static_cast<int>(buttons.size()));
    const int next = nextEnabledIndex(start, logicalDelta);
    if (next < 0) {
        return;
    }
    q->setCurrentIndex(next);
    buttons[next]->setFocus(Qt::TabFocusReason);
    if (activate) {
        emit q->segmentActivated(next);
    }
}

int VkSegmentedControlPrivate::nextEnabledIndex(int start, int logicalDelta) const {
    int index = start + logicalDelta;
    while (validIndex(index)) {
        if (buttons[index]->isEnabled()) {
            return index;
        }
        index += logicalDelta;
    }
    return -1;
}

void VkSegmentedControlPrivate::updateFocusProxy() {
    QWidget* proxy = validIndex(currentIndex) && buttons[currentIndex]->isEnabled()
                         ? static_cast<QWidget*>(buttons[currentIndex])
                         : nullptr;
    if (!proxy) {
        for (VkSegmentButton* button : std::as_const(buttons)) {
            if (button->isEnabled()) {
                proxy = button;
                break;
            }
        }
    }
    q->setFocusProxy(proxy);
}

bool VkSegmentedControlPrivate::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right) {
            const int visualDelta = keyEvent->key() == Qt::Key_Right ? 1 : -1;
            moveSelection(visualDelta, true);
            event->accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

VkSegmentedControl::VkSegmentedControl(QWidget* parent)
    : QWidget(parent), d(std::make_unique<VkSegmentedControlPrivate>(this)) {
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFont(VkThemeManager::instance()->theme().typography().body);

    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this, [this](quint64) {
        for (VkSegmentButton* button : std::as_const(d->buttons)) {
            button->update();
        }
        update();
    });
}

VkSegmentedControl::~VkSegmentedControl() = default;

int VkSegmentedControl::count() const noexcept {
    return static_cast<int>(d->buttons.size());
}

int VkSegmentedControl::currentIndex() const noexcept {
    return d->currentIndex;
}

int VkSegmentedControl::addSegment(const QString& text) {
    return addSegment(QIcon{}, text);
}

int VkSegmentedControl::addSegment(const QIcon& icon, const QString& text) {
    return insertSegment(static_cast<int>(d->buttons.size()), icon, text);
}

int VkSegmentedControl::insertSegment(int index, const QIcon& icon, const QString& text) {
    if (index < 0 || index > d->buttons.size()) {
        index = static_cast<int>(d->buttons.size());
    }

    auto* button = new VkSegmentButton(this);
    button->setIcon(icon);
    button->setText(text);
    button->setProperty(explicitAccessibleNameProperty, false);
    updateAutomaticAccessibleName(button);
    button->installEventFilter(d.get());
    d->buttons.insert(index, button);
    d->group->addButton(button);
    d->updateButtonIds();

    if (d->currentIndex < 0) {
        d->currentIndex = 0;
        button = d->buttons.front();
        button->setChecked(true);
        emit currentIndexChanged(0);
    } else if (index <= d->currentIndex) {
        ++d->currentIndex;
        d->buttons[d->currentIndex]->setChecked(true);
        emit currentIndexChanged(d->currentIndex);
    }

    d->updateFocusProxy();
    d->relayout(false);
    updateGeometry();
    return index;
}

void VkSegmentedControl::removeSegment(int index) {
    if (!d->validIndex(index)) {
        return;
    }

    const int oldCurrent = d->currentIndex;
    VkSegmentButton* button = d->buttons.takeAt(index);
    d->group->removeButton(button);
    button->removeEventFilter(d.get());
    delete button;
    d->updateButtonIds();

    int newCurrent = oldCurrent;
    bool selectionChanged = false;
    if (d->buttons.isEmpty()) {
        newCurrent = -1;
        selectionChanged = oldCurrent != -1;
    } else if (index == oldCurrent) {
        newCurrent = std::min(index, static_cast<int>(d->buttons.size()) - 1);
        selectionChanged = true;
    } else if (index < oldCurrent) {
        newCurrent = oldCurrent - 1;
        selectionChanged = true;
    }

    d->currentIndex = newCurrent;
    if (d->validIndex(newCurrent)) {
        d->buttons[newCurrent]->setChecked(true);
    }
    if (selectionChanged) {
        emit currentIndexChanged(newCurrent);
    }

    d->updateFocusProxy();
    d->relayout(false);
    updateGeometry();
}

void VkSegmentedControl::clear() {
    if (d->buttons.isEmpty()) {
        return;
    }
    for (VkSegmentButton* button : std::as_const(d->buttons)) {
        d->group->removeButton(button);
        button->removeEventFilter(d.get());
        delete button;
    }
    d->buttons.clear();
    d->currentIndex = -1;
    d->indicatorAnimation->stop();
    d->indicatorRect = {};
    setFocusProxy(nullptr);
    updateGeometry();
    update();
    emit currentIndexChanged(-1);
}

QString VkSegmentedControl::segmentText(int index) const {
    return d->validIndex(index) ? d->buttons[index]->text() : QString{};
}

void VkSegmentedControl::setSegmentText(int index, const QString& text) {
    if (!d->validIndex(index) || d->buttons[index]->text() == text) {
        return;
    }
    d->buttons[index]->setText(text);
    updateAutomaticAccessibleName(d->buttons[index]);
    updateGeometry();
    d->relayout(false);
}

QIcon VkSegmentedControl::segmentIcon(int index) const {
    return d->validIndex(index) ? d->buttons[index]->icon() : QIcon{};
}

void VkSegmentedControl::setSegmentIcon(int index, const QIcon& icon) {
    if (!d->validIndex(index) || d->buttons[index]->icon().cacheKey() == icon.cacheKey()) {
        return;
    }
    d->buttons[index]->setIcon(icon);
    updateAutomaticAccessibleName(d->buttons[index]);
    updateGeometry();
    d->relayout(false);
}

bool VkSegmentedControl::isSegmentEnabled(int index) const {
    return d->validIndex(index) && d->buttons[index]->isEnabled();
}

void VkSegmentedControl::setSegmentEnabled(int index, bool enabled) {
    if (!d->validIndex(index) || d->buttons[index]->isEnabled() == enabled) {
        return;
    }
    d->buttons[index]->setEnabled(enabled);
    d->updateFocusProxy();
    update();
}

QString VkSegmentedControl::segmentAccessibleName(int index) const {
    return d->validIndex(index) ? d->buttons[index]->accessibleName() : QString{};
}

void VkSegmentedControl::setSegmentAccessibleName(int index, const QString& name) {
    if (!d->validIndex(index)) {
        return;
    }
    VkSegmentButton* button = d->buttons[index];
    button->setProperty(explicitAccessibleNameProperty, !name.isEmpty());
    if (name.isEmpty()) {
        updateAutomaticAccessibleName(button);
    } else {
        button->setAccessibleName(name);
    }
}

QSize VkSegmentedControl::sizeHint() const {
    if (d->buttons.isEmpty()) {
        const auto& metrics = VkThemeManager::instance()->theme().metrics();
        return QSize(0, qCeil(metrics.controlHeightRegular));
    }
    int segmentWidth = 0;
    int height = 0;
    for (const VkSegmentButton* button : d->buttons) {
        const QSize buttonHint = button->sizeHint();
        segmentWidth = std::max(segmentWidth, buttonHint.width());
        height = std::max(height, buttonHint.height());
    }
    return QSize(segmentWidth * static_cast<int>(d->buttons.size()), height);
}

QSize VkSegmentedControl::minimumSizeHint() const {
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    return QSize(static_cast<int>(d->buttons.size()) * qCeil(metrics.controlHeightSmall),
                 qCeil(metrics.controlHeightRegular));
}

void VkSegmentedControl::setCurrentIndex(int index) {
    if (index < -1 || index >= d->buttons.size() || index == d->currentIndex) {
        return;
    }

    const int previous = d->currentIndex;
    if (index < 0) {
        d->group->setExclusive(false);
        if (d->validIndex(previous)) {
            d->buttons[previous]->setChecked(false);
        }
        d->group->setExclusive(true);
    } else {
        d->buttons[index]->setChecked(true);
    }
    d->currentIndex = index;
    d->updateFocusProxy();
    d->updateIndicator(true);
    emit currentIndexChanged(index);
}

void VkSegmentedControl::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    VkStylePainter::drawRoundedPanel(painter, QRectF(rect()), metrics.cornerRadiusRegular,
                                     isEnabled() ? colors.controlFill : colors.controlFillDisabled,
                                     colors.border, metrics.borderWidth);

    if (!d->indicatorRect.isEmpty()) {
        painter.fillPath(
            VkStylePainter::roundedRectPath(d->indicatorRect, metrics.cornerRadiusSmall),
            colors.accent);
    }

    for (int index = 0; index + 1 < d->buttons.size(); ++index) {
        if (index == d->currentIndex || index + 1 == d->currentIndex) {
            continue;
        }
        const QRect leftGeometry = d->buttons[index]->geometry();
        const QRect rightGeometry = d->buttons[index + 1]->geometry();
        const qreal x = (leftGeometry.center().x() + rightGeometry.center().x()) * 0.5;
        VkStylePainter::drawHairline(painter, QPointF(x, rect().top() + metrics.spacing6),
                                     QPointF(x, rect().bottom() - metrics.spacing6),
                                     colors.separator);
    }
}

void VkSegmentedControl::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d->relayout(false);
}

void VkSegmentedControl::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::LayoutDirectionChange:
    case QEvent::FontChange:
    case QEvent::StyleChange:
        for (VkSegmentButton* button : std::as_const(d->buttons)) {
            button->setFont(font());
        }
        updateGeometry();
        d->relayout(false);
        update();
        break;
    case QEvent::EnabledChange:
        update();
        break;
    default:
        break;
    }
}

void VkSegmentedControl::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (d->validIndex(d->currentIndex)) {
        d->buttons[d->currentIndex]->setFocus(event->reason());
    }
}

void VkSegmentedControl::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        d->moveSelection(event->key() == Qt::Key_Right ? 1 : -1, true);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace vkui
