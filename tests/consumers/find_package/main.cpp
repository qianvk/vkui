// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QCheckBox>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    vkui::installVkUi(application);
    vkui::VkSwitch control;
    control.setChecked(true);
    QCheckBox checkBox;
    vkui::setControlSize(checkBox, vkui::VkControlSize::Large);
    vkui::setControlExtent(checkBox, 27);
    return control.isChecked() && vkui::controlSize(checkBox) == vkui::VkControlSize::Large &&
                   vkui::controlExtent(checkBox) == 27
               ? 0
               : 1;
}
