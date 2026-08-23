// SPDX-License-Identifier: MIT

#include "IconCatalogModel.h"
#include "IconSelectionStore.h"
#include "NerdSymbolIcon.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <vkui/core/VkIcon.h>

namespace {

QRect visibleBounds(const QImage& image) {
    QRect result;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() > 8) {
                result |= QRect(x, y, 1, 1);
            }
        }
    }
    return result;
}

qreal visibleAspectRatio(const QIcon& icon) {
    const QRect bounds = visibleBounds(icon.pixmap(QSize(480, 480)).toImage());
    return bounds.isEmpty() ? 0.0
                            : static_cast<qreal>(bounds.width()) / bounds.height();
}

} // namespace

class IconChosenTest final : public QObject {
    Q_OBJECT

  private slots:
    void catalogRenderingPreservesFolderAspectRatios();
    void selectedCatalogSymbolsMatchVkUiResources();
    void selectionManifestRoundTrips();
    void legacyStringManifestLoads();
    void malformedManifestIsRejected();
};

void IconChosenTest::catalogRenderingPreservesFolderAspectRatios() {
    const qreal chosenClosed =
        visibleAspectRatio(nerdSymbolIcon(QStringLiteral("symbols-009.svg"),
                                          QStringLiteral("i119")));
    const qreal chosenOpen =
        visibleAspectRatio(nerdSymbolIcon(QStringLiteral("symbols-009.svg"),
                                          QStringLiteral("i120")));
    const qreal galleryClosed =
        visibleAspectRatio(vkui::icon(vkui::VkSymbol::FileFolderClosed, QColor(Qt::black)));
    const qreal galleryOpen =
        visibleAspectRatio(vkui::icon(vkui::VkSymbol::FileFolderOpen, QColor(Qt::black)));

    QVERIFY(chosenClosed > 1.1);
    QVERIFY(chosenOpen > 1.2);
    QVERIFY(std::abs(chosenClosed - galleryClosed) < 0.02);
    QVERIFY(std::abs(chosenOpen - galleryOpen) < 0.02);
}

void IconChosenTest::selectedCatalogSymbolsMatchVkUiResources() {
    struct Mapping final {
        const char* pack;
        const char* element;
        vkui::VkSymbol symbol;
    };
    const QList<Mapping> mappings{
        {"symbols-009.svg", "i15", vkui::VkSymbol::GearFilled},
        {"symbols-009.svg", "i119", vkui::VkSymbol::FileFolderClosed},
        {"symbols-009.svg", "i120", vkui::VkSymbol::FileFolderOpen},
        {"symbols-010.svg", "i87", vkui::VkSymbol::FileGeneric},
        {"symbols-013.svg", "i233", vkui::VkSymbol::CloudFilled},
        {"symbols-015.svg", "i10", vkui::VkSymbol::CloseCircleFilled},
    };

    for (const Mapping& mapping : mappings) {
        const QImage catalog = nerdSymbolIcon(QString::fromLatin1(mapping.pack),
                                               QString::fromLatin1(mapping.element))
                                   .pixmap(QSize(192, 192))
                                   .toImage();
        const QImage promoted =
            vkui::icon(mapping.symbol, QColor(Qt::black)).pixmap(QSize(192, 192)).toImage();
        QCOMPARE(visibleBounds(catalog), visibleBounds(promoted));
    }
}

void IconChosenTest::selectionManifestRoundTrips() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("selection.json"));
    const QList<IconCatalogEntry> entries{
        {QStringLiteral("fa-folder"), QStringLiteral("F07B"),
         QStringLiteral("symbols-009.svg"), QStringLiteral("i119"), true},
        {QStringLiteral("fa-folder_open"), QStringLiteral("F07C"),
         QStringLiteral("symbols-009.svg"), QStringLiteral("i120"), true},
    };

    QVERIFY(IconSelectionStore::save(path, entries));
    QString error;
    const auto loaded = IconSelectionStore::load(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded,
             QSet<QString>({QStringLiteral("fa-folder"), QStringLiteral("fa-folder_open")}));
}

void IconChosenTest::legacyStringManifestLoads() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("legacy.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(R"({"symbols":["fa-folder","fa-folder_open"]})") > 0);
    file.close();

    QString error;
    const auto loaded = IconSelectionStore::load(file.fileName(), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->size(), 2);
}

void IconChosenTest::malformedManifestIsRejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("invalid.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(R"({"formatVersion":1,"symbols":[{}]})") > 0);
    file.close();

    QString error;
    QVERIFY(!IconSelectionStore::load(file.fileName(), &error).has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(IconChosenTest)

#include "tst_iconchosen.moc"
