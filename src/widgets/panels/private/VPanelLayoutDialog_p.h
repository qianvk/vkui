// SPDX-License-Identifier: MIT

#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <functional>
#include <vkui/widgets/panels/VPanelManager.h>

class QEvent;
class QPaintEvent;

namespace vkui {

class VPanelLayoutCanvas;

/** Private edge-to-edge view for one immutable manager snapshot. */
class VPanelLayoutDialog final : public QDialog {
  public:
    explicit VPanelLayoutDialog(QWidget* owner, std::function<void(const QString&)> togglePanel);
    ~VPanelLayoutDialog() override;

    void setPanelStates(const QList<VPanelState>& states);
    void present();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QPointer<QWidget> owner_;
    VPanelLayoutCanvas* canvas_ = nullptr;
};

} // namespace vkui
