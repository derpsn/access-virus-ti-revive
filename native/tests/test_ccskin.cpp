// The Control Center layout reader (src/cc/ccskin) on a hand-written layout in the format of Access' Control Center
// (no Access files needed): nested positions become absolute, colours, bitmaps without "PNG/", expressions.
#include "cc/ccskin.h"
#include "check.h"
#include <string>

using namespace vti::cc;

TEST(ccskin_parse)
{
    std::string xml = R"(<editor><frame width="300" height="200">
        <container left="0" top="0" width="300" height="40" forecolor="ffffffff" backcolor="000000ff" background="PNG/Head.png">
            <control type="radio" left="10" top="5" width="50" height="20" value="1" parameter="X/Tab" bitmap="PNG/Tab.png"/>
        </container>
        <container left="0" top="40" width="300" height="160" backcolor="00000000">
            <container left="5" top="10" width="290" height="140" expression="X/page1" background="PNG/Page.png">
                <control type="textlabel" left="20" top="30" width="100" height="10" parameter="X/Text" bitmap="PNG/Font.png"/>
            </container>
        </container>
    </frame></editor>)";
    SkinLayout l;
    std::string err;
    CHECK(parse_skin(std::vector<uint8_t>(xml.begin(), xml.end()), l, err));
    CHECK(l.width == 300 && l.height == 200 && l.nodes.size() == 2);
    if (l.nodes.size() != 2)
        return;
    auto &head = l.nodes[0];
    CHECK(head.container && head.background == "Head.png" && head.back == 0x000000ffu);
    CHECK(head.children.size() == 1 && head.children[0].type == "radio" && head.children[0].value == 1 &&
          head.children[0].bitmap == "Tab.png" && head.children[0].x == 10 && head.children[0].y == 5);
    auto &page = l.nodes[1].children.at(0);
    CHECK(page.expression == "X/page1" && page.x == 5 && page.y == 50);
    CHECK(page.children.size() == 1 && page.children[0].x == 25 && page.children[0].y == 80 &&
          page.children[0].parameter == "X/Text");
    CHECK(!parse_skin(std::vector<uint8_t>{'<', 'x', '/', '>'}, l, err));
}

// Burn To Flash ROM and OS Updater are not offered (they write the Virus' flash); since 2026-11-19 their tab buttons
// are not even drawn (user: "We remove the Burn/OS thing"), Backup & Restore and everything else stays
TEST(ccskin_burn_and_os_tabs_not_offered)
{
    SkinNode tab;
    tab.type = "radio";
    tab.parameter = "ControlCenter/Tabs/ActiveTab";
    tab.value = 0;
    CHECK(skin_node_offered(tab)); // Backup & Restore
    tab.value = 1;
    CHECK(!skin_node_offered(tab)); // Burn To Flash ROM
    tab.value = 2;
    CHECK(!skin_node_offered(tab)); // OS Updater
    SkinNode other = tab;
    other.parameter = "ControlCenter/SelectVirus/SelectedRow";
    CHECK(skin_node_offered(other));
}
