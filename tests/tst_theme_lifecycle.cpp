// SPDX-License-Identifier: MIT

#include <QApplication>
#include <vkui/core/VkThemeManager.h>

int main(int argc, char* argv[]) {
    vkui::VkThemeManager* const beforeApplication = vkui::VkThemeManager::instance();
    beforeApplication->setAppearance(vkui::VkAppearance::Light);

    QApplication application(argc, argv);
    vkui::VkThemeManager* const afterApplication = vkui::VkThemeManager::instance();

    if (afterApplication != beforeApplication) {
        return 1;
    }
    if (afterApplication->parent() != &application) {
        return 2;
    }
    if (afterApplication->effectiveAppearance() != vkui::VkAppearance::Light) {
        return 3;
    }
    return 0;
}
