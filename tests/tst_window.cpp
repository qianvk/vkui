// SPDX-License-Identifier: MIT

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitterHandle>
#include <QToolButton>
#include <QWidget>
#include <QWindow>
#include <QtTest>
#include <memory>
#include <type_traits>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSplitter.h>
#include <vkui/window/VMessageDialog.h>
#include <vkui/window/VWindowAgent.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
bool macWindowUsesNativeFullScreen(WId nativeViewId);
bool macToggleNativeFullScreen(WId nativeViewId);
bool macPerformNativeClose(WId nativeViewId);
bool macTrafficLightsAreHidden(WId nativeViewId);
int macVisibleSystemButtons(WId nativeViewId);
bool macForceTrafficLightsVisible(WId nativeViewId);
QRectF macTrafficLightGeometry(WId nativeViewId);
bool macTrafficLightsMatchGeometry(WId nativeViewId, const QRectF& expected);
bool macTrafficLightsFitInNativeTitleBar(WId nativeViewId);
#endif

class WindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void registersMultipleTitleBars();
    void titleBarExclusionsAreWindowScoped();
    void splitterCursorRemainsStableAcrossTitleBar();
    void composedAgentOwnsNativeBehavior();
    void messageDialogOwnsChromeAndButtonContract();
    void messageDialogTitleTracksTextSize();
    void destructivePromptDefaultsToCancel();
    void macFullScreenUsesNativeTrafficLightLayout();
    void macNativeCloseAbandonsDyingHandle_data();
    void macNativeCloseAbandonsDyingHandle();
};

void WindowTest::messageDialogTitleTracksTextSize() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    struct LevelGuard final {
        vkui::VkThemeManager* manager;
        int level;
        ~LevelGuard() {
            manager->setTextSizeLevel(level);
        }
    } restore{manager, originalLevel};
    manager->resetTextSizeLevel();

    vkui::VMessageDialog prompt(vkui::VMessageDialog::Icon::Information,
                                QStringLiteral("Typography"), QStringLiteral("Message"),
                                QDialogButtonBox::Cancel);
    auto* titleLabel = prompt.findChild<QLabel*>(QStringLiteral("VMessageDialogTitleLabel"));
    QVERIFY(titleLabel != nullptr);
    const qreal defaultSize = titleLabel->font().pointSizeF();

    manager->setTextSizeLevel(vkui::VkMaximumTextSizeLevel);
    QCOMPARE(titleLabel->font(), manager->theme().typography().bodyEmphasized);
    QVERIFY(titleLabel->font().pointSizeF() > defaultSize);
}

void WindowTest::registersMultipleTitleBars() {
    QWidget host;
    auto* firstTitleBar = new QWidget(&host);
    auto* secondTitleBar = new QWidget(&host);

    vkui::VWindowAgent agent(host);
    QVERIFY(agent.addTitleBar(firstTitleBar));
    QVERIFY(agent.addTitleBar(secondTitleBar));
    QCOMPARE(agent.titleBars(), QList<QWidget*>({firstTitleBar, secondTitleBar}));

    QVERIFY(agent.removeTitleBar(firstTitleBar));
    QCOMPARE(agent.titleBars(), QList<QWidget*>({secondTitleBar}));
    agent.clearTitleBars();
    QVERIFY(agent.titleBars().isEmpty());
}

void WindowTest::titleBarExclusionsAreWindowScoped() {
    QWidget host;
    auto* firstTitleBar = new QWidget(&host);
    auto* secondTitleBar = new QWidget(&host);
    auto* secondTitleBarControl = new QToolButton(secondTitleBar);
    auto* overlappingSibling = new QWidget(&host);

    firstTitleBar->setGeometry(0, 0, 200, 48);
    secondTitleBar->setGeometry(200, 0, 200, 48);
    secondTitleBarControl->setGeometry(10, 8, 80, 30);
    overlappingSibling->setGeometry(196, 0, 8, 48);

    vkui::VWindowAgent agent(host);
    QVERIFY(agent.addTitleBar(firstTitleBar));
    QVERIFY(agent.addTitleBar(secondTitleBar));

    QVERIFY(agent.setHitTestVisible(secondTitleBarControl));
    QVERIFY(agent.isHitTestVisible(secondTitleBarControl));

    // A sibling that spans title-bar boundaries is registered only once.
    QVERIFY(agent.setHitTestVisible(overlappingSibling));
    QVERIFY(agent.isHitTestVisible(overlappingSibling));
    QVERIFY(agent.setHitTestVisible(overlappingSibling, false));
    QVERIFY(!agent.isHitTestVisible(overlappingSibling));

    QWidget foreignWindow;
    QWidget foreignControl(&foreignWindow);
    QVERIFY(!agent.setHitTestVisible(&foreignControl));
    QVERIFY(!agent.isHitTestVisible(&foreignControl));
}

void WindowTest::splitterCursorRemainsStableAcrossTitleBar() {
    QWidget host;
    host.resize(640, 480);

    auto* splitter = new vkui::VSplitter(Qt::Horizontal, &host);
    splitter->setGeometry(host.rect());
    auto* firstPanel = new QWidget(splitter);
    auto* secondPanel = new QWidget(splitter);
    auto* firstTitleBar = new QWidget(firstPanel);
    auto* secondTitleBar = new QWidget(secondPanel);
    firstTitleBar->setGeometry(0, 0, 320, 56);
    secondTitleBar->setGeometry(0, 0, 320, 56);
    splitter->addWidget(firstPanel);
    splitter->addWidget(secondPanel);
    splitter->setSizes({240, 400});

    vkui::VWindowAgent agent(host);
    QVERIFY(agent.addTitleBar(firstTitleBar));
    QVERIFY(agent.addTitleBar(secondTitleBar));
    QSplitterHandle* handle = splitter->handle(1);
    QVERIFY(handle != nullptr);
    QVERIFY(agent.setHitTestVisible(handle));

    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const QPoint titleBarPoint(handle->contentsRect().center().x(), 28);
    QTest::mouseMove(handle, titleBarPoint);
    QTRY_VERIFY(handle->underMouse());
    QTRY_COMPARE(host.windowHandle()->cursor().shape(), Qt::SplitHCursor);

    // No component may replace the cursor while the handle still owns the pointer.
    QTest::qWait(250);
    QVERIFY(handle->underMouse());
    QCOMPARE(host.windowHandle()->cursor().shape(), Qt::SplitHCursor);

    const QPoint contentPoint(handle->contentsRect().center().x(), 180);
    QTest::mouseMove(handle, contentPoint);
    QTRY_VERIFY(handle->underMouse());
    QTRY_COMPARE(host.windowHandle()->cursor().shape(), Qt::SplitHCursor);
    QTest::qWait(250);
    QCOMPARE(host.windowHandle()->cursor().shape(), Qt::SplitHCursor);
}

void WindowTest::composedAgentOwnsNativeBehavior() {
    QWidget host;
    vkui::VWindowAgent agent(host);
    QVERIFY(agent.isResizable());
    QCOMPARE(agent.systemButtons(), vkui::VStandardSystemButtons);
    QVERIFY(agent.systemButtonsVisible());
    QVERIFY(agent.titleBars().isEmpty());

    QSignalSpy buttonChanges(&agent, &vkui::VWindowAgent::systemButtonsChanged);
    agent.setSystemButtons(vkui::VSystemButton::Close);
    QCOMPARE(agent.systemButtons(), vkui::VSystemButtons(vkui::VSystemButton::Close));
    QCOMPARE(buttonChanges.count(), 1);

    QSignalSpy visibilityChanges(&agent, &vkui::VWindowAgent::systemButtonsVisibleChanged);
    agent.setSystemButtonsVisible(false);
    QVERIFY(!agent.systemButtonsVisible());
    QCOMPARE(visibilityChanges.count(), 1);

    QWidget foreignWindow;
    QWidget foreignTitleBar(&foreignWindow);
    QVERIFY(!agent.addTitleBar(&foreignTitleBar));
    agent.setSystemButtonsVisible(true);
    QVERIFY(agent.systemButtonsVisible());
    QCOMPARE(visibilityChanges.count(), 2);
    agent.setResizable(false);
    QVERIFY(!agent.isResizable());
}

void WindowTest::messageDialogOwnsChromeAndButtonContract() {
    static_assert(std::is_base_of_v<QDialog, vkui::VMessageDialog>);

    vkui::VMessageDialog prompt(vkui::VMessageDialog::Icon::Information,
                                QStringLiteral("Operation complete"),
                                QStringLiteral("The requested operation completed successfully."),
                                QDialogButtonBox::Cancel);
    QVERIFY(prompt.windowFlags().testFlag(Qt::CustomizeWindowHint));
    QVERIFY(!prompt.windowFlags().testFlag(Qt::WindowCloseButtonHint));
    QVERIFY(!prompt.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
    QVERIFY(!prompt.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));

    auto* titleBar = prompt.findChild<QWidget*>(QStringLiteral("VMessageDialogTitleBar"));
    auto* titleLabel = prompt.findChild<QLabel*>(QStringLiteral("VMessageDialogTitleLabel"));
    QVERIFY(titleBar != nullptr);
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->parentWidget(), titleBar);
    titleBar->layout()->activate();
    QCOMPARE(titleLabel->geometry().left(), titleBar->layout()->contentsMargins().left());

    QPushButton* cancel = prompt.button(QDialogButtonBox::Cancel);
    QVERIFY(cancel != nullptr);
    QCOMPARE(prompt.buttons(), QList<QAbstractButton*>({cancel}));
    QCOMPARE(prompt.buttonRole(cancel), QDialogButtonBox::RejectRole);
    QCOMPARE(prompt.standardButton(cancel), QDialogButtonBox::Cancel);
    QCOMPARE(prompt.escapeButton(), static_cast<QAbstractButton*>(cancel));

    auto* custom = new QPushButton(QStringLiteral("Inspect"));
    prompt.addButton(custom, QDialogButtonBox::ActionRole);
    QVERIFY(prompt.buttons().contains(custom));
    QCOMPARE(prompt.buttonRole(custom), QDialogButtonBox::ActionRole);
    QCOMPARE(prompt.standardButton(custom), QDialogButtonBox::NoButton);
    QVERIFY(prompt.setButtonRole(custom, QDialogButtonBox::DestructiveRole));
    QCOMPARE(prompt.buttonRole(custom), QDialogButtonBox::DestructiveRole);
    QVERIFY(prompt.setButtonRole(custom, QDialogButtonBox::ApplyRole));
    QCOMPARE(prompt.buttonRole(custom), QDialogButtonBox::ApplyRole);

    prompt.setDefaultButton(custom);
    prompt.setEscapeButton(custom);
    QCOMPARE(prompt.defaultButton(), custom);
    QCOMPARE(prompt.escapeButton(), static_cast<QAbstractButton*>(custom));
    prompt.removeButton(custom);
    QVERIFY(!prompt.buttons().contains(custom));
    QVERIFY(custom->parent() == nullptr);
    QVERIFY(prompt.defaultButton() == nullptr);
    QVERIFY(prompt.escapeButton() == nullptr);
    delete custom;

    QPointer<QPushButton> transient = prompt.addButton(QDialogButtonBox::Help);
    QVERIFY(transient != nullptr);
    QCOMPARE(prompt.standardButton(transient), QDialogButtonBox::Help);
    prompt.clearButtons();
    QVERIFY(prompt.buttons().isEmpty());
    QVERIFY(transient == nullptr);

    auto* help = prompt.addButton(QDialogButtonBox::Help);
    auto* done = prompt.addButton(QStringLiteral("Done"), QDialogButtonBox::AcceptRole);
    QVERIFY(help != nullptr);
    QVERIFY(done != nullptr);
    int clickedCount = 0;
    connect(&prompt, &vkui::VMessageDialog::buttonClicked, &prompt,
            [&clickedCount](QAbstractButton*) { ++clickedCount; });
    prompt.show();
    QTRY_VERIFY(prompt.isVisible());
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        QTRY_COMPARE(macVisibleSystemButtons(prompt.winId()), 0);
    }
#endif
    help->click();
    QCOMPARE(clickedCount, 1);
    QVERIFY(prompt.isVisible());
    done->click();
    QCOMPARE(clickedCount, 2);
    QTRY_VERIFY(!prompt.isVisible());
    QCOMPARE(prompt.clickedButton(), static_cast<QAbstractButton*>(done));
}

void WindowTest::destructivePromptDefaultsToCancel() {
    vkui::VMessageDialog prompt(vkui::VMessageDialog::Icon::Warning, QStringLiteral("Delete file"),
                                QStringLiteral("This action cannot be undone."),
                                QDialogButtonBox::Cancel);
    auto* promptTitle = prompt.findChild<QLabel*>(QStringLiteral("VMessageDialogTitleLabel"));
    QVERIFY(promptTitle != nullptr);
    auto* promptTitleBar =
        prompt.findChild<QWidget*>(QStringLiteral("VMessageDialogTitleBar"));
    QVERIFY(promptTitleBar != nullptr);
    QCOMPARE(promptTitle->parentWidget(), promptTitleBar);
    QAbstractButton* destructive =
        prompt.addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
    QPushButton* cancel = prompt.button(QDialogButtonBox::Cancel);
    QVERIFY(cancel != nullptr);
    QVERIFY(destructive != nullptr);

    prompt.setDefaultButton(cancel);
    prompt.setEscapeButton(cancel);
    QCOMPARE(prompt.defaultButton(), cancel);
    QCOMPARE(prompt.escapeButton(), static_cast<QAbstractButton*>(cancel));
    QVERIFY(cancel->isDefault());
    QVERIFY(!qobject_cast<QPushButton*>(destructive)->isDefault());
    QVERIFY(!qobject_cast<QPushButton*>(destructive)->autoDefault());

    QSignalSpy destructiveClicks(destructive, &QAbstractButton::clicked);
    QTest::keyClick(destructive, Qt::Key_Return);
    QCOMPARE(destructiveClicks.count(), 0);
    QCOMPARE(prompt.clickedButton(), static_cast<QAbstractButton*>(cancel));

    prompt.reject();
    QCOMPARE(prompt.clickedButton(), static_cast<QAbstractButton*>(cancel));

    vkui::VMessageDialog outlinePrompt(
        vkui::VMessageDialog::Icon::Warning, QStringLiteral("Outline selection"),
        QStringLiteral("The logical default need not be painted as selection."),
        QDialogButtonBox::Cancel);
    QPushButton* outlineCancel = outlinePrompt.button(QDialogButtonBox::Cancel);
    QVERIFY(outlineCancel != nullptr);
    outlinePrompt.setDefaultButton(outlineCancel);
    outlinePrompt.setDefaultButtonIndicatorVisible(false);
    QVERIFY(!outlinePrompt.defaultButtonIndicatorVisible());
    QVERIFY(!outlineCancel->isDefault());
    QVERIFY(!outlineCancel->autoDefault());
    vkui::VMessageDialog guardedPrompt(
        vkui::VMessageDialog::Icon::Warning, QStringLiteral("Guarded"),
        QStringLiteral("Foreign and deleted buttons are never retained."),
        QDialogButtonBox::NoButton);
    QPushButton foreignButton;
    guardedPrompt.setEscapeButton(&foreignButton);
    guardedPrompt.reject();
    QVERIFY(guardedPrompt.clickedButton() == nullptr);

    vkui::VMessageDialog deletedButtonPrompt(
        vkui::VMessageDialog::Icon::Warning, QStringLiteral("Guarded"),
        QStringLiteral("Deleted buttons are cleared."), QDialogButtonBox::NoButton);
    auto* transientButton =
        deletedButtonPrompt.addButton(QStringLiteral("Transient"), QDialogButtonBox::RejectRole);
    deletedButtonPrompt.setEscapeButton(transientButton);
    delete transientButton;
    deletedButtonPrompt.reject();
    QVERIFY(deletedButtonPrompt.clickedButton() == nullptr);
}

void WindowTest::macFullScreenUsesNativeTrafficLightLayout() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        QSKIP("The Cocoa platform plugin is required for native AppKit validation.");
    }

    QWidget host;
    host.resize(960, 640);
    auto* titleBar = new QWidget(&host);
    titleBar->setGeometry(0, 0, host.width(), 56);

    vkui::VWindowAgent agent(host);
    QVERIFY(agent.addTitleBar(titleBar));
    const QPoint trafficLightOrigin(15, 15);
    agent.setTrafficLightOrigin(trafficLightOrigin);

    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const QRectF initialButtonGeometry = macTrafficLightGeometry(host.winId());
    QVERIFY(initialButtonGeometry.isValid());
    QCOMPARE(initialButtonGeometry.topLeft(), QPointF(trafficLightOrigin));

    agent.setSystemButtonsVisible(false);
    QTRY_VERIFY(macTrafficLightsAreHidden(host.winId()));
    agent.setSystemButtonsVisible(true);
    QTRY_VERIFY(!macTrafficLightsAreHidden(host.winId()));

    agent.setTrafficLightOrigin(QPoint(34, 1000));
    QTRY_VERIFY(macTrafficLightGeometry(host.winId()).top() < 1000.0);
    QVERIFY(macTrafficLightsFitInNativeTitleBar(host.winId()));

    const QPoint movedOrigin(34, 17);
    agent.setTrafficLightOrigin(movedOrigin);
    QTRY_COMPARE(macTrafficLightGeometry(host.winId()).topLeft(), QPointF(movedOrigin));
    const QRectF movedButtonGeometry = macTrafficLightGeometry(host.winId());

    QVERIFY(macToggleNativeFullScreen(host.winId()));
    QTRY_VERIFY_WITH_TIMEOUT(macWindowUsesNativeFullScreen(host.winId()), 10000);
    QTest::qWait(2000);
    QTRY_VERIFY_WITH_TIMEOUT(macTrafficLightsFitInNativeTitleBar(host.winId()), 10000);

    QVERIFY(macToggleNativeFullScreen(host.winId()));
    QTRY_VERIFY_WITH_TIMEOUT(macTrafficLightsAreHidden(host.winId()), 2000);
    // AppKit changes `hidden` again during the exit animation. The KVO guard
    // must synchronously restore the intended hidden state.
    QVERIFY(macForceTrafficLightsVisible(host.winId()));
    QVERIFY(macTrafficLightsAreHidden(host.winId()));

    QElapsedTimer exitTimer;
    exitTimer.start();
    bool exitCompleted = false;
    while (exitTimer.elapsed() < 10000) {
        const bool hidden = macTrafficLightsAreHidden(host.winId());
        if (!hidden) {
            // The first visible frame must already use the stable windowed
            // geometry; any intermediate AppKit origin is a visible jump.
            const QRectF currentButtonGeometry = macTrafficLightGeometry(host.winId());
            QVERIFY2(macTrafficLightsMatchGeometry(host.winId(), movedButtonGeometry),
                     qPrintable(QStringLiteral("Expected (%1, %2, %3, %4), actual "
                                               "(%5, %6, %7, %8)")
                                    .arg(movedButtonGeometry.x())
                                    .arg(movedButtonGeometry.y())
                                    .arg(movedButtonGeometry.width())
                                    .arg(movedButtonGeometry.height())
                                    .arg(currentButtonGeometry.x())
                                    .arg(currentButtonGeometry.y())
                                    .arg(currentButtonGeometry.width())
                                    .arg(currentButtonGeometry.height())));
            if (!macWindowUsesNativeFullScreen(host.winId())) {
                exitCompleted = true;
                break;
            }
        }
        QTest::qWait(5);
    }
    QVERIFY(exitCompleted);

    QElapsedTimer stabilityTimer;
    stabilityTimer.start();
    while (stabilityTimer.elapsed() < 500) {
        QVERIFY(!macTrafficLightsAreHidden(host.winId()));
        QVERIFY(macTrafficLightsMatchGeometry(host.winId(), movedButtonGeometry));
        QTest::qWait(5);
    }
    QTRY_VERIFY_WITH_TIMEOUT(macTrafficLightsFitInNativeTitleBar(host.winId()), 10000);
#else
    QSKIP("Native traffic lights are available only on macOS.");
#endif
}

void WindowTest::macNativeCloseAbandonsDyingHandle_data() {
    QTest::addColumn<bool>("fullScreen");
    QTest::newRow("windowed") << false;
    QTest::newRow("full-screen") << true;
}

void WindowTest::macNativeCloseAbandonsDyingHandle() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        QSKIP("The Cocoa platform plugin is required for native AppKit validation.");
    }

    QFETCH(bool, fullScreen);
    QGuiApplication::setQuitOnLastWindowClosed(false);

    auto* host = new QWidget;
    host->setAttribute(Qt::WA_DeleteOnClose);
    host->resize(960, 640);
    auto* titleBar = new QWidget(host);
    titleBar->setGeometry(0, 0, host->width(), 56);
    auto agent = std::make_unique<vkui::VWindowAgent>(*host);
    QVERIFY(agent->addTitleBar(titleBar));

    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    const WId nativeViewId = host->winId();
    if (fullScreen) {
        QVERIFY(macToggleNativeFullScreen(nativeViewId));
        QTRY_VERIFY_WITH_TIMEOUT(macWindowUsesNativeFullScreen(nativeViewId), 10000);
    }

    QPointer<QWidget> hostGuard(host);
    QVERIFY(macPerformNativeClose(nativeViewId));
    QTRY_VERIFY_WITH_TIMEOUT(hostGuard.isNull(), 10000);
#else
    QSKIP("Native close validation is available only on macOS.");
#endif
}

QTEST_MAIN(WindowTest)
#include "tst_window.moc"
