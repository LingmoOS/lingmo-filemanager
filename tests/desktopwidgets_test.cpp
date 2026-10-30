// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "testutil.h"

#include "desktop/widgets/widget.h"
#include "desktop/widgets/widgetconfig.h"
#include "desktop/widgets/widgetgallery.h"
#include "desktop/widgets/widgetlayer.h"
#include "ui/application.h"
#include "ui/test_input.h"
#include "ui/view.h"
#include "ui/window.h"

#include <cmath>

#include <gtest/gtest.h>

using namespace Lingmo;

namespace {

class WidgetLayerTest : public testing::Test
{
protected:
    ui::Window window;
    fm::WidgetLayer* layer = nullptr;
    ui::View* rootView = nullptr;

    void SetUp() override
    {
        auto root = std::make_unique<ui::View>();
        rootView = root.get();
        window.setContentView(std::move(root));
        window.resize({800, 600});
        window.show();
        auto widgets = std::make_unique<fm::WidgetLayer>();
        layer = widgets.get();
        rootView->addChild(std::move(widgets));
        layer->setGeometry({0, 0, 800, 600});
        layer->setWorkArea({0, 32, 800, 568});
        window.grab();
    }
};

} // namespace

TEST(WidgetConfigTest, PlacementsRoundTrip)
{
    const std::vector<fm::WidgetPlacement> placements = {
            {fm::WidgetKind::Clock, fm::WidgetSize::Medium, 24, 40},
            {fm::WidgetKind::Calendar, fm::WidgetSize::Large, 200, 80},
            {fm::WidgetKind::System, fm::WidgetSize::Small, -8, 0},
    };
    const std::vector<fm::WidgetPlacement> parsed = fm::parseWidgets(fm::serializeWidgets(placements));
    ASSERT_EQ(parsed.size(), placements.size());
    for (size_t i = 0; i < placements.size(); ++i) {
        EXPECT_EQ(parsed[i].kind, placements[i].kind);
        EXPECT_EQ(parsed[i].size, placements[i].size);
        EXPECT_DOUBLE_EQ(parsed[i].x, placements[i].x);
        EXPECT_DOUBLE_EQ(parsed[i].y, placements[i].y);
    }
    EXPECT_TRUE(fm::parseWidgets("not json").empty());
}

TEST(WidgetConfigTest, UnknownKindsAndSizesAreDropped)
{
    const std::string json = R"([
        {"kind":"weather","size":"medium","x":1,"y":2},
        {"kind":"clock","size":"huge","x":3,"y":4},
        {"kind":"clock","size":"small","x":5,"y":6}
    ])";
    const std::vector<fm::WidgetPlacement> parsed = fm::parseWidgets(json);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].kind, fm::WidgetKind::Clock);
    EXPECT_EQ(parsed[0].size, fm::WidgetSize::Small);
    EXPECT_DOUBLE_EQ(parsed[0].x, 5);
    EXPECT_DOUBLE_EQ(parsed[0].y, 6);
}

TEST(WidgetConfigTest, SizesAreDistinctAndPositive)
{
    EXPECT_GT(fm::widgetSizeSpec(fm::WidgetSize::Small).width, 0);
    EXPECT_GT(fm::widgetSizeSpec(fm::WidgetSize::Medium).width,
              fm::widgetSizeSpec(fm::WidgetSize::Small).width);
    EXPECT_GT(fm::widgetSizeSpec(fm::WidgetSize::Large).height,
              fm::widgetSizeSpec(fm::WidgetSize::Medium).height);
}

TEST_F(WidgetLayerTest, AddsAndRemovesCards)
{
    EXPECT_TRUE(layer->widgets().empty());
    layer->addWidget(fm::WidgetKind::Clock);
    ASSERT_EQ(layer->widgets().size(), 1u);
    EXPECT_EQ(layer->widgets().front()->kind(), fm::WidgetKind::Clock);
    ASSERT_EQ(layer->placements().size(), 1u);
    EXPECT_EQ(layer->placements().front().kind, fm::WidgetKind::Clock);

    layer->removeWidget(layer->widgets().front());
    EXPECT_TRUE(layer->widgets().empty());
    EXPECT_TRUE(layer->placements().empty());
}

TEST_F(WidgetLayerTest, PlacedCardsStayInsideTheWorkArea)
{
    layer->setPlacements({{fm::WidgetKind::Calendar, fm::WidgetSize::Large, 5000, -500}});
    ASSERT_EQ(layer->placements().size(), 1u);
    const fm::WidgetPlacement placement = layer->placements().front();
    const ui::SizeF size = fm::widgetSizeSpec(fm::WidgetSize::Large);
    EXPECT_GE(placement.x, 0);
    EXPECT_GE(placement.y, 32);
    EXPECT_LE(placement.x + size.width, 800);
    EXPECT_LE(placement.y + size.height, 600);
}

TEST_F(WidgetLayerTest, EditingReachesEveryCard)
{
    layer->addWidget(fm::WidgetKind::Clock);
    layer->addWidget(fm::WidgetKind::System);
    ASSERT_EQ(layer->widgets().size(), 2u);
    layer->setEditing(true);
    EXPECT_TRUE(layer->editing());
    for (fm::DesktopWidget* widget : layer->widgets())
        EXPECT_TRUE(widget->editing());
    layer->setEditing(false);
    for (fm::DesktopWidget* widget : layer->widgets())
        EXPECT_FALSE(widget->editing());
}

TEST_F(WidgetLayerTest, CardsDragAndSnapToTheGrid)
{
    layer->addWidget(fm::WidgetKind::Clock);
    window.grab();
    fm::DesktopWidget* card = layer->widgets().front();
    const fm::WidgetPlacement before = card->placement();

    const ui::PointF press = card->mapToWindow({40, 12});
    ui::TestInput::press(window, press);
    ui::TestInput::movePointer(window, {press.x + 64, press.y + 40});
    ui::TestInput::release(window, {press.x + 64, press.y + 40});
    window.grab();

    const fm::WidgetPlacement after = card->placement();
    EXPECT_NE(after.x, before.x);
    EXPECT_NE(after.y, before.y);
    // Released, a card sits on the 8 px grid.
    EXPECT_DOUBLE_EQ(std::fmod(after.x, 8.0), 0.0);
    EXPECT_DOUBLE_EQ(std::fmod(after.y, 8.0), 0.0);
}

TEST_F(WidgetLayerTest, TheGalleryDrawsOverTheDesktop)
{
    auto gallery = std::make_unique<fm::WidgetGallery>();
    gallery->setGeometry({0, 0, 800, 600});
    gallery->retranslate();
    gallery->setVisible(true);
    rootView->addChild(std::move(gallery));
    window.grab();

    // The desktop under the dim still works: a card can be added while the
    // gallery is open.
    layer->addWidget(fm::WidgetKind::System);
    EXPECT_EQ(layer->widgets().size(), 1u);
    window.grab();
}
