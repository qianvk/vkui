// SPDX-License-Identifier: MIT

#include <QGuiApplication>
#include <vkui/Core.h>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    const vkui::VkTheme& theme = vkui::VkThemeManager::instance()->theme();
    return theme.colors().textPrimary.isValid() ? 0 : 1;
}
