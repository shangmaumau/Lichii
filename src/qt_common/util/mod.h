// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace QtCommon::Mod {

QString TranslatePatchName(const char* name);
QString TranslatePatchDescription(const char* description);

QStringList GetModFolders(const QString& root, const QString& fallbackName);

const QString ExtractMod(const QString& path);

} // namespace QtCommon::Mod
