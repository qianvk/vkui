// SPDX-License-Identifier: MIT

#include "GalleryWindow.h"

#include <QAbstractButton>
#include <QDialog>
#include <QImage>
#include <QPointer>
#include <QSplitter>
#include <QSplitterHandle>
#include <QtTest>
#include <cmath>
#include <vkui/Widgets.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include "private/MacCursorProbe.h"

namespace {

class SplitterCursorTransitionProbe final : public QObject {
  public:
    explicit SplitterCursorTransitionProbe(QSplitterHandle* handle) : handle_(handle) {}

    [[nodiscard]] int invalidTransitionCount() const {
        return invalidTransitionCount_;
    }

    [[nodiscard]] QString firstInvalidTransition() const {
        return firstInvalidTransition_;
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::CursorChange || handle_ == nullptr) {
            return QObject::eventFilter(watched, event);
        }

        auto* window = qobject_cast<QWindow*>(watched);
        if (window == nullptr) {
            return QObject::eventFilter(watched, event);
        }

        const QPoint pointer = QCursor::pos();
        const QRect responseArea(handle_->mapToGlobal(QPoint()), handle_->size());
        if (responseArea.contains(pointer) && window->cursor().shape() != Qt::SplitHCursor) {
            ++invalidTransitionCount_;
            if (firstInvalidTransition_.isEmpty()) {
                QWidget* hit = QApplication::widgetAt(pointer);
                QSplitter* splitter = handle_->splitter();
                firstInvalidTransition_ =
                    QStringLiteral("pointer=%1,%2 area=%3,%4 %5x%6 qt=%7 native=%8 "
                                   "handleUnderMouse=%9 splitterCursor=%10 splitterHasCursor=%11 "
                                   "hit=%12 hitCursor=%13 hitHasCursor=%14")
                        .arg(pointer.x())
                        .arg(pointer.y())
                        .arg(responseArea.x())
                        .arg(responseArea.y())
                        .arg(responseArea.width())
                        .arg(responseArea.height())
                        .arg(window->cursor().shape())
                        .arg(vkui::test::macCurrentCursorName())
                        .arg(handle_->underMouse())
                        .arg(splitter != nullptr ? splitter->cursor().shape() : Qt::ArrowCursor)
                        .arg(splitter != nullptr && splitter->testAttribute(Qt::WA_SetCursor))
                        .arg(hit != nullptr ? QString::fromLatin1(hit->metaObject()->className())
                                            : QStringLiteral("null"))
                        .arg(hit != nullptr ? hit->cursor().shape() : Qt::ArrowCursor)
                        .arg(hit != nullptr && hit->testAttribute(Qt::WA_SetCursor));
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    QPointer<QSplitterHandle> handle_;
    QString firstInvalidTransition_;
    int invalidTransitionCount_{0};
};

} // namespace
#endif

class GalleryWindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void languageActivationDefersUiRebuild();
    void splitterClickOpensPanelChooser();
    void nativeSplitterCursorTracksSideToSideCrossing();
    void nativeSplitterCursorNeverChangesDuringInternalTranslation();
    void leftWindowEdgeClickOpensPanelChooser();
    void panelChooserTogglesPanelsAcrossAppearances();
};

void GalleryWindowTest::initTestCase() {
    vkui::installVkUi(*qApp);
}

void GalleryWindowTest::languageActivationDefersUiRebuild() {
    GalleryWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto* appearanceSurface =
        window.findChild<vkui::VLiquidGlassSurface*>(QStringLiteral("galleryAppearanceGlass"));
    auto* languageSurface =
        window.findChild<vkui::VLiquidGlassSurface*>(QStringLiteral("galleryLanguageGlass"));
    QVERIFY(appearanceSurface != nullptr);
    QVERIFY(languageSurface != nullptr);
    QVERIFY(appearanceSurface->glassStyle() == vkui::VLiquidGlassStyle::control());
    QVERIFY(languageSurface->glassStyle() == vkui::VLiquidGlassStyle::control());

    QPointer<vkui::VCombobox> original =
        window.findChild<vkui::VCombobox*>(QStringLiteral("galleryLanguageBox"));
    QVERIFY(original);
    constexpr int SimplifiedChinese = 2;
    const int targetIndex = original->findData(SimplifiedChinese);
    QVERIFY(targetIndex >= 0);

    QVERIFY(QMetaObject::invokeMethod(original, "activated", Qt::DirectConnection,
                                      Q_ARG(int, targetIndex)));
    // The signal sender must survive until QComboBox finishes delivering activated().
    QVERIFY(original);

    QTRY_VERIFY(original.isNull());
    auto* rebuilt = window.findChild<vkui::VCombobox*>(QStringLiteral("galleryLanguageBox"));
    QVERIFY(rebuilt != nullptr);
    QCOMPARE(rebuilt->currentData().toInt(), SimplifiedChinese);
}

void GalleryWindowTest::splitterClickOpensPanelChooser() {
    GalleryWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();
    auto* splitter = window.findChild<vkui::VSplitter*>(QStringLiteral("galleryPanelSplitter"));
    QVERIFY(splitter != nullptr);
    QSplitterHandle* handle = splitter->handle(1);
    QVERIFY(handle != nullptr);
    QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier, handle->rect().center());

    QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) != nullptr);
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog"));
    QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(!dialog->windowFlags().testFlag(Qt::NoDropShadowWindowHint));
    QCOMPARE(dialog->windowType(), Qt::Popup);
    QCOMPARE(QApplication::activePopupWidget(), dialog);
    QVERIFY(std::abs(dialog->width() - qRound(window.width() * 0.60)) <= 2);
    QVERIFY(std::abs(dialog->height() - qRound(window.height() * 0.60)) <= 2);

    const auto previews = dialog->findChildren<QAbstractButton*>();
    QCOMPARE(previews.size(), 2);
    QAbstractButton* navigation = nullptr;
    QAbstractButton* content = nullptr;
    for (QAbstractButton* preview : previews) {
        const QString panelId = preview->property("vPanelId").toString();
        if (panelId == QStringLiteral("navigation")) {
            navigation = preview;
            QCOMPARE(preview->property("panelNumber").toInt(), 1);
        } else if (panelId == QStringLiteral("content")) {
            content = preview;
            QCOMPARE(preview->property("panelNumber").toInt(), 2);
        }
    }
    QVERIFY(navigation != nullptr);
    QVERIFY(content != nullptr);
    QVERIFY(navigation->text().isEmpty());
    QVERIFY(content->text().isEmpty());
    auto* canvas = dialog->findChild<QWidget*>(QStringLiteral("vPanelLayoutCanvas"));
    QVERIFY(canvas != nullptr);
    QCOMPARE(canvas->geometry(), dialog->rect());
    QCOMPARE(navigation->geometry().left(), canvas->rect().left());
    QCOMPARE(navigation->geometry().top(), canvas->rect().top());
    QCOMPARE(navigation->geometry().bottom(), canvas->rect().bottom());
    QCOMPARE(content->geometry().right(), canvas->rect().right());
    QCOMPARE(content->geometry().top(), canvas->rect().top());
    QCOMPARE(content->geometry().bottom(), canvas->rect().bottom());
    QCOMPARE(navigation->geometry().right() + 1, content->geometry().left());
    QVERIFY(std::abs(navigation->geometry().top() - content->geometry().top()) <= 2);
    QVERIFY(std::abs(navigation->height() - content->height()) <= 2);
    QVERIFY(navigation->width() < content->width());
}

void GalleryWindowTest::nativeSplitterCursorTracksSideToSideCrossing() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        QSKIP("This test requires the Cocoa platform plugin.");
    }

    GalleryWindow window;
    window.resize(1000, 700);
    window.show();
    window.raise();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.isActiveWindow());
    QTest::qWait(100);

    auto* splitter = window.findChild<vkui::VSplitter*>(QStringLiteral("galleryPanelSplitter"));
    QVERIFY(splitter != nullptr);
    QSplitterHandle* handle = splitter->handle(1);
    QWindow* topLevelWindow = window.windowHandle();
    QVERIFY(handle != nullptr);
    QVERIFY(topLevelWindow != nullptr);

    // Install after VSplitter's internal filter so this probe observes the intermediate cursor
    // selected by Qt before VSplitter has an opportunity to correct it.
    SplitterCursorTransitionProbe transitionProbe(handle);
    topLevelWindow->installEventFilter(&transitionProbe);
    QStringList invalidNativeTransitions;

    const auto postMoveAndWait = [](const QPoint& target) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            vkui::test::macPostMouseMove(target);
            QTest::qWait(1);
            if (QCursor::pos() == target) {
                return true;
            }
        }
        return false;
    };

    for (int iteration = 0; iteration < 80; ++iteration) {
        const bool leftToRight = iteration % 2 == 0;
        const int localY = iteration % 4 < 2 ? 24 : handle->rect().center().y();
        if (iteration % 4 == 0) {
            const QPoint dragStart =
                handle->mapToGlobal(QPoint(handle->rect().center().x(), localY));
            QVERIFY(postMoveAndWait(dragStart));
            QTRY_VERIFY(vkui::test::macCursorMatchesSplitter(Qt::Horizontal));
            const QPoint dragTarget = dragStart + QPoint(iteration % 8 == 0 ? 16 : -16, 0);
            vkui::test::macPostLeftMouseDown(dragStart);
            QTest::qWait(1);
            vkui::test::macPostLeftMouseDrag(dragTarget);
            QTest::qWait(4);
            vkui::test::macPostLeftMouseUp(dragTarget);
            QTest::qWait(4);
        }
        const QRect handleGlobal(handle->mapToGlobal(QPoint{}), handle->size());
        const int firstX = leftToRight ? handleGlobal.left() - 8 : handleGlobal.right() + 8;
        const int lastX = leftToRight ? handleGlobal.right() + 8 : handleGlobal.left() - 8;
        const int step = leftToRight ? 1 : -1;
        const int globalY = handle->mapToGlobal(QPoint(0, localY)).y();
        bool startedOutside = false;
        bool enteredHandle = false;
        bool exitedHandle = false;
        vkui::test::macStartCursorTransitionRecording();

        for (int x = firstX;; x += step) {
            const QPoint target(x, globalY);
            QVERIFY2(postMoveAndWait(target),
                     "The native pointer did not reach the crossing point");
            QApplication::processEvents(QEventLoop::AllEvents, 1);

            const bool insideHandle = handleGlobal.contains(target);
            if (!insideHandle && !startedOutside) {
                QTRY_COMPARE(topLevelWindow->cursor().shape(), Qt::ArrowCursor);
                QTRY_VERIFY(!vkui::test::macCursorMatchesSplitter(Qt::Horizontal));
                startedOutside = true;
            } else if (insideHandle && !enteredHandle) {
                // Cursor transitions are delivered asynchronously by AppKit. Wait only at the
                // geometric boundary, then require every remaining handle pixel to retain it.
                QTRY_COMPARE(topLevelWindow->cursor().shape(), Qt::SplitHCursor);
                QTRY_VERIFY(vkui::test::macCursorMatchesSplitter(Qt::Horizontal));
                enteredHandle = true;
            } else if (!insideHandle && enteredHandle && !exitedHandle) {
                QTRY_COMPARE(topLevelWindow->cursor().shape(), Qt::ArrowCursor);
                QTRY_VERIFY(!vkui::test::macCursorMatchesSplitter(Qt::Horizontal));
                exitedHandle = true;
            }

            const bool windowHasSplitCursor = topLevelWindow->cursor().shape() == Qt::SplitHCursor;
            const bool nativeHasSplitCursor = vkui::test::macCursorMatchesSplitter(Qt::Horizontal);
            if (windowHasSplitCursor != insideHandle || nativeHasSplitCursor != insideHandle) {
                QWidget* hit = QApplication::widgetAt(target);
                qCritical().nospace()
                    << "Side-to-side cursor mismatch: iteration=" << iteration
                    << " direction=" << (leftToRight ? "left-to-right" : "right-to-left")
                    << " target=" << target << " handleRect=" << handleGlobal
                    << " expected=" << (insideHandle ? "SplitH" : "Arrow")
                    << " windowCursor=" << topLevelWindow->cursor().shape()
                    << " nativeCursor=" << vkui::test::macCurrentCursorName()
                    << " handleUnderMouse=" << handle->underMouse() << " widgetAt="
                    << (hit != nullptr ? hit->objectName() : QStringLiteral("null"));
                QFAIL("The cursor changed incorrectly while crossing the splitter response area");
            }
            if (x == lastX) {
                break;
            }
        }
        invalidNativeTransitions.append(
            vkui::test::macStopCursorTransitionRecording(handleGlobal, Qt::Horizontal));
        QVERIFY(startedOutside);
        QVERIFY(enteredHandle);
        QVERIFY(exitedHandle);
        QTRY_COMPARE(topLevelWindow->cursor().shape(), Qt::ArrowCursor);
        QTRY_VERIFY(!vkui::test::macCursorMatchesSplitter(Qt::Horizontal));
    }
    QVERIFY2(transitionProbe.invalidTransitionCount() == 0,
             qPrintable(transitionProbe.firstInvalidTransition()));
    QVERIFY2(invalidNativeTransitions.isEmpty(),
             qPrintable(QStringLiteral("Invalid native cursor transitions inside the splitter: %1")
                            .arg(invalidNativeTransitions.join(QStringLiteral(", ")))));
#else
    QSKIP("This test requires macOS.");
#endif
}

void GalleryWindowTest::nativeSplitterCursorNeverChangesDuringInternalTranslation() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"), Qt::CaseInsensitive)) {
        QSKIP("This test requires the Cocoa platform plugin.");
    }

    GalleryWindow window;
    window.resize(1000, 700);
    window.show();
    window.raise();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.isActiveWindow());
    QTest::qWait(100);

    auto* splitter = window.findChild<vkui::VSplitter*>(QStringLiteral("galleryPanelSplitter"));
    QVERIFY(splitter != nullptr);
    QSplitterHandle* handle = splitter->handle(1);
    QWindow* topLevelWindow = window.windowHandle();
    QVERIFY(handle != nullptr);
    QVERIFY(topLevelWindow != nullptr);

    const QRect responseArea(handle->mapToGlobal(QPoint()), handle->size());
    const QPoint initialPoint = handle->mapToGlobal(handle->rect().center());
    vkui::test::macPostMouseMove(initialPoint);
    QTRY_COMPARE(QCursor::pos(), initialPoint);
    QTRY_COMPARE(topLevelWindow->cursor().shape(), Qt::SplitHCursor);
    QTRY_VERIFY(vkui::test::macCursorMatchesSplitter(Qt::Horizontal));

    SplitterCursorTransitionProbe transitionProbe(handle);
    topLevelWindow->installEventFilter(&transitionProbe);
    vkui::test::macStartCursorTransitionRecording();

    constexpr int TranslationCount = 400;
    const int span = qMax(1, responseArea.width() - 1);
    for (int iteration = 0; iteration < TranslationCount; ++iteration) {
        const int phase = iteration % (span * 2);
        const int offset = phase <= span ? phase : span * 2 - phase;
        const int localY = iteration % 2 == 0 ? 24 : handle->rect().center().y();
        const QPoint target(responseArea.left() + offset,
                            handle->mapToGlobal(QPoint(0, localY)).y());
        QVERIFY(responseArea.contains(target));

        vkui::test::macPostMouseMove(target);
        QTRY_COMPARE(QCursor::pos(), target);
        const bool qtCursorIsValid = topLevelWindow->cursor().shape() == Qt::SplitHCursor;
        const bool nativeCursorIsValid = vkui::test::macCursorMatchesSplitter(Qt::Horizontal);
        if (!qtCursorIsValid || !nativeCursorIsValid) {
            const QStringList invalidTransitions =
                vkui::test::macStopCursorTransitionRecording(responseArea, Qt::Horizontal);
            QWidget* hit = QApplication::widgetAt(target);
            qCritical().nospace() << "Internal cursor translation failed: iteration=" << iteration
                                  << " target=" << target << " area=" << responseArea
                                  << " qt=" << topLevelWindow->cursor().shape()
                                  << " native=" << vkui::test::macCurrentCursorName()
                                  << " handleUnderMouse=" << handle->underMouse()
                                  << " handleCursor=" << handle->cursor().shape()
                                  << " splitterCursor=" << splitter->cursor().shape()
                                  << " splitterHasCursor="
                                  << splitter->testAttribute(Qt::WA_SetCursor) << " widgetAt="
                                  << (hit != nullptr ? hit->objectName() : QStringLiteral("null"))
                                  << " transitions="
                                  << invalidTransitions.join(QStringLiteral("; "));
            QFAIL("The cursor changed while translating inside the splitter response area");
        }
    }

    const QStringList invalidNativeTransitions =
        vkui::test::macStopCursorTransitionRecording(responseArea, Qt::Horizontal);
    QVERIFY2(transitionProbe.invalidTransitionCount() == 0,
             qPrintable(transitionProbe.firstInvalidTransition()));
    QVERIFY2(invalidNativeTransitions.isEmpty(),
             qPrintable(QStringLiteral("Invalid native cursor transitions during internal "
                                       "translation: %1")
                            .arg(invalidNativeTransitions.join(QStringLiteral(", ")))));
#else
    QSKIP("This test requires macOS.");
#endif
}

void GalleryWindowTest::leftWindowEdgeClickOpensPanelChooser() {
    GalleryWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    auto* edgeHandle = window.findChild<QAbstractButton*>(
        QStringLiteral("vPanelWindowEdgeHandle_left"), Qt::FindDirectChildrenOnly);
    QVERIFY(edgeHandle != nullptr);
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_top"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_bottom"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QVERIFY(window.findChild<QAbstractButton*>(QStringLiteral("vPanelWindowEdgeHandle_right"),
                                               Qt::FindDirectChildrenOnly) == nullptr);
    QCOMPARE(edgeHandle->geometry().left(), window.rect().left());
    QVERIFY(edgeHandle->width() >= 8);

    QTest::mouseMove(edgeHandle, edgeHandle->rect().center());
    QTest::qWait(180);
    QTest::mouseClick(edgeHandle, Qt::RightButton, Qt::NoModifier, edgeHandle->rect().center());
    QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) != nullptr);
}

void GalleryWindowTest::panelChooserTogglesPanelsAcrossAppearances() {
    GalleryWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    auto* splitter = window.findChild<vkui::VSplitter*>(QStringLiteral("galleryPanelSplitter"));
    auto* manager = window.findChild<vkui::VPanelManager*>(QStringLiteral("galleryPanelManager"));
    QVERIFY(splitter != nullptr);
    QVERIFY(manager != nullptr);
    manager->showPanelChooser();
    QCoreApplication::processEvents();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog"));
    QVERIFY(dialog != nullptr);
    auto* navigation =
        dialog->findChild<QAbstractButton*>(QStringLiteral("vPanelPreview_navigation"));
    QVERIFY(navigation != nullptr);

    for (const vkui::VkAppearance appearance :
         {vkui::VkAppearance::Light, vkui::VkAppearance::Dark}) {
        vkui::VkThemeManager::instance()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QVERIFY(manager->setPanelExpanded(QStringLiteral("navigation"), true));
        manager->showPanelChooser();
        QCoreApplication::processEvents();
        dialog = window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog"));
        QVERIFY(dialog != nullptr);
        navigation =
            dialog->findChild<QAbstractButton*>(QStringLiteral("vPanelPreview_navigation"));
        QVERIFY(navigation != nullptr);
        QVERIFY(navigation->property("expanded").toBool());
        QImage expandedImage(navigation->size(), QImage::Format_ARGB32_Premultiplied);
        expandedImage.fill(Qt::transparent);
        navigation->render(&expandedImage);

        QTest::mouseClick(navigation, Qt::LeftButton, Qt::NoModifier, navigation->rect().center());
        QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) == nullptr);
        QVERIFY(!manager->isPanelExpanded(QStringLiteral("navigation")));
        QTRY_COMPARE(splitter->sizes().constFirst(), 0);

        manager->showPanelChooser();
        QCoreApplication::processEvents();
        dialog = window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog"));
        QVERIFY(dialog != nullptr);
        navigation =
            dialog->findChild<QAbstractButton*>(QStringLiteral("vPanelPreview_navigation"));
        QVERIFY(navigation != nullptr);
        QVERIFY(!navigation->property("expanded").toBool());
        QImage collapsedImage(navigation->size(), QImage::Format_ARGB32_Premultiplied);
        collapsedImage.fill(Qt::transparent);
        navigation->render(&collapsedImage);
        QVERIFY(expandedImage != collapsedImage);

        QTest::mouseClick(navigation, Qt::LeftButton, Qt::NoModifier, navigation->rect().center());
        QTRY_VERIFY(window.findChild<QDialog*>(QStringLiteral("vPanelLayoutDialog")) == nullptr);
        QVERIFY(manager->isPanelExpanded(QStringLiteral("navigation")));
        QTRY_VERIFY(splitter->sizes().constFirst() > 0);
    }
    vkui::VkThemeManager::instance()->setAppearance(vkui::VkAppearance::Auto);
}

QTEST_MAIN(GalleryWindowTest)

#include "tst_gallerywindow.moc"
