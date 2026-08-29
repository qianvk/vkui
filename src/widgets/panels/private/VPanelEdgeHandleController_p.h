// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <functional>

class QEvent;
class QWidget;

namespace vkui {

/** Owns the non-native inner-left-edge affordance for one panel host window. */
class VPanelEdgeHandleController final : public QObject {
  public:
    using ActivationHandler =
        std::function<void(Qt::MouseButton button, const QPoint& globalPosition)>;

    explicit VPanelEdgeHandleController(QWidget& window, ActivationHandler activated,
                                        QObject* parent = nullptr);
    ~VPanelEdgeHandleController() override;

    [[nodiscard]] QList<QWidget*> handles() const;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updateHandleGeometry();

    QPointer<QWidget> window_;
    QPointer<QWidget> handle_;
};

} // namespace vkui
