// SPDX-License-Identifier: MIT

#include <QtCore/QFile>
#include <QtWidgets/QApplication>
#include <vkui/widgets/style/VkStyleSheet.h>

namespace vkui {

bool applyStyleSheetFile(QApplication& application, const QString& filePath,
                         QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QString styleSheet = QString::fromUtf8(file.readAll());
    if (application.styleSheet() != styleSheet) {
        application.setStyleSheet(styleSheet);
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void clearStyleSheet(QApplication& application) {
    if (!application.styleSheet().isEmpty()) {
        application.setStyleSheet({});
    }
}

} // namespace vkui
