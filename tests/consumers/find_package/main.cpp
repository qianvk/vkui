// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QCheckBox>
#include <QWidget>
#include <vkui/Widgets.h>
#include <vkui/Window.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    vkui::installVkUi(application);

    QWidget host;
    QWidget firstTitleBar(&host);
    QWidget secondTitleBar(&host);
    vkui::VkWindowAgent windowAgent;
    if (!windowAgent.setup(&host) || !windowAgent.addTitleBar(&firstTitleBar) ||
        !windowAgent.addTitleBar(&secondTitleBar)) {
        return 1;
    }

    vkui::VkSwitch control;
    control.setChecked(true);
    QCheckBox checkBox;
    vkui::setControlSize(checkBox, vkui::VkControlSize::Large);
    vkui::setControlExtent(checkBox, 27);
    return windowAgent.titleBars().size() == 2 && control.isChecked() &&
                   vkui::controlSize(checkBox) == vkui::VkControlSize::Large &&
                   vkui::controlExtent(checkBox) == 27
               ? 0
               : 1;
}
