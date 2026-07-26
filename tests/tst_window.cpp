// SPDX-License-Identifier: MIT

#include <QAbstractButton>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>
#include <limits>
#include <memory>
#include <vkui/window/VkFramelessDialog.h>
#include <vkui/window/VkMessageDialog.h>
#include <vkui/window/VkWindowAgent.h>

class WindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void registersMultipleTitleBars();
    void uninitializedAndDestroyedHostsAreSafe();
    void framelessDialogUsesCloseOnlyChromeAndHostGeometry();
    void destructivePromptDefaultsToCancel();
};

void WindowTest::registersMultipleTitleBars() {
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    auto* firstTitleBar = new QWidget(&host);
    auto* secondTitleBar = new QWidget(&host);
    layout->addWidget(firstTitleBar);
    layout->addWidget(secondTitleBar);

    vkui::VkWindowAgent agent;
    QVERIFY(agent.setup(&host));
    QVERIFY(agent.addTitleBar(firstTitleBar));
    QVERIFY(agent.addTitleBar(secondTitleBar));
    QCOMPARE(agent.titleBars(), QList<QWidget*>({firstTitleBar, secondTitleBar}));

    QVERIFY(agent.removeTitleBar(firstTitleBar));
    QCOMPARE(agent.titleBars(), QList<QWidget*>({secondTitleBar}));
    agent.clearTitleBars();
    QVERIFY(agent.titleBars().isEmpty());
}

void WindowTest::uninitializedAndDestroyedHostsAreSafe() {
    vkui::VkWindowAgent agent;
    QVERIFY(agent.titleBars().isEmpty());
    QVERIFY(agent.titleBar() == nullptr);
    QVERIFY(agent.systemButton(vkui::VkWindowAgent::SystemButton::Close) == nullptr);
    QVERIFY(!agent.installSystemButtons());
    QVERIFY(agent.systemButtonAreaGeometry().isNull());
    QVERIFY(!agent.setWindowAttribute(QStringLiteral("unknown"), true));

    QSignalSpy visibilityChanges(&agent, &vkui::VkWindowAgent::systemButtonVisibilityChanged);
    agent.setSystemButtonVisibility(vkui::VkWindowAgent::SystemButtonVisibility::AlwaysHidden);
    QCOMPARE(agent.systemButtonVisibility(),
             vkui::VkWindowAgent::SystemButtonVisibility::AlwaysHidden);
    QCOMPARE(visibilityChanges.count(), 1);

    auto host = std::make_unique<QWidget>();
    QVERIFY(agent.setup(host.get()));
    QCOMPARE(agent.systemButtonVisibility(),
             vkui::VkWindowAgent::SystemButtonVisibility::AlwaysHidden);
    QVERIFY(!agent.setup(host.get()));

    QWidget foreignWindow;
    QWidget foreignTitleBar(&foreignWindow);
    QVERIFY(!agent.addTitleBar(&foreignTitleBar));
    agent.setSystemButton(vkui::VkWindowAgent::SystemButton::Close, &foreignTitleBar);
    QVERIFY(agent.systemButton(vkui::VkWindowAgent::SystemButton::Close) == nullptr);

    host.reset();
    QVERIFY(agent.titleBars().isEmpty());
    QVERIFY(agent.titleBar() == nullptr);
    QVERIFY(!agent.installSystemButtons());
    agent.setResizable(true);
    QVERIFY(agent.isResizable());
}

void WindowTest::framelessDialogUsesCloseOnlyChromeAndHostGeometry() {
    QWidget host;
    host.setGeometry(120, 90, 1000, 800);

    vkui::VkFramelessDialog dialog(QStringLiteral("Preferences"), &host);
    dialog.setMinimumSize(320, 240);
    dialog.positionForHost(&host, QSizeF(std::numeric_limits<qreal>::quiet_NaN(),
                                         std::numeric_limits<qreal>::quiet_NaN()));

    QVERIFY(dialog.windowFlags().testFlag(Qt::WindowCloseButtonHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    QVERIFY(dialog.titleBar() != nullptr);
    QVERIFY(dialog.contentLayout() != nullptr);
    QVERIFY(!dialog.isResizable());
    QCOMPARE(dialog.size(), QSize(800, 640));
    QCOMPARE(dialog.frameGeometry().center(), host.frameGeometry().center());
#ifdef Q_OS_MAC
    // Leave traffic-light placement to AppKit's current platform geometry.
    QVERIFY(
        !dialog.windowAgent()->hasSystemButtonPosition(vkui::VkWindowAgent::SystemButton::Close));
#endif

    // Headless QPA plugins must use the portable context instead of treating
    // their synthetic WId as an AppKit/Win32 native handle.
    dialog.show();
    QTRY_VERIFY(dialog.isVisible());
    dialog.close();
}

void WindowTest::destructivePromptDefaultsToCancel() {
    vkui::VkMessageDialog prompt(
        vkui::VkMessageDialog::Icon::Warning, QStringLiteral("Delete file"),
        QStringLiteral("This action cannot be undone."), QDialogButtonBox::Cancel);
    QAbstractButton* destructive =
        prompt.addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
    QPushButton* cancel = prompt.button(QDialogButtonBox::Cancel);
    QVERIFY(cancel != nullptr);
    QVERIFY(destructive != nullptr);

    prompt.setDefaultButton(cancel);
    prompt.setEscapeButton(cancel);
    QVERIFY(cancel->isDefault());
    QVERIFY(!qobject_cast<QPushButton*>(destructive)->isDefault());
    QVERIFY(!qobject_cast<QPushButton*>(destructive)->autoDefault());

    QSignalSpy destructiveClicks(destructive, &QAbstractButton::clicked);
    QTest::keyClick(destructive, Qt::Key_Return);
    QCOMPARE(destructiveClicks.count(), 0);
    QVERIFY(prompt.clickedButton() == nullptr);

    prompt.reject();
    QCOMPARE(prompt.clickedButton(), static_cast<QAbstractButton*>(cancel));

    vkui::VkMessageDialog guardedPrompt(
        vkui::VkMessageDialog::Icon::Warning, QStringLiteral("Guarded"),
        QStringLiteral("Foreign and deleted buttons are never retained."),
        QDialogButtonBox::NoButton);
    QPushButton foreignButton;
    guardedPrompt.setEscapeButton(&foreignButton);
    guardedPrompt.reject();
    QVERIFY(guardedPrompt.clickedButton() == nullptr);

    vkui::VkMessageDialog deletedButtonPrompt(
        vkui::VkMessageDialog::Icon::Warning, QStringLiteral("Guarded"),
        QStringLiteral("Deleted buttons are cleared."), QDialogButtonBox::NoButton);
    auto* transientButton =
        deletedButtonPrompt.addButton(QStringLiteral("Transient"), QDialogButtonBox::RejectRole);
    deletedButtonPrompt.setEscapeButton(transientButton);
    delete transientButton;
    deletedButtonPrompt.reject();
    QVERIFY(deletedButtonPrompt.clickedButton() == nullptr);
}

QTEST_MAIN(WindowTest)
#include "tst_window.moc"
