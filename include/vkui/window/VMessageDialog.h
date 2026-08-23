// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QList>
#include <QPushButton>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QEvent;
class QKeyEvent;

namespace vkui {

class VMessageDialogPrivate;

/** A cross-platform message prompt with native full-content window behavior. */
class VKUI_WINDOW_EXPORT VMessageDialog final : public QDialog {
    Q_OBJECT

  public:
    enum class Icon {
        Information,
        Warning,
        Critical,
    };
    Q_ENUM(Icon)

    explicit VMessageDialog(
        Icon icon, const QString& title, const QString& text,
        QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
        QWidget* parent = nullptr);
    ~VMessageDialog() override;

    /** Adds a custom button and transfers its ownership to the dialog. */
    void addButton(QAbstractButton* button, QDialogButtonBox::ButtonRole role);
    [[nodiscard]] QPushButton* addButton(QDialogButtonBox::StandardButton button);
    [[nodiscard]] QPushButton* addButton(const QString& text,
                                         QDialogButtonBox::ButtonRole role);
    /** Removes a button without deleting it. The caller resumes ownership. */
    void removeButton(QAbstractButton* button);
    /** Deletes every button owned by the dialog. */
    void clearButtons();

    [[nodiscard]] QList<QAbstractButton*> buttons() const;
    [[nodiscard]] QPushButton* button(QDialogButtonBox::StandardButton button) const;
    [[nodiscard]] QDialogButtonBox::ButtonRole buttonRole(QAbstractButton* button) const;
    [[nodiscard]] QDialogButtonBox::StandardButton
    standardButton(QAbstractButton* button) const;
    /** Changes the semantic role of a button already owned by the dialog. */
    [[nodiscard]] bool setButtonRole(QAbstractButton* button,
                                     QDialogButtonBox::ButtonRole role);

    void setDefaultButton(QAbstractButton* button);
    void setDefaultButton(QDialogButtonBox::StandardButton button);
    [[nodiscard]] QPushButton* defaultButton() const;
    [[nodiscard]] bool defaultButtonIndicatorVisible() const noexcept;
    /**
     * Controls only the native default-button paint state. Enter keeps
     * activating the logical default even when this indicator is hidden.
     */
    void setDefaultButtonIndicatorVisible(bool visible);

    void setEscapeButton(QAbstractButton* button);
    void setEscapeButton(QDialogButtonBox::StandardButton button);
    [[nodiscard]] QAbstractButton* escapeButton() const;

    [[nodiscard]] QAbstractButton* clickedButton() const;
    [[nodiscard]] QDialogButtonBox::StandardButton clickedStandardButton() const;

    static QDialogButtonBox::StandardButton
    information(QWidget* parent, const QString& title, const QString& text,
                QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    warning(QWidget* parent, const QString& title, const QString& text,
            QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
            QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    critical(QWidget* parent, const QString& title, const QString& text,
             QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
             QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);

    /**
     * Runs a destructive prompt with Cancel as both default and Escape.
     * Enter can therefore never confirm deletion accidentally, even when the
     * destructive button has keyboard focus.
     */
    static bool confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                                   const QString& confirmText = {});

  public slots:
    void reject() override;

  signals:
    void buttonClicked(QAbstractButton* button);

  protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    static QDialogButtonBox::StandardButton run(Icon icon, QWidget* parent, const QString& title,
                                                const QString& text,
                                                QDialogButtonBox::StandardButtons buttons,
                                                QDialogButtonBox::StandardButton defaultButton);
    void buildUi(Icon icon, const QString& text, QDialogButtonBox::StandardButtons buttons);
    void configureWindowChrome();
    void refreshAutomaticEscapeButton();
    void refreshDefaultButtonState();
    void refreshIcon();
    void refreshTitle();
    void handleButtonClick(QAbstractButton* button);
    [[nodiscard]] bool ownsButton(const QAbstractButton* button) const;

    // The private native-window agent is destroyed before the QDialog base.
    std::unique_ptr<VMessageDialogPrivate> d_;
};

} // namespace vkui
