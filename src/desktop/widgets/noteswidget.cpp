// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/noteswidget.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "base/i18n.h"
#include "base/paths.h"
#include "desktop/widgets/widgetlayer.h"
#include "ui/text_area.h"
#include "ui/theme.h"

using Lingmo::i18n::tr;

namespace fm {

NotesWidget::NotesWidget(WidgetLayer& layer, WidgetSize size)
    : DesktopWidget(layer, WidgetKind::Notes, size)
{
    setName("notes-widget");
    m_text = addChild<ui::TextArea>();
    m_text->setPlaceholder(tr("Write a note…"));
    m_text->setWrapMode(ui::TextArea::WrapMode::WordWrap);
    m_text->setFrameVisible(false);
    m_text->setPadding({4, 4, 4, 4});
    m_text->setFontSize(size == WidgetSize::Small ? 11 : 13);
    m_text->setTextColor(ui::Theme::current().text);
    m_text->onTextChanged = [this] {
        if (!m_loaded)
            return;
        m_saveTimer.start();
    };

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(std::chrono::milliseconds(450));
    m_saveTimer.onTimeout = [this] { save(); };
}

NotesWidget::~NotesWidget()
{
    m_saveTimer.stop();
    save();
}

void NotesWidget::instanceIdChanged()
{
    load();
}

std::string NotesWidget::notePath() const
{
    // The id can come from a hand-edited layout: hash it into a safe file name.
    const std::string name = std::to_string(std::hash<std::string> {}(instanceId()));
    return base::paths::dataHome() + "/lingmo/filemanager/widget-notes/" + name + ".txt";
}

void NotesWidget::load()
{
    m_saveTimer.stop();
    m_loaded = false;
    std::string text;
    if (!instanceId().empty()) {
        std::ifstream file(notePath(), std::ios::binary);
        if (file) {
            std::ostringstream contents;
            contents << file.rdbuf();
            text = contents.str();
        }
    }
    m_text->setText(std::move(text));
    m_loaded = true;
}

void NotesWidget::save()
{
    if (!m_loaded || instanceId().empty())
        return;
    const std::string path = notePath();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error)
        return;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << m_text->text();
}

void NotesWidget::layoutContent()
{
    const ui::RectF content = contentRect();
    if (content.width <= 0 || content.height <= 0)
        return;
    m_text->setFontSize(size() == WidgetSize::Small ? 11 : size() == WidgetSize::Large ? 15 : 13);
    m_text->setGeometry(content);
}

void NotesWidget::retranslateContent()
{
    m_text->setPlaceholder(tr("Write a note…"));
    m_text->setTextColor(ui::Theme::current().text);
}

} // namespace fm
