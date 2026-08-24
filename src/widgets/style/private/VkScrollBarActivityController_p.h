// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QBasicTimer>
#include <QtCore/QEasingCurve>
#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>

class QScrollBar;
class QTimerEvent;

namespace vkui {

/** Tracks transient scrollbar visibility without replacing QScrollBar instances. */
class VkScrollBarActivityController final : public QObject {
  public:
    explicit VkScrollBarActivityController(QObject* parent);
    ~VkScrollBarActivityController() override;

    void polish(QScrollBar* scrollBar);
    void unpolish(QScrollBar* scrollBar);
    [[nodiscard]] qreal opacity(const QScrollBar* scrollBar) const noexcept;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

  private:
    struct State final {
        qreal opacity = 0.0;
        qreal transitionStartOpacity = 0.0;
        qreal transitionTargetOpacity = 0.0;
        qint64 transitionStartMs = 0;
        qint64 holdUntilMs = 0;
        int transitionDurationMs = 0;
        QEasingCurve transitionEasing;
        bool hovered = false;
        bool dragging = false;
    };

    [[nodiscard]] qint64 nowMs() const noexcept;
    void reveal(QScrollBar& scrollBar, bool transientActivity);
    void scheduleHide(QScrollBar& scrollBar);
    void startTransition(QScrollBar& scrollBar, State& state, qreal targetOpacity);
    void ensureTimerRunning();

    QHash<QScrollBar*, State> states_;
    QElapsedTimer clock_;
    QBasicTimer timer_;
};

} // namespace vkui
