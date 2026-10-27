#include "Str.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Command.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Theme.hpp"
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
#include "ui/Nodes.hpp"
#include "app/Shell.hpp"

#include <cmath>
#include <spdlog/spdlog.h>
#include <cstring>
#include <ctime>

namespace burnhope {

const char* kPages[3] = {"SCENE", "LOOK", "INPUT"};

SampleShell& shellOf(void* user) {
    return *static_cast<SampleShell*>(user);
}

UiState& uiOf(void* user) {
    return *shellOf(user).state;
}

void setPage(SampleShell& sh, int8_t tag) {
    UiState& s = *sh.state;
    if (tag < 0 || tag > 2) {
        tag = 0;
    }
    SampleShell& ids = sh;
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

void applyCompact(SampleShell& sh) {
    UiState& s = *sh.state;
    const SampleShell& ids = sh;
    if (ids.compactRow == kUiNone || ids.listInner == kUiNone) {
        return;
    }
    const bool on = uiCheckOn(s, ids.compactRow) > 0;
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

void formSubmit(SampleShell& sh) {
    UiState& s = *sh.state;
    const char* why = "";
    const char* msg = uiFormValidate(s, why) ? "sent" : why;
    auto writeStatus = [&](uint16_t id) {
        if (id == kUiNone) {
            return;
        }
        uiText(s, id, msg);
    };
    writeStatus(sh.statusLabel);
    writeStatus(sh.formNote);
    uiNotifyVisualDirty(s);
}

void cmdClear(void* user, uint16_t) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    uiSetFieldText(s, uiField(s), "");
}

void onPage(void* user, uint16_t id) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    const int8_t page = id == uiFindName(s, "btn_scene") ? 0
                       : id == uiFindName(s, "btn_look")  ? 1
                       : id == uiFindName(s, "btn_input") ? 2
                                                          : -1;
    if (page >= 0 && page != sh.page) {
        setPage(sh, page);
    }
    uiNotifyVisualDirty(s);
}

void onMin(void* user, uint16_t) {
    uiRequestMinimize(uiOf(user));
}

void onMax(void* user, uint16_t) {
    uiRequestMaximize(uiOf(user));
}

void onFile(void* user, uint16_t) {
    uiHostNote(uiOf(user)).file = true;
}

void onSend(void* user, uint16_t) {
    formSubmit(shellOf(user));
}

void onFilesHide(void* user, uint16_t) {
    filesShow(uiOf(user), false);
}

void onCompact(void* user, uint16_t) {
    applyCompact(shellOf(user));
}

void cmdScene(void* user, uint16_t) { setPage(shellOf(user), 0); }

void cmdLook(void* user, uint16_t) { setPage(shellOf(user), 1); }

void cmdInput(void* user, uint16_t) { setPage(shellOf(user), 2); }

void cmdCompact(void* user, uint16_t) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    const int on = uiCheckOn(s, sh.compactRow);
    if (on < 0) {
        return;
    }
    uiCheckAssign(s, sh.compactRow, on == 0 ? 1 : 0);
    applyCompact(sh);
    uiNotifyVisualDirty(s);
}

void cmdMin(void* user, uint16_t) { uiRequestMinimize(uiOf(user)); }

void cmdMax(void* user, uint16_t) { uiRequestMaximize(uiOf(user)); }

void cmdTop(void* user, uint16_t) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    SampleShell& ids = sh;
    ids.topOn = !ids.topOn;
    uiRequestTop(s, ids.topOn);
}

void cmdGhost(void* user, uint16_t) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    SampleShell& ids = sh;
    ids.ghost = !ids.ghost;
    uiRequestOpacity(s, ids.ghost ? 0.86f : 1.0f);
}

void cmdFiles(void* user, uint16_t) {
    filesShow(uiOf(user), true);
}

void cmdTheme(void* user, uint16_t) {
    auto& sh = shellOf(user);
    UiState& s = *sh.state;
    SampleShell& ids = sh;
    ids.warm = !ids.warm;
    uiApplyLook(s, ids.warm);
}

void uiBuildShell(UiState& s, SampleShell& shell) {
    shell.state = &s;
    if (!uiThemeLoadAsset(s, "assets/ui/theme.json")) {
        spdlog::warn("sample: assets/ui/theme.json is unavailable, themes fall back to the default palette");
    }
    const uint16_t root = uiImportBinFile(s, kUiNone, BH_UI_SHELL);
    auto take = [&](const char* name) { return uiFindName(s, name); };
    auto link = [&](const char* from, const char* to) {
        const uint16_t a = take(from);
        const uint16_t b = take(to);
        if (a != kUiNone && b != kUiNone) {
            uiSetContextMenu(s, a, b);
        }
    };
    SampleShell& ids = shell;
    ids.pageLabel = take("page_label");
    ids.pages[0] = take("page_scene");
    ids.pages[1] = take("page_look");
    ids.pages[2] = take("page_input");
    ids.compactRow = take("compact");
    ids.listInner = take("list_inner");
    ids.preview = take("preview");
    uiSetField(s, take("field_name"));
    uiFiles(s).fileTarget = take("field_file");
    uiCalSetTarget(s, take("field_date"));
    uiColorSetTarget(s, take("field_color"));
    ids.formNote = take("form_note");
    uiFiles(s).photoView = take("photo");
    ids.statusLabel = take("scale_text");
    ids.sideId = take("side");
    uiSetMenu(s, take("menu_main"));
    uiSetFieldMenu(s, take("field_menu"));
    uiBindName(s, "btn_scene", onPage, &shell);
    uiBindName(s, "btn_look", onPage, &shell);
    uiBindName(s, "btn_input", onPage, &shell);
    uiBindName(s, "compact", onCompact, &shell);
    uiBindName(s, "btn_file", onFile, &shell);
    uiBindName(s, "btn_date", calOnOpen, &s);
    uiBindName(s, "btn_color", colorOnOpen, &s);
    uiBindName(s, "btn_send", onSend, &shell);
    link("menu_file", "menu_file_pop");
    link("menu_edit", "menu_edit_pop");
    uiCommandAddTo(s, take("menu_file_pop"), "SCENE", cmdScene, &shell);
    uiCommandAddTo(s, take("menu_file_pop"), "LOOK", cmdLook, &shell);
    uiCommandAddTo(s, take("menu_file_pop"), "INPUT", cmdInput, &shell);
    uiCommandAddTo(s, take("menu_edit_pop"), "CLEAR", cmdClear, &shell);
    uiCommandAddTo(s, take("menu_edit_pop"), "COMPACT", cmdCompact, &shell);
    uiCommandAdd(s, "SCENE", cmdScene, &shell);
    uiCommandAdd(s, "LOOK", cmdLook, &shell);
    uiCommandAdd(s, "INPUT", cmdInput, &shell);
    uiCommandAdd(s, "COMPACT", cmdCompact, &shell);
    uiCommandAdd(s, "CLEAR", cmdClear, &shell);
    uiCommandAdd(s, "MIN", cmdMin, &shell);
    uiCommandAdd(s, "MAX", cmdMax, &shell);
    uiCommandAdd(s, "ON TOP", cmdTop, &shell);
    uiCommandAdd(s, "GHOST", cmdGhost, &shell);
    uiCommandAdd(s, "THEME", cmdTheme, &shell);
    uiCommandAdd(s, "FILES", cmdFiles, &shell);
    uiCommandAddTo(s, uiFieldMenu(s), "CUT", cmdFieldCut, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "COPY", cmdFieldCopy, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "PASTE", cmdFieldPaste, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "SELECT ALL", cmdFieldAll, &s);
    uiCommandAddTo(s, uiFieldMenu(s), "CLEAR", cmdFieldWipe, &s);
    uiFieldBindContextMenuAll(s, uiFieldMenu(s));
    if (take("input_host") != kUiNone) {
        (void)uiImportBinFile(s, take("input_host"), BH_UI_SAMPLE);
    }
    setPage(shell, 0);

    uiFilesOwn(s, uiFloatWindow(s, root, 70.0f, 64.0f, 1100.0f, 700.0f));
    (void)uiTitleBar(s, uiFiles(s).fileFrame, "FILES", onFilesHide, &shell);

    const uint16_t title = take("title");
    const uint16_t brand = take("brand");
    const uint16_t side = take("side");
    const uint16_t content = take("content");
    const uint16_t status = take("status");
    const uint16_t scroll = take("scene_scroll");
    const uint16_t slider = take("scale");
    uiSetToken(s, root, UiToken::Window);
    uiSetToken(s, title, UiToken::Title);
    uiSetTitleBar(s, title);
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
