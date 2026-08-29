// SPDX-License-Identifier: MIT

#include "MacCursorProbe.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <objc/runtime.h>

#include <QMutex>
#include <QMutexLocker>
#include <QVector>

namespace vkui::test {

namespace {

struct CursorTransition {
    QPoint globalPosition;
    QString cursorName;
};

QMutex transitionMutex;
QVector<CursorTransition> cursorTransitions;
bool recordsCursorTransitions = false;
IMP originalCursorSet = nullptr;

QString cursorName(NSCursor* cursor) {
    if (cursor == NSCursor.arrowCursor) {
        return QStringLiteral("Arrow");
    }
    if (cursor == NSCursor.resizeLeftRightCursor) {
        return QStringLiteral("SplitH");
    }
    if (cursor == NSCursor.resizeUpDownCursor) {
        return QStringLiteral("SplitV");
    }
    if (cursor == NSCursor.pointingHandCursor) {
        return QStringLiteral("PointingHand");
    }
    if (cursor == NSCursor.IBeamCursor) {
        return QStringLiteral("IBeam");
    }
    return QString::fromNSString(cursor.description);
}

void recordCursorSet(NSCursor* cursor, SEL selector) {
    {
        QMutexLocker locker(&transitionMutex);
        if (recordsCursorTransitions) {
            CGEventRef event = CGEventCreate(nullptr);
            if (event != nullptr) {
                const CGPoint location = CGEventGetLocation(event);
                cursorTransitions.append(
                    {QPoint(qRound(location.x), qRound(location.y)), cursorName(cursor)});
                CFRelease(event);
            }
        }
    }

    reinterpret_cast<void (*)(id, SEL)>(originalCursorSet)(cursor, selector);
}

void installCursorSetRecorder() {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      Method method = class_getInstanceMethod(NSCursor.class, @selector(set));
      originalCursorSet = method_setImplementation(method, reinterpret_cast<IMP>(recordCursorSet));
    });
}

void postMouseEvent(const CGEventType type, const QPoint& globalPosition) {
    const CGPoint location = CGPointMake(globalPosition.x(), globalPosition.y());
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, location, kCGMouseButtonLeft);
    if (event == nullptr) {
        return;
    }
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

} // namespace

bool macCursorMatchesSplitter(const Qt::Orientation orientation) {
    NSCursor* expected = orientation == Qt::Horizontal ? NSCursor.resizeLeftRightCursor
                                                       : NSCursor.resizeUpDownCursor;
    return NSCursor.currentCursor == expected;
}

QString macCurrentCursorName() {
    return cursorName(NSCursor.currentCursor);
}

void macStartCursorTransitionRecording() {
    installCursorSetRecorder();
    QMutexLocker locker(&transitionMutex);
    cursorTransitions.clear();
    recordsCursorTransitions = true;
}

QStringList macStopCursorTransitionRecording(const QRect& splitterResponseArea,
                                              const Qt::Orientation orientation) {
    QMutexLocker locker(&transitionMutex);
    recordsCursorTransitions = false;

    const QString expected =
        orientation == Qt::Horizontal ? QStringLiteral("SplitH") : QStringLiteral("SplitV");
    QStringList invalidTransitions;
    for (const CursorTransition& transition : std::as_const(cursorTransitions)) {
        if (splitterResponseArea.contains(transition.globalPosition) &&
            transition.cursorName != expected) {
            invalidTransitions.append(
                QStringLiteral("%1 at %2,%3")
                    .arg(transition.cursorName)
                    .arg(transition.globalPosition.x())
                    .arg(transition.globalPosition.y()));
        }
    }
    cursorTransitions.clear();
    return invalidTransitions;
}

void macPostMouseMove(const QPoint& globalPosition) {
    postMouseEvent(kCGEventMouseMoved, globalPosition);
}

void macPostLeftMouseDown(const QPoint& globalPosition) {
    postMouseEvent(kCGEventLeftMouseDown, globalPosition);
}

void macPostLeftMouseDrag(const QPoint& globalPosition) {
    postMouseEvent(kCGEventLeftMouseDragged, globalPosition);
}

void macPostLeftMouseUp(const QPoint& globalPosition) {
    postMouseEvent(kCGEventLeftMouseUp, globalPosition);
}

} // namespace vkui::test
