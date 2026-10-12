#include "ui/widget/Detail.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/TreeView.hpp"

#include <cmath>
#include <cstring>

namespace burnhope {
namespace {

void onTree(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* tree = s->ent[id].try_get_mut<UiTree>();
    if (tree == nullptr || tree->body == kUiNone) {
        return;
    }
    tree->open = tree->open == 0 ? 1 : 0;
    uiShow(*s, tree->body, tree->open != 0);
}

} // namespace

uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text) {
    UiFlex row{};
    row.direction = 1;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = 1;
    row.gap = 6.0f;
    const uint16_t head = spawn(s, parent, row, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm));
    addText(s, head, text != nullptr ? text : "");
    UiFlex body{};
    body.direction = 0;
    body.padL = 16.0f;
    body.shrink = 0.0f;
    const uint16_t child = spawn(s, parent, body, paint(UiRole::Hidden, 0, 0, 0));
    uiShow(s, child, false);
    s.ent[head].set<UiTree>({child, 0});
    uiBind(s, head, onTree, &s);
    return child;
}

} // namespace burnhope
