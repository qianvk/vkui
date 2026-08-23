// SPDX-License-Identifier: MIT

#include "IconChosenWindow.h"

#include <QApplication>
#include <QLocale>
#include <vkui/Widgets.h>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("icon-chosen"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("vkui"));
    QApplication::setLayoutDirection(QLocale::system().textDirection());
    vkui::installVkUi(application);

    IconChosenWindow window;
    window.show();
    return application.exec();
}
