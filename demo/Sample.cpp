#include "Str.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Command.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "host/HostNote.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/State.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

const char* kPages[3] = {"SCENE", "LOOK", "INPUT"};

struct ShellIds {
    uint16_t pages[3]{kUiNone, kUiNone, kUiNone};
    uint16_t preview = kUiNone;
    uint16_t listInner = kUiNone;
    uint16_t compactRow = kUiNone;
    uint16_t formNote = kUiNone;
    uint16_t sideId = kUiNone;
    uint16_t pageLabel = kUiNone;
    uint16_t statusLabel = kUiNone;
    int8_t page = 0;
    bool topOn = false;
    bool ghost = false;
    bool warm = false;
};

ShellIds& shellIds(UiState& s) {
    return s.world.ensure<ShellIds>();
}

void setPage(UiState& s, int8_t tag) {
    if (tag < 0 || tag > 2) {
        tag = 0;
    }
    ShellIds& ids = shellIds(s);
    ids.page = tag;
    for (int i = 0; i < 3; ++i) {
        if (ids.pages[i] == kUiNone) {
            continue;
        }
        uiShow(s, ids.pages[i], i == tag);
    }
    if (ids.pageLabel != kUiNone) {
        uiText(s, ids.pageLabel, kPages[tag]);
        (void)uiButtonFitText(s, ids.pageLabel, kPages[tag], 0.0f);
    }
    uiNotifyLayoutDirty(s);
    uiNotifyVisualDirty(s);
}

void applyCompact(UiState& s) {
    const ShellIds& ids = shellIds(s);
    if (ids.compactRow == kUiNone || ids.listInner == kUiNone) {
        return;
    }
    const UiCheck* check = s.ent[ids.compactRow].try_get<UiCheck>();
    const bool on = check != nullptr && check->on != 0;
    const float itemH = on ? 24.0f : 36.0f;
    const float gap = on ? 2.0f : 8.0f;
    const float pad = on ? 2.0f : 8.0f;
    uiSetGap(s, ids.listInner, gap);
    const uint32_t rows = uiNodeChildCount(s, ids.listInner);
    for (uint32_t i = 0; i < rows; ++i) {
        const uint16_t id = uiNodeChild(s, ids.listInner, i);
        uiSetSize(s, id, -1.0f, itemH);
        uiSetPad(s, id, pad);
    }
    uiNotifyLayoutDirty(s);
}

void formSubmit(UiState& s) {
    const char* why = "";
    const char* msg = uiFormValidate(s, why) ? "sent" : why;
    auto writeStatus = [&](uint16_t id) {
        if (id == kUiNone) {
            return;
        }
        uiText(s, id, msg);
    };
    writeStatus(shellIds(s).statusLabel);
    writeStatus(shellIds(s).formNote);
    uiNotifyVisualDirty(s);
}

void cmdClear(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiSetFieldText(s, uiField(s), "");
}

void onPage(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const int8_t page = id == uiFindName(s, "btn_scene") ? 0
                       : id == uiFindName(s, "btn_look")  ? 1
                       : id == uiFindName(s, "btn_input") ? 2
                                                          : -1;
    if (page >= 0 && page != shellIds(s).page) {
        setPage(s, page);
    }
    uiNotifyVisualDirty(s);
}

void onMin(void* user, uint16_t) {
    uiRequestMinimize(*static_cast<UiState*>(user));
}

void onMax(void* user, uint16_t) {
    uiRequestMaximize(*static_cast<UiState*>(user));
}

void onFile(void* user, uint16_t) {
    uiHostNote(*static_cast<UiState*>(user)).file = true;
}

void onSend(void* user, uint16_t) {
    formSubmit(*static_cast<UiState*>(user));
}

void onFilesHide(void* user, uint16_t) {
    filesShow(*static_cast<UiState*>(user), false);
}

void onCompact(void* user, uint16_t) {
    applyCompact(*static_cast<UiState*>(user));
}

void cmdScene(void* user, uint16_t) { setPage(*static_cast<UiState*>(user), 0); }

void cmdLook(void* user, uint16_t) { setPage(*static_cast<UiState*>(user), 1); }

void cmdInput(void* user, uint16_t) { setPage(*static_cast<UiState*>(user), 2); }

void cmdCompact(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    const int on = uiCheckOn(s, shellIds(s).compactRow);
    if (on < 0) {
        return;
    }
    uiCheckAssign(s, shellIds(s).compactRow, on == 0 ? 1 : 0);
    applyCompact(s);
    uiNotifyVisualDirty(s);
}

void cmdMin(void* user, uint16_t) { uiRequestMinimize(*static_cast<UiState*>(user)); }

void cmdMax(void* user, uint16_t) { uiRequestMaximize(*static_cast<UiState*>(user)); }

void cmdTop(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.topOn = !ids.topOn;
    uiRequestTop(s, ids.topOn);
}

void cmdGhost(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.ghost = !ids.ghost;
    uiRequestOpacity(s, ids.ghost ? 0.86f : 1.0f);
}

void cmdFiles(void* user, uint16_t) {
    filesShow(*static_cast<UiState*>(user), true);
}

void cmdTheme(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.warm = !ids.warm;
    uiApplyLook(s, ids.warm);
}

void uiBuildShell(UiState& s) {
    const uint16_t root = uiImportBinFile(s, kUiNone, BH_UI_SHELL);
    auto take = [&](const char* name) { return uiFindName(s, name); };
    auto link = [&](const char* from, const char* to) {
        const uint16_t a = take(from);
        const uint16_t b = take(to);
        if (a < s.count && b < s.count) {
            uiSetContextMenu(s, a, b);
        }
    };
    ShellIds& ids = shellIds(s);
    ids.pageLabel = take("page_label");
    ids.pages[0] = take("page_scene");
    ids.pages[1] = take("page_look");
    ids.pages[2] = take("page_input");
    ids.compactRow = take("compact");
    ids.listInner = take("list_inner");
    ids.preview = take("preview");
    uiSetField(s, take("field_name"));
    uiFiles(s).fileTarget = take("field_file");
    if (UiCalendarComponent* cal = uiCal(s)) {
        cal->calTarget = take("field_date");
    }
    if (UiColorPickerComponent* color = uiColor(s)) {
        color->colorTarget = take("field_color");
    }
    ids.formNote = take("form_note");
    uiFiles(s).photoView = take("photo");
    ids.statusLabel = take("scale_text");
    ids.sideId = take("side");
    uiSetMenu(s, take("menu_main"));
    uiSetFieldMenu(s, take("field_menu"));
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
    uiCommandAddTo(s, uiFieldMenu(s), "CUT", cmdFieldCut, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "COPY", cmdFieldCopy, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "PASTE", cmdFieldPaste, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "SELECT ALL", cmdFieldAll, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "CLEAR", cmdFieldWipe, &s);
    uiFieldBindContextMenuAll(s, uiFieldMenu(s));
    if (take("input_host") != kUiNone) {
        (void)uiImportBinFile(s, take("input_host"), BH_UI_SAMPLE);
    }
    setPage(s, 0);

    auto floatPanel = [&](float x, float y, float w, float h) {
        UiFlex flex{};
        flex.position = UiPosition::Absolute;
        flex.posX = x;
        flex.posY = y;
        flex.widthMode = UiSize::Px;
        flex.width = w;
        flex.heightMode = UiSize::Px;
        flex.height = h;
        flex.pad = 10.0f;
        flex.gap = 6.0f;
        flex.shrink = 0.0f;
        const uint16_t id = uiPanel(s, root, flex, paintToken(s.look, UiToken::Window, s.look.radius));
        uiShow(s, id, false);
        return id;
    };
    uiFilesOwn(s, floatPanel(70.0f, 64.0f, 1100.0f, 700.0f));
    if (uiFiles(s).fileFrame != kUiNone) {
        s.ent[uiFiles(s).fileFrame].set<UiResizable>({});
    }
    (void)uiTitleBar(s, uiFiles(s).fileFrame, "FILES", onFilesHide, &s);

    const uint16_t title = take("title");
    const uint16_t brand = take("brand");
    const uint16_t side = take("side");
    const uint16_t content = take("content");
    const uint16_t status = take("status");
    const uint16_t scroll = take("scene_scroll");
    const uint16_t slider = take("scale");
    uiSetToken(s, root, UiToken::Window);
    uiSetToken(s, title, UiToken::Title);
    if (title < s.count) {
        s.ent[title].set<UiTitleBar>({});
    }
    uiSetToken(s, brand, UiToken::Accent);
    uiSetToken(s, side, UiToken::Side);
    uiSetToken(s, content, UiToken::Content);
    uiSetToken(s, ids.pageLabel, UiToken::Text);
    uiSetToken(s, ids.compactRow, UiToken::Check);
    uiSetToken(s, scroll, UiToken::Scroll);
    uiSetToken(s, slider, UiToken::Slider);
    uiSetToken(s, ids.preview, UiToken::Preview);
    uiSetToken(s, uiField(s), UiToken::Field);
    uiSetToken(s, status, UiToken::Status);
    uiSetToken(s, ids.statusLabel, UiToken::Muted);
    uiSetToken(s, uiMenu(s), UiToken::Menu);
    const uint32_t rows = uiNodeChildCount(s, ids.listInner);
    for (uint32_t i = 0; i < rows; ++i) {
        uiSetToken(s, uiNodeChild(s, ids.listInner, i), UiToken::Item);
    }
    uiRestyle(s);
}


} // namespace burnhope
