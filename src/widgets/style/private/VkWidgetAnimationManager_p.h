// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QObject>
#include <array>

class QVariantAnimation;
class QWidget;

namespace vkui {

class VkWidgetAnimationManager final : public QObject {
  public:
    enum class Channel : std::size_t {
        Hover = 0,
        Press,
        Focus,
        Selection,
        Count,
    };

    struct Progress {
        qreal hover = 0.0;
        qreal press = 0.0;
        qreal focus = 0.0;
        qreal selection = 0.0;
    };

    explicit VkWidgetAnimationManager(QObject* parent = nullptr);
    ~VkWidgetAnimationManager() override;

    void watch(QWidget* widget);
    void unwatch(QWidget* widget);

    Progress progress(QWidget* widget, bool hovered, bool pressed, bool focused, bool selected);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct WidgetState;

    WidgetState* ensureState(QWidget* widget);
    void setTarget(QWidget* widget, Channel channel, qreal target);
    void removeState(QWidget* widget);
    void settleAnimations();

    QHash<QWidget*, WidgetState*> m_states;
};

} // namespace vkui
