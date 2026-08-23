// SPDX-License-Identifier: MIT

#include "IconsPage.h"

#include <QLineEdit>
#include <QToolButton>
#include <QtTest>

class IconsPageTest final : public QObject {
    Q_OBJECT

  private slots:
    void chosenIconSearchFiltersBySemanticAndSourceName();
};

void IconsPageTest::chosenIconSearchFiltersBySemanticAndSourceName() {
    IconsPage page;
    auto* search = page.findChild<QLineEdit*>(QStringLiteral("chosenIconSearch"));
    const QList<QToolButton*> cards =
        page.findChildren<QToolButton*>(QStringLiteral("chosenIconCard"));
    QVERIFY(search != nullptr);
    QCOMPARE(cards.size(), 6);

    const auto visibleSources = [&cards] {
        QStringList result;
        for (const QToolButton* card : cards) {
            if (!card->isHidden()) {
                result.append(card->property("sourceName").toString());
            }
        }
        result.sort();
        return result;
    };

    search->setText(QStringLiteral("icloud"));
    QCOMPARE(visibleSources(), QStringList({QStringLiteral("md-apple_icloud")}));

    search->setText(QStringLiteral("folder"));
    QCOMPARE(visibleSources(),
             QStringList({QStringLiteral("fa-folder"), QStringLiteral("fa-folder_open")}));

    search->setText(QStringLiteral("not-in-the-selection"));
    QVERIFY(visibleSources().isEmpty());

    search->clear();
    QCOMPARE(visibleSources().size(), 6);
}

QTEST_MAIN(IconsPageTest)

#include "tst_iconspage.moc"
