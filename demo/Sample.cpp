#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

const char* kPages[3] = {"SCENE", "LOOK", "INPUT"};

void setPage(UiState& s, int8_t tag) {
    if (tag < 0 || tag > 2) {
        tag = 0;
    }
    s.selected = tag;
    for (int i = 0; i < 3; ++i) {
        if (s.pages[i] == kUiNone || s.yoga[s.pages[i]] == nullptr) {
            continue;
        }
        YGNodeStyleSetDisplay(s.yoga[s.pages[i]], i == tag ? YGDisplayFlex : YGDisplayNone);
    }
    if (s.pageLabel != kUiNone) {
        auto* text = s.ent[s.pageLabel].try_get_mut<UiText>();
        copyText(text->bytes, text->len, kPages[tag]);
        auto* flex = s.ent[s.pageLabel].try_get_mut<UiFlex>();
        flex->width = uiMeasure(s, text->bytes, text->len, text->len);
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void applyCompact(UiState& s) {
    if (s.compactRow == kUiNone || s.listInner == kUiNone) {
        return;
    }
    const UiCheck* check = s.ent[s.compactRow].try_get<UiCheck>();
    const bool on = check != nullptr && check->on != 0;
    const float itemH = on ? 24.0f : 36.0f;
    const float gap = on ? 2.0f : 8.0f;
    const float pad = on ? 2.0f : 8.0f;
    auto* inner = s.ent[s.listInner].try_get_mut<UiFlex>();
    if (inner != nullptr) {
        inner->gap = gap;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.parentOf[id] != s.listInner) {
            continue;
        }
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (flex == nullptr) {
            continue;
        }
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = itemH;
        flex->pad = pad;
    }
    s.layoutDirty = true;
}

void formSubmit(UiState& s) {
    const char* why = "";
    bool bad = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiField* field = s.ent[id].try_get<UiField>();
        if (field == nullptr || field->inForm == 0) {
            continue;
        }
        fieldMark(s, id);
        const char* msg = "";
        if (!bad && fieldProblem(*field, msg)) {
            bad = true;
            why = msg;
        }
    }
    setNote(s, bad ? why : "sent");
}

void cmdClear(void* user) {
    auto& s = *static_cast<UiState*>(user);
    auto* field = s.ent[s.field].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    field->len = 0;
    field->caret = 0;
    field->anchor = 0;
    field->scroll = 0.0f;
    field->bytes[0] = '\0';
    s.visualDirty = true;
}

void onPage(void* user, uint16_t, int16_t tag) {
    auto& s = *static_cast<UiState*>(user);
    if (tag != s.selected) {
        setPage(s, static_cast<int8_t>(tag));
    }
    s.visualDirty = true;
}

void onMin(void* user, uint16_t, int16_t) {
    static_cast<UiState*>(user)->opMinimize = true;
}

void onMax(void* user, uint16_t, int16_t) {
    static_cast<UiState*>(user)->opMaximize = true;
}

void onFile(void* user, uint16_t, int16_t) {
    static_cast<UiState*>(user)->opFile = true;
}

void onSend(void* user, uint16_t, int16_t) {
    formSubmit(*static_cast<UiState*>(user));
}

void onCompact(void* user, uint16_t, int16_t) {
    applyCompact(*static_cast<UiState*>(user));
}

void cmdScene(void* user) { setPage(*static_cast<UiState*>(user), 0); }

void cmdLook(void* user) { setPage(*static_cast<UiState*>(user), 1); }

void cmdInput(void* user) { setPage(*static_cast<UiState*>(user), 2); }

void cmdCompact(void* user) {
    auto& s = *static_cast<UiState*>(user);
    auto* check = s.ent[s.compactRow].try_get_mut<UiCheck>();
    if (check == nullptr) {
        return;
    }
    check->on = check->on == 0 ? 1 : 0;
    applyCompact(s);
    s.visualDirty = true;
}

void cmdMin(void* user) { static_cast<UiState*>(user)->opMinimize = true; }

void cmdMax(void* user) { static_cast<UiState*>(user)->opMaximize = true; }

void cmdTop(void* user) {
    auto& s = *static_cast<UiState*>(user);
    s.topOn = !s.topOn;
    s.opTop = s.topOn ? 1 : 0;
}

void cmdGhost(void* user) {
    auto& s = *static_cast<UiState*>(user);
    s.ghost = !s.ghost;
    s.opOpacity = s.ghost ? 0.86f : 1.0f;
}

void cmdFiles(void* user) {
    auto& s = *static_cast<UiState*>(user);
    if (s.onClick != nullptr) {
        s.onClick(s.clickUser, kUiNone, -94);
    }
}

void cmdTheme(void* user) {
    auto& s = *static_cast<UiState*>(user);
    s.altLook = !s.altLook;
    const float glyphH = s.look.glyphH;
    const float glyphW = s.look.glyphW;
    const float advance = s.look.advance;
    const float title = s.look.titleBar;
    const float border = s.look.resizeBorder;
    s.look = s.altLook ? uiLookWarm() : UiLook{};
    s.look.glyphH = glyphH;
    s.look.glyphW = glyphW;
    s.look.advance = advance;
    s.look.titleBar = title;
    s.look.resizeBorder = border;
    uiRestyle(s);
}

void uiBuildShell(UiState& s) {
    const uint16_t root = uiImportBinFile(s, kUiNone, BH_UI_SHELL);
    auto take = [&](const char* name) { return uiFindName(s, name); };
    auto link = [&](const char* from, const char* to) {
        const uint16_t a = take(from);
        const uint16_t b = take(to);
        if (a < s.count && b < s.count) {
            s.contextOf[a] = b;
        }
    };
    s.pageLabel = take("page_label");
    s.pages[0] = take("page_scene");
    s.pages[1] = take("page_look");
    s.pages[2] = take("page_input");
    s.compactRow = take("compact");
    s.listInner = take("list_inner");
    s.preview = take("preview");
    s.field = take("field_name");
    s.fileTarget = take("field_file");
    s.calTarget = take("field_date");
    s.colorTarget = take("field_color");
    s.formNote = take("form_note");
    s.photoView = take("photo");
    s.statusLabel = take("scale_text");
    s.sideId = take("side");
    s.menu = take("menu_main");
    s.fieldMenu = take("field_menu");
    uiBindName(s, "btn_scene", onPage, &s);
    uiBindName(s, "btn_look", onPage, &s);
    uiBindName(s, "btn_input", onPage, &s);
    uiBindName(s, "compact", onCompact, &s);
    uiBindName(s, "btn_file", onFile, &s);
    uiBindName(s, "btn_date", calOnOpen, &s);
    uiBindName(s, "btn_color", colorOnOpen, &s);
    uiBindName(s, "btn_send", onSend, &s);
    link("menu_file", "menu_file_pop");
    link("menu_edit", "menu_edit_pop");
    uiCommandAddTo(s, take("menu_file_pop"), "SCENE", cmdScene, &s);
    uiCommandAddTo(s, take("menu_file_pop"), "LOOK", cmdLook, &s);
    uiCommandAddTo(s, take("menu_file_pop"), "INPUT", cmdInput, &s);
    uiCommandAddTo(s, take("menu_edit_pop"), "CLEAR", cmdClear, &s);
    uiCommandAddTo(s, take("menu_edit_pop"), "COMPACT", cmdCompact, &s);
    uiCommandAdd(s, "SCENE", cmdScene, &s);
    uiCommandAdd(s, "LOOK", cmdLook, &s);
    uiCommandAdd(s, "INPUT", cmdInput, &s);
    uiCommandAdd(s, "COMPACT", cmdCompact, &s);
    uiCommandAdd(s, "CLEAR", cmdClear, &s);
    uiCommandAdd(s, "MIN", cmdMin, &s);
    uiCommandAdd(s, "MAX", cmdMax, &s);
    uiCommandAdd(s, "ON TOP", cmdTop, &s);
    uiCommandAdd(s, "GHOST", cmdGhost, &s);
    uiCommandAdd(s, "THEME", cmdTheme, &s);
    uiCommandAdd(s, "FILES", cmdFiles, &s);
    uiCommandAddTo(s, s.fieldMenu, "CUT", cmdFieldCut, &s);
    uiCommandAddTo(s, s.fieldMenu, "COPY", cmdFieldCopy, &s);
    uiCommandAddTo(s, s.fieldMenu, "PASTE", cmdFieldPaste, &s);
    uiCommandAddTo(s, s.fieldMenu, "SELECT ALL", cmdFieldAll, &s);
    uiCommandAddTo(s, s.fieldMenu, "CLEAR", cmdFieldWipe, &s);
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.ent[id].try_get<UiField>() != nullptr && s.fieldMenu < s.count) {
            s.contextOf[id] = s.fieldMenu;
        }
    }
    if (take("input_host") != kUiNone) {
        (void)uiImportBinFile(s, take("input_host"), BH_UI_SAMPLE);
    }
    (void)jsonLoadFile(s, "assets/ui/sample.json");
    setPage(s, 0);

    auto floatPanel = [&](float x, float y, float w, float h) {
        UiFlex flex{};
        flex.position = 1;
        flex.posX = x;
        flex.posY = y;
        flex.widthMode = static_cast<uint8_t>(UiSize::Px);
        flex.width = w;
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = h;
        flex.pad = 10.0f;
        flex.gap = 6.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, root, flex, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f));
        uiShow(s, id, false);
        return id;
    };
    auto mini = [&](uint16_t host, const char* name, int8_t tag, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 28.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, host, flex, paint(UiRole::Button, 0.20f, 0.24f, 0.32f, 6.0f, tag));
        addText(s, id, name);
        if (auto* box = s.ent[id].try_get_mut<UiFlex>()) {
            const UiText* label = s.ent[id].try_get<UiText>();
            const uint8_t n = label != nullptr ? label->len : 1;
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = static_cast<float>(n) * s.look.advance + 24.0f;
            box->height = 28.0f;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
        }
        uiBind(s, id, fn, &s);
        return id;
    };
    s.fileFrame = floatPanel(70.0f, 64.0f, 1100.0f, 700.0f);
    if (auto* framePaint = s.ent[s.fileFrame].try_get_mut<UiPaint>()) {
        framePaint->tag = -15;
    }
    UiFlex fileBar{};
    fileBar.direction = 1;
    fileBar.shrink = 0.0f;
    fileBar.widthMode = static_cast<uint8_t>(UiSize::Percent);
    fileBar.width = 100.0f;
    fileBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    fileBar.height = 32.0f;
    fileBar.gap = 8.0f;
    fileBar.pad = 8.0f;
    const uint16_t fileTools = spawn(s, s.fileFrame, fileBar, paint(UiRole::Button, 0.13f, 0.14f, 0.17f, 0.0f, -6));
    addText(s, spawn(s, fileTools, UiFlex{}, paint(UiRole::Label, 0.9f, 0.92f, 0.96f)), "FILES");
    UiFlex fileSpacer{};
    fileSpacer.grow = 1.0f;
    fileSpacer.shrink = 1.0f;
    spawn(s, fileTools, fileSpacer, paint(UiRole::Hidden, 0, 0, 0));
    mini(fileTools, "X", -93, nullptr);

    const uint16_t title = take("title");
    const uint16_t brand = take("brand");
    const uint16_t side = take("side");
    const uint16_t content = take("content");
    const uint16_t status = take("status");
    const uint16_t scroll = take("scene_scroll");
    const uint16_t slider = take("scale");
    stamp(s, root, UiToken::Window);
    stamp(s, title, UiToken::Title);
    stamp(s, brand, UiToken::Accent);
    stamp(s, side, UiToken::Side);
    stamp(s, content, UiToken::Content);
    stamp(s, s.pageLabel, UiToken::Text);
    stamp(s, s.compactRow, UiToken::Check);
    stamp(s, scroll, UiToken::Scroll);
    stamp(s, slider, UiToken::Slider);
    stamp(s, s.preview, UiToken::Preview);
    stamp(s, s.field, UiToken::Field);
    stamp(s, status, UiToken::Status);
    stamp(s, s.statusLabel, UiToken::Muted);
    stamp(s, s.menu, UiToken::Menu);
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* node = s.ent[id].try_get<UiPaint>();
        if (node == nullptr || node->token != static_cast<uint8_t>(UiToken::Custom)) {
            continue;
        }
        if (node->role == static_cast<uint8_t>(UiRole::Button) && node->tag >= 0 && node->tag < 3) {
            stamp(s, id, UiToken::Button);
        } else if (node->role == static_cast<uint8_t>(UiRole::Label)) {
            stamp(s, id, UiToken::Text);
        } else if (node->role == static_cast<uint8_t>(UiRole::Close)) {
            stamp(s, id, UiToken::Close);
        } else if (node->role == static_cast<uint8_t>(UiRole::Button) && (node->tag == -2 || node->tag == -3)) {
            stamp(s, id, UiToken::Chrome);
        } else if (s.parentOf[id] == s.listInner) {
            stamp(s, id, UiToken::Item);
        }
    }
    if (s.valueN < 4) {
        s.valueFn[s.valueN] = colorOnValue;
        s.valueUser[s.valueN] = &s;
        ++s.valueN;
    }
    uiRestyle(s);
}


} // namespace burnhope
