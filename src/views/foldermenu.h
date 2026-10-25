// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/common.h"

#include <memory>
#include <string>

namespace Lingmo::ui {
class Menu;
}

namespace fm {

class FolderModel;

// What a model action of FolderModel::action() does.
void triggerAction(FolderModel& model, const std::string& name);

// The folder's menu without a selection, the
// selection's with one. The model's actions are prepared first.
std::unique_ptr<ui::Menu> createFolderMenu(FolderModel& model);

} // namespace fm
