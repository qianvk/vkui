// SPDX-License-Identifier: MIT

#include <QGuiApplication>
#include <vkui/Core.h>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    auto* manager = vkui::VkThemeManager::instance();
    manager->setLiquidGlassTintLevel(37);
    const vkui::VkTheme& theme = manager->theme();
    return theme.colors().textPrimary.isValid() && manager->liquidGlassTintLevel() == 37 ? 0 : 1;
}
