// SPDX-License-Identifier: MIT

#include <QApplication>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    vkui::installVkUi(application);
    vkui::VkSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.setCurrentIndex(1);
    return control.currentIndex() == 1 ? 0 : 1;
}
