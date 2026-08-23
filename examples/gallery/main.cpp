// SPDX-License-Identifier: MIT

#include "GalleryApplicationController.h"

#include <QApplication>
#include <QLocale>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    // Frameless native windows require this before QApplication creates any
    // native widget siblings.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vkui Gallery"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("vkui"));
    QApplication::setLayoutDirection(QLocale::system().textDirection());

    vkui::installVkUi(application);

    GalleryApplicationController controller;
    controller.showMainWindow();

    return application.exec();
}
