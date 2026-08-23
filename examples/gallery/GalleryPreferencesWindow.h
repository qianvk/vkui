// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>
#include <vkui/window/VWindowAgent.h>

class QCheckBox;
class QEvent;
class QGroupBox;
class QLabel;
class QSlider;
class QWidget;

namespace vkui {
class VCombobox;
}

/** Application-owned, lazily created Preferences window. */
class GalleryPreferencesWindow final : public QWidget {
    Q_OBJECT

  public:
    GalleryPreferencesWindow();

    void showForHost(const QWidget* host);

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void buildUi();
    void configureWindowChrome();
    void positionForHost(const QWidget* host);
    void retranslateUi();

    QWidget* titleBar_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QWidget* content_ = nullptr;
    QGroupBox* generalGroup_ = nullptr;
    QLabel* appearanceLabel_ = nullptr;
    vkui::VCombobox* appearanceBox_ = nullptr;
    QCheckBox* animationsBox_ = nullptr;
    QLabel* scaleLabel_ = nullptr;
    QSlider* scaleSlider_ = nullptr;
    QLabel* scaleValue_ = nullptr;
    QLabel* noteLabel_ = nullptr;
    // Declared last so native teardown precedes QWidget base destruction.
    vkui::VWindowAgent windowAgent_;
};
