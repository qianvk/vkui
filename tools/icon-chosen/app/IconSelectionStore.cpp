// SPDX-License-Identifier: MIT

#include "IconSelectionStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

QString IconSelectionStore::defaultPath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(directory).filePath(QStringLiteral("selected-icons.json"));
}

std::optional<QSet<QString>> IconSelectionStore::load(const QString& path, QString* errorMessage) {
    const auto fail = [errorMessage](const QString& message) -> std::optional<QSet<QString>> {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return std::nullopt;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(parseError.errorString());
    }
    if (!document.isObject()) {
        return fail(QStringLiteral("The selection manifest must contain a JSON object."));
    }

    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("formatVersion"));
    if (!version.isUndefined() && (!version.isDouble() || version.toInt() != 1)) {
        return fail(QStringLiteral("The selection manifest version is not supported."));
    }
    const QJsonValue symbolValue = root.value(QStringLiteral("symbols"));
    if (!symbolValue.isArray()) {
        return fail(QStringLiteral("The selection manifest has no valid symbols array."));
    }

    const QJsonArray symbols = symbolValue.toArray();
    QSet<QString> result;
    for (const QJsonValue& value : symbols) {
        QString name;
        if (value.isObject()) {
            name = value.toObject().value(QStringLiteral("name")).toString();
        } else if (value.isString()) {
            name = value.toString();
        }
        if (name.isEmpty()) {
            return fail(QStringLiteral("The selection manifest contains an invalid symbol."));
        }
        result.insert(name);
    }
    return result;
}

bool IconSelectionStore::save(const QString& path, const QList<IconCatalogEntry>& selection) {
    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return false;
    }

    QJsonArray symbols;
    for (const IconCatalogEntry& item : selection) {
        symbols.append(QJsonObject{{QStringLiteral("name"), item.name},
                                   {QStringLiteral("codepoint"), item.codePoint}});
    }
    const QJsonObject root{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("source"), QStringLiteral("Nerd Fonts v3.4.0 / FiraCode Nerd Font")},
        {QStringLiteral("symbols"), symbols}};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}
