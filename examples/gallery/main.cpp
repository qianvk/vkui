// SPDX-License-Identifier: MIT

#include "GalleryWindow.h"

#include <QApplication>
#include <QLocale>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vkui Gallery"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("vkui"));
    QApplication::setLayoutDirection(QLocale::system().textDirection());

    vkui::installVkUi(application);

    GalleryWindow window;
    window.resize(1120, 760);
    window.show();

    return application.exec();
}
