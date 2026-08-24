// SPDX-License-Identifier: MIT

#include <QApplication>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    vkui::installVkUi(application);
    vkui::VSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.setCurrentIndex(1);
    QWidget source;
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface glass;
    glass.setBackdrop(&backdrop);
    return control.currentIndex() == 1 && glass.backdrop() == &backdrop ? 0 : 1;
}
