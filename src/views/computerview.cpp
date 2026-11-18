// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "views/computerview.h"

#include "core/common.h"
#include "core/standardfolders.h"
#include "model/places.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/locale.h"
#include "base/paths.h"
#include "base/url.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

#include <algorithm>
#include <array>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace fm {

using Lingmo::i18n::format;
using Lingmo::i18n::tr;

namespace {

constexpr double kMargin = 24;
constexpr double kGap = 14;
constexpr double kFolderWidth = 142;
constexpr double kFolderHeight = 132;
constexpr double kDriveHeight = 94;

bool directoryExists(const std::string& path)
{
    struct stat info {};
    return !path.empty() && ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void storageUsage(const std::string& path, uint64_t& used, uint64_t& total)
{
    struct statvfs info {};
    if (::statvfs(path.c_str(), &info) != 0 || info.f_blocks == 0)
        return;
    total = uint64_t(info.f_blocks) * uint64_t(info.f_frsize);
    const uint64_t free = uint64_t(info.f_bavail) * uint64_t(info.f_frsize);
    used = total > free ? total - free : 0;
}

class ComputerCard : public ui::View
{
public:
    ComputerCard(ComputerView::Entry entry, std::function<void(const std::string&)> open)
        : m_entry(std::move(entry))
        , m_open(std::move(open))
    {
        setName("computer-card");
        setTracksHover(true);
        setCursorShape(ui::CursorShape::Pointer);
        m_icon = addChild<ui::ThemeIcon>(m_entry.icon, m_entry.drive ? 42 : 48);
        m_name = addChild<ui::Label>(m_entry.name);
        m_name->setWeight(600);
        m_name->setElide(ui::Label::Elide::Right);
        m_detail = addChild<ui::Label>();
        m_detail->setFontSize(10);
        m_detail->setElide(ui::Label::Elide::Right);
        if (m_entry.drive) {
            const auto& locale = base::Locale::system();
            if (m_entry.total > 0)
                m_detail->setText(format(tr("{used} used of {total}"),
                                         {{"used", locale.formattedDataSize(int64_t(m_entry.used))},
                                          {"total", locale.formattedDataSize(int64_t(m_entry.total))}}));
        } else {
            m_detail->setText(tr("Open folder"));
        }
    }

    void retranslate()
    {
        m_name->setText(m_entry.name);
        if (!m_entry.drive)
            m_detail->setText(tr("Open folder"));
        requestLayout();
    }

protected:
    void layout() override
    {
        const ui::RectF rect = localRect();
        const ui::Theme& theme = ui::Theme::current();
        m_name->setColor(theme.text);
        m_detail->setColor(theme.disabledText);
        if (m_entry.drive) {
            m_icon->setGeometry({14, 14, 42, 42});
            m_name->setGeometry({68, 13, std::max(0.0, rect.width - 80), 22});
            m_detail->setGeometry({68, 38, std::max(0.0, rect.width - 80), 18});
        } else {
            m_icon->setGeometry({std::max(0.0, (rect.width - 48) / 2), 14, 48, 48});
            m_name->setTextAlignment(ui::Label::TextAlignment::Center);
            m_detail->setTextAlignment(ui::Label::TextAlignment::Center);
            m_name->setGeometry({8, 70, std::max(0.0, rect.width - 16), 22});
            m_detail->setGeometry({8, 96, std::max(0.0, rect.width - 16), 18});
        }
    }

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::Color fill = isHovered() ? theme.text.withAlpha(theme.darkMode ? .12 : .07)
                                           : theme.text.withAlpha(theme.darkMode ? .07 : .045);
        painter.drawSmoothRectangle(localRect(), theme.smallRadius, fill);
        if (m_entry.drive && m_entry.total > 0) {
            const double ratio = std::clamp(double(m_entry.used) / double(m_entry.total), 0.0, 1.0);
            const ui::RectF bar {68, 68, std::max(0.0, geometry().width - 82), 7};
            painter.fillRoundedRect(bar, 3.5, theme.text.withAlpha(.12));
            painter.fillRoundedRect({bar.x, bar.y, bar.width * ratio, bar.height}, 3.5,
                                    ratio > .9 ? ui::accent::red : theme.highlight);
        }
    }

    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (event.button == ui::MouseButton::Left && localRect().contains(event.position) && m_open)
            m_open(m_entry.url);
    }

private:
    ComputerView::Entry m_entry;
    std::function<void(const std::string&)> m_open;
    ui::ThemeIcon* m_icon = nullptr;
    ui::Label* m_name = nullptr;
    ui::Label* m_detail = nullptr;
};

} // namespace

ComputerView::ComputerView(std::function<void(const std::string&)> open)
    : m_open(std::move(open))
    , m_places(std::make_unique<PlacesModel>())
{
    setName("computer-view");
    setClipsChildren(true);
    m_foldersTitle = addChild<ui::Label>(tr("My folders"));
    m_foldersTitle->setFontSize(17);
    m_foldersTitle->setWeight(600);
    m_drivesTitle = addChild<ui::Label>(tr("Devices and drives"));
    m_drivesTitle->setFontSize(17);
    m_drivesTitle->setWeight(600);
    m_subscriptions.push_back(m_places->changed.connect([this] { rebuild(); }));
    rebuild();
}

ComputerView::~ComputerView() = default;

void ComputerView::rebuild()
{
    for (ui::View* card : m_cards) {
        std::shared_ptr<ui::View> old = takeChild(card);
        base::EventLoop::current()->post([old] {});
    }
    m_cards.clear();
    m_entries.clear();

    using Lingmo::base::paths::UserDirectory;
    const std::array<std::pair<UserDirectory, const char*>, 6> folders = {{
        {UserDirectory::Desktop, "folder-desktop"},
        {UserDirectory::Videos, "folder-video"},
        {UserDirectory::Music, "folder-music"},
        {UserDirectory::Pictures, "folder-picture"},
        {UserDirectory::Documents, "folder-documents"},
        {UserDirectory::Downloads, "folder-download"},
    }};
    for (const auto& [directory, icon] : folders) {
        const std::string path = Lingmo::base::paths::userDirectory(directory);
        if (!directoryExists(path))
            continue;
        m_entries.push_back({standardFolderName(path), base::url::fromLocalFile(path), icon, false, 0, 0});
    }

    std::vector<Entry> drives;
    Entry root {tr("System Disk"), base::url::fromLocalFile("/"), "drive-harddisk", true, 0, 0};
    storageUsage("/", root.used, root.total);
    drives.push_back(std::move(root));
    for (int i = 0; i < m_places->count(); ++i) {
        const PlacesModel::Place& place = m_places->at(i);
        if (!place.isDevice() || !place.accessible || place.url.empty())
            continue;
        const std::string path = place.path();
        if (path.empty() || path == "/")
            continue;
        Entry drive {place.displayName(), place.url, place.iconName.empty() ? "drive-harddisk" : place.iconName,
                     true, 0, 0};
        storageUsage(path, drive.used, drive.total);
        drives.push_back(std::move(drive));
    }
    m_entries.insert(m_entries.end(), drives.begin(), drives.end());
    for (const Entry& entry : m_entries)
        m_cards.push_back(addChild<ComputerCard>(entry, m_open));
    requestLayout();
    requestPaint();
}

void ComputerView::retranslate()
{
    m_places->retranslate();
    m_foldersTitle->setText(tr("My folders"));
    m_drivesTitle->setText(tr("Devices and drives"));
    requestLayout();
}

void ComputerView::layout()
{
    const ui::RectF rect = localRect();
    const ui::Theme& theme = ui::Theme::current();
    m_foldersTitle->setColor(theme.text);
    m_drivesTitle->setColor(theme.text);
    const double width = std::max(0.0, rect.width - kMargin * 2);
    const int folderCount = int(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return !e.drive; }));
    const int folderColumns = std::max(1, int(std::floor((width + kGap) / (kFolderWidth + kGap))));
    const int folderRows = (folderCount + folderColumns - 1) / folderColumns;
    const double folderWidth = std::max(kFolderWidth, (width - kGap * (folderColumns - 1)) / folderColumns);
    const double titleHeight = std::max(22.0, m_foldersTitle->sizeHint().height);
    m_foldersTitle->setGeometry({kMargin, kMargin, width, titleHeight});
    double y = kMargin + titleHeight + 12;
    int folderIndex = 0;
    for (ui::View* card : m_cards) {
        if (folderIndex >= folderCount)
            break;
        const int row = folderIndex / folderColumns;
        const int column = folderIndex % folderColumns;
        card->setGeometry({kMargin + column * (folderWidth + kGap), y + row * (kFolderHeight + kGap), folderWidth,
                           kFolderHeight});
        ++folderIndex;
    }
    y += folderRows * (kFolderHeight + kGap) + 10;
    m_drivesTitle->setGeometry({kMargin, y, width, titleHeight});
    y += titleHeight + 12;
    const int driveCount = int(m_cards.size()) - folderCount;
    const int driveColumns = std::max(1, int(std::floor((width + kGap) / (300.0 + kGap))));
    const double driveWidth = std::max(220.0, (width - kGap * (driveColumns - 1)) / driveColumns);
    for (int i = 0; i < driveCount; ++i) {
        ui::View* card = m_cards[size_t(folderCount + i)];
        const int row = i / driveColumns;
        const int column = i % driveColumns;
        card->setGeometry({kMargin + column * (driveWidth + kGap), y + row * (kDriveHeight + kGap), driveWidth,
                           kDriveHeight});
    }
}

void ComputerView::paint(ui::Painter& painter)
{
    painter.fillRect(localRect(), ui::Theme::current().secondBackground);
}

} // namespace fm
