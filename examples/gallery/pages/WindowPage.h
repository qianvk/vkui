// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class QLabel;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
class QSpinBox;
#endif

namespace vkui {
class VWindowAgent;
}

/** Cross-platform gallery examples for native windows and dialog composition. */
class WindowPage final : public QWidget {
    Q_OBJECT

  public:
    explicit WindowPage(vkui::VWindowAgent& windowAgent, QWidget* parent = nullptr);

  signals:
    void preferencesRequested();

  private:
    void showConfirmWindow();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    void applyTrafficLightOrigin();
#endif

    vkui::VWindowAgent& windowAgent_;
    QLabel* confirmResult_ = nullptr;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QSpinBox* horizontalOrigin_ = nullptr;
    QSpinBox* verticalOrigin_ = nullptr;
#endif
};
