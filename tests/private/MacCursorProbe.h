// SPDX-License-Identifier: MIT

#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>

namespace vkui::test {

[[nodiscard]] bool macCursorMatchesSplitter(Qt::Orientation orientation);
[[nodiscard]] QString macCurrentCursorName();
void macStartCursorTransitionRecording();
[[nodiscard]] QStringList macStopCursorTransitionRecording(const QRect& splitterResponseArea,
                                                           Qt::Orientation orientation);
void macPostMouseMove(const QPoint& globalPosition);
void macPostLeftMouseDown(const QPoint& globalPosition);
void macPostLeftMouseDrag(const QPoint& globalPosition);
void macPostLeftMouseUp(const QPoint& globalPosition);

} // namespace vkui::test
