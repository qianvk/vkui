// SPDX-License-Identifier: MIT

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QImage>
#include <QSignalSpy>
#include <QSplitterHandle>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QtTest>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vkui/Widgets.h>
#include <vkui/core/VkThemeManager.h>

class PanelManagerTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void ownsWindowScopedStateAcrossWidgetRebinding();
    void alwaysKeepsOneLivePanelExpanded();
    void managedHandleOpensChooser();
    void leftWindowEdgeHandleOwnsCollapsedSplitterOverlap();
};

void PanelManagerTest::initTestCase() {
    vkui::installVkUi(*qApp);
}

void PanelManagerTest::ownsWindowScopedStateAcrossWidgetRebinding() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    vkui::VPanelManager manager(window);

    auto createLayout = [&window, layout]() {
        auto* splitter = new vkui::VSplitter(Qt::Horizontal, &window);
        splitter->setChildrenCollapsible(true);
        auto* first = new QWidget(splitter);
        auto* second = new QWidget(splitter);
        splitter->addWidget(first);
        splitter->addWidget(second);
        splitter->setSizes({240, 560});
        layout->addWidget(splitter);
        return std::tuple{splitter, first, second};
    };

    auto [splitter, first, second] = createLayout();
    QVERIFY(manager.setLayoutRoot(splitter));
    QVERIFY(manager.registerPanel(QStringLiteral("first"), QStringLiteral("First"), first, 1));
    QVERIFY(manager.registerPanel(QStringLiteral("second"), QStringLiteral("Second"), second, 2));
    window.resize(800, 500);
    window.show();
    QCoreApplication::processEvents();
    const QList<int> originalSizes = splitter->sizes();
    const qreal originalRatio = static_cast<qreal>(originalSizes.constFirst()) /
                                std::accumulate(originalSizes.cbegin(), originalSizes.cend(), 0);

    QVERIFY(manager.setPanelExpanded(QStringLiteral("first"), false));
    QCOMPARE(splitter->sizes().constFirst(), 0);
    QVERIFY(!manager.isPanelExpanded(QStringLiteral("first")));

    QVERIFY(manager.setLayoutRoot(nullptr));
    delete splitter;
    auto [replacementSplitter, replacementFirst, replacementSecond] = createLayout();
    QVERIFY(manager.setLayoutRoot(replacementSplitter));
    QVERIFY(manager.registerPanel(QStringLiteral("first"), QStringLiteral("First"),
                                  replacementFirst, 1));
    QVERIFY(manager.registerPanel(QStringLiteral("second"), QStringLiteral("Second"),
                                  replacementSecond, 2));
    QCoreApplication::processEvents();

    QVERIFY(!manager.isPanelExpanded(QStringLiteral("first")));
    QCOMPARE(replacementSplitter->sizes().constFirst(), 0);
    QCOMPARE(manager.panelStates().size(), 2);
    QCOMPARE(manager.panelStates().constFirst().number, 1);

    QVERIFY(manager.setPanelExpanded(QStringLiteral("first"), true));
    const QList<int> restoredSizes = replacementSplitter->sizes();
    const qreal restoredRatio = static_cast<qreal>(restoredSizes.constFirst()) /
                                std::accumulate(restoredSizes.cbegin(), restoredSizes.cend(), 0);
    QVERIFY(std::abs(restoredRatio - originalRatio) < 0.02);
}

void PanelManagerTest::alwaysKeepsOneLivePanelExpanded() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* splitter = new vkui::VSplitter(Qt::Horizontal, &window);
    layout->addWidget(splitter);

    vkui::VPanelManager manager(window);
    QVERIFY(manager.setLayoutRoot(splitter));
    for (int index = 0; index < 3; ++index) {
        auto* panel = new QWidget(splitter);
        splitter->addWidget(panel);
        const QString id = QStringLiteral("panel-%1").arg(index + 1);
        QVERIFY(manager.registerPanel(id, id, panel, index + 1));
    }
    window.resize(900, 500);
    window.show();
    QCoreApplication::processEvents();

    const auto expandedCount = [&manager] {
        const QList<vkui::VPanelState> states = manager.panelStates();
        return std::count_if(states.cbegin(), states.cend(),
                             [](const vkui::VPanelState& state) { return state.expanded; });
    };

    QVERIFY(manager.setPanelExpanded(QStringLiteral("panel-1"), false));
    QVERIFY(manager.setPanelExpanded(QStringLiteral("panel-2"), false));
    QCOMPARE(expandedCount(), 1);

    // Collapsing the last visible panel atomically expands the next live panel.
    QVERIFY(manager.setPanelExpanded(QStringLiteral("panel-3"), false));
    QVERIFY(manager.isPanelExpanded(QStringLiteral("panel-1")));
    QVERIFY(!manager.isPanelExpanded(QStringLiteral("panel-3")));
    QCOMPARE(expandedCount(), 1);

    // Removing the sole visible panel restores another registered panel immediately.
    QVERIFY(manager.unregisterPanel(QStringLiteral("panel-1")));
    QVERIFY(manager.isPanelExpanded(QStringLiteral("panel-2")));
    QCOMPARE(expandedCount(), 1);

    QVERIFY(manager.unregisterPanel(QStringLiteral("panel-2")));
    QVERIFY(manager.isPanelExpanded(QStringLiteral("panel-3")));
    QCOMPARE(expandedCount(), 1);
    QVERIFY(!manager.setPanelExpanded(QStringLiteral("panel-3"), false));
    QVERIFY(manager.isPanelExpanded(QStringLiteral("panel-3")));
}

void PanelManagerTest::managedHandleOpensChooser() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* splitter = new vkui::VSplitter(Qt::Horizontal, &window);
    splitter->addWidget(new QWidget(splitter));
    splitter->addWidget(new QWidget(splitter));
    layout->addWidget(splitter);

    vkui::VPanelManager manager(window);
    QVERIFY(manager.setLayoutRoot(splitter));
    QVERIFY(manager.registerPanel(QStringLiteral("first"), QStringLiteral("First"),
                                  splitter->widget(0), 1));
    QVERIFY(manager.registerPanel(QStringLiteral("second"), QStringLiteral("Second"),
                                  splitter->widget(1), 2));
    window.resize(800, 500);
    window.show();
    QCoreApplication::processEvents();

    QSplitterHandle* handle = splitter->handle(1);
    QVERIFY(handle != nullptr);
    QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier, handle->rect().center());
    QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) != nullptr);
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog"));
    QCOMPARE(dialog->windowType(), Qt::Popup);
    QCOMPARE(QApplication::activePopupWidget(), dialog);

    const QPoint outsidePosition(5, 5);
    QCursor::setPos(window.mapToGlobal(outsidePosition));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, outsidePosition);
    QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) == nullptr);

    QTest::mouseClick(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
    QTRY_COMPARE(splitter->sizes().constFirst(), 0);
    QVERIFY(!manager.isPanelExpanded(QStringLiteral("first")));
    QVERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) == nullptr);
}

void PanelManagerTest::leftWindowEdgeHandleOwnsCollapsedSplitterOverlap() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new vkui::VSplitter(Qt::Horizontal, &window);
    splitter->addWidget(new QWidget(splitter));
    splitter->addWidget(new QWidget(splitter));
    layout->addWidget(splitter);

    vkui::VPanelManager manager(window);
    QVERIFY(manager.setLayoutRoot(splitter));
    QVERIFY(manager.registerPanel(QStringLiteral("first"), QStringLiteral("First"),
                                  splitter->widget(0), 1));
    QVERIFY(manager.registerPanel(QStringLiteral("second"), QStringLiteral("Second"),
                                  splitter->widget(1), 2));
    window.resize(800, 500);
    window.show();
    QCoreApplication::processEvents();

    auto* left = window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_left"),
                                                    Qt::FindDirectChildrenOnly);
    QVERIFY(left != nullptr);
    QCOMPARE(manager.windowEdgeHandles(), QList<QWidget*>{left});
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_top"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_bottom"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_right"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QCOMPARE(left->geometry().left(), window.rect().left());
    QCOMPARE(left->geometry().top(), window.rect().top());
    QCOMPARE(left->geometry().bottom(), window.rect().bottom());
    QCOMPARE(left->width(), 20);

    QSplitterHandle* splitterHandle = splitter->handle(1);
    QVERIFY(splitterHandle != nullptr);

    QImage image(left->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    left->render(&image);
    QCOMPARE(left->width(), 20);
    const QPoint pointer(10, left->height() / 3);
    const QPoint trackCenter(3, pointer.y());
    const QColor restingColor = image.pixelColor(trackCenter);

    QTest::mouseMove(left, pointer);
    QTest::qWait(180);
    image.fill(Qt::transparent);
    left->render(&image);
    const QColor edgeColor = image.pixelColor(trackCenter);
    QVERIFY(edgeColor != restingColor);
    QCOMPARE(edgeColor.rgb(), vkui::VkThemeManager::instance()->theme().colors().accent.rgb());

    QVERIFY(manager.setPanelExpanded(QStringLiteral("first"), false));
    QCOMPARE(splitter->sizes().constFirst(), 0);
    QTest::mouseMove(left, pointer);
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::widgetAt(left->mapToGlobal(pointer)), left);
    QTRY_COMPARE(window.windowHandle()->cursor().shape(), Qt::ArrowCursor);

    QImage splitterImage(splitterHandle->size(), QImage::Format_ARGB32_Premultiplied);
    splitterImage.fill(Qt::transparent);
    splitterHandle->render(&splitterImage);
    const QRgb accent = vkui::VkThemeManager::instance()->theme().colors().accent.rgb();
    bool hasFullHeightAccent = false;
    const int separatorX = splitterHandle->rect().center().x();
    for (int y = 0; y < splitterImage.height(); ++y) {
        if (splitterImage.pixelColor(separatorX, y).rgb() == accent) {
            hasFullHeightAccent = true;
            break;
        }
    }
    QVERIFY(!hasFullHeightAccent);

    QTest::mouseClick(left, Qt::LeftButton, Qt::NoModifier, pointer);
    QTRY_VERIFY(manager.isPanelExpanded(QStringLiteral("first")));
    QVERIFY(splitter->sizes().constFirst() > 0);
    QVERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) == nullptr);

    QVERIFY(manager.setPanelExpanded(QStringLiteral("second"), false));
    QCOMPARE(splitter->sizes().constLast(), 0);
    QTest::mouseClick(left, Qt::LeftButton, Qt::NoModifier, pointer);
    QTRY_VERIFY(manager.isPanelExpanded(QStringLiteral("second")));
    QVERIFY(splitter->sizes().constLast() > 0);

    QTest::mouseClick(left, Qt::RightButton, Qt::NoModifier, pointer);
    QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) != nullptr);
}

QTEST_MAIN(PanelManagerTest)

#include "tst_panelmanager.moc"
