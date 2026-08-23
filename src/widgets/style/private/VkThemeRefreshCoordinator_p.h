// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <vkui/core/VkThemeChange.h>

class QApplication;

namespace vkui {

class VkThemeRefreshCoordinator final : public QObject {
  public:
    explicit VkThemeRefreshCoordinator(QApplication& application);

  private:
    void scheduleRefresh(VkThemeChanges changes);
    void refreshWidgets();

    QPointer<QApplication> application_;
    VkThemeChanges pendingChanges_;
    bool refreshPending_ = false;
};

} // namespace vkui
