// SPDX-License-Identifier: MIT

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QtMath>
#include <vkui/widgets/controls/VSlider.h>

namespace vkui {

class VSliderPrivate final {
  public:
    qreal pressAxisPosition{0.0};
    int pressSliderPosition{0};
    int movementSpan{1};
    bool upsideDown{false};
    bool anchoredHandleDrag{false};
};

namespace {
qreal axisPosition(const QMouseEvent& event, Qt::Orientation orientation) {
    return orientation == Qt::Horizontal ? event.position().x() : event.position().y();
}
} // namespace

VSlider::VSlider(QWidget* parent) : VSlider(Qt::Vertical, parent) {}

VSlider::VSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent), d(std::make_unique<VSliderPrivate>()) {}

VSlider::~VSlider() = default;

void VSlider::mousePressEvent(QMouseEvent* event) {
    QStyleOptionSlider option;
    initStyleOption(&option);
    const QStyle::SubControl hitControl =
        style()->hitTestComplexControl(QStyle::CC_Slider, &option,
                                       event->position().toPoint(), this);
    const bool handlePress = event->button() == Qt::LeftButton &&
                             hitControl == QStyle::SC_SliderHandle;

    const int pressSliderPosition = sliderPosition();
    const qreal pressAxisPosition = axisPosition(*event, orientation());
    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    const int movementSpan = orientation() == Qt::Horizontal
                                 ? groove.width() - handle.width()
                                 : groove.height() - handle.height();

    QSlider::mousePressEvent(event);
    d->anchoredHandleDrag = handlePress && isSliderDown();
    if (!d->anchoredHandleDrag) {
        return;
    }

    d->pressAxisPosition = pressAxisPosition;
    d->pressSliderPosition = pressSliderPosition;
    d->movementSpan = qMax(1, movementSpan);
    d->upsideDown = option.upsideDown;
    setSliderPosition(pressSliderPosition);
}

void VSlider::mouseMoveEvent(QMouseEvent* event) {
    if (!d->anchoredHandleDrag || !isSliderDown()) {
        QSlider::mouseMoveEvent(event);
        return;
    }

    qreal pixelDelta = axisPosition(*event, orientation()) - d->pressAxisPosition;
    if (d->upsideDown) {
        pixelDelta = -pixelDelta;
    }
    const qint64 valueRange = static_cast<qint64>(maximum()) - minimum();
    const qint64 valueDelta =
        qRound64(pixelDelta * static_cast<qreal>(valueRange) / d->movementSpan);
    const qint64 target =
        qBound(static_cast<qint64>(minimum()),
               static_cast<qint64>(d->pressSliderPosition) + valueDelta,
               static_cast<qint64>(maximum()));
    setSliderPosition(static_cast<int>(target));
    event->accept();
}

void VSlider::mouseReleaseEvent(QMouseEvent* event) {
    d->anchoredHandleDrag = false;
    QSlider::mouseReleaseEvent(event);
}

} // namespace vkui
