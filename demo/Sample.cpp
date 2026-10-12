#include "Str.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"

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
        auto* text = s.ent[ids.pageLabel].try_get_mut<UiText>();
        text->len = static_cast<uint8_t>(strCopyCap(text->bytes, sizeof(text->bytes), kPages[tag]));
        auto* flex = s.ent[ids.pageLabel].try_get_mut<UiFlex>();
        flex->width = uiMeasure(s, text->bytes, text->len, text->len);
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
    auto* inner = s.ent[ids.listInner].try_get_mut<UiFlex>();
    if (inner != nullptr) {
        inner->gap = gap;
    }
    const uint32_t rows = uiNodeChildCount(s, ids.listInner);
    for (uint32_t i = 0; i < rows; ++i) {
        const uint16_t id = uiNodeChild(s, ids.listInner, i);
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (flex == nullptr) {
            continue;
        }
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = itemH;
        flex->pad = pad;
    }
    uiNotifyLayoutDirty(s);
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
    const char* msg = bad ? why : "sent";
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

void cmdClear(void* user) {
    auto& s = *static_cast<UiState*>(user);
    auto* field = s.ent[uiField(s)].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    field->len = 0;
    field->caret = 0;
    field->anchor = 0;
    field->scroll = 0.0f;
    field->bytes[0] = '\0';
    fieldSyncText(s, uiField(s));
    uiNotifyVisualDirty(s);
}

void onPage(void* user, uint16_t, int16_t tag) {
    auto& s = *static_cast<UiState*>(user);
    if (tag != shellIds(s).page) {
        setPage(s, static_cast<int8_t>(tag));
    }
    uiNotifyVisualDirty(s);
}

void onMin(void* user, uint16_t, int16_t) {
    uiRequestMinimize(*static_cast<UiState*>(user));
}

void onMax(void* user, uint16_t, int16_t) {
    uiRequestMaximize(*static_cast<UiState*>(user));
}

void onFile(void* user, uint16_t, int16_t) {
    uiHostNote(*static_cast<UiState*>(user)).file = true;
}

void onSend(void* user, uint16_t, int16_t) {
    formSubmit(*static_cast<UiState*>(user));
}

void onFilesHide(void* user, uint16_t, int16_t) {
    filesShow(*static_cast<UiState*>(user), false);
}

void onCompact(void* user, uint16_t, int16_t) {
    applyCompact(*static_cast<UiState*>(user));
}

void cmdScene(void* user) { setPage(*static_cast<UiState*>(user), 0); }

void cmdLook(void* user) { setPage(*static_cast<UiState*>(user), 1); }

void cmdInput(void* user) { setPage(*static_cast<UiState*>(user), 2); }

void cmdCompact(void* user) {
    auto& s = *static_cast<UiState*>(user);
    auto* check = s.ent[shellIds(s).compactRow].try_get_mut<UiCheck>();
    if (check == nullptr) {
        return;
    }
    check->on = check->on == 0 ? 1 : 0;
    applyCompact(s);
    uiNotifyVisualDirty(s);
}

void cmdMin(void* user) { uiRequestMinimize(*static_cast<UiState*>(user)); }

void cmdMax(void* user) { uiRequestMaximize(*static_cast<UiState*>(user)); }

void cmdTop(void* user) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.topOn = !ids.topOn;
    uiRequestTop(s, ids.topOn);
}

void cmdGhost(void* user) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.ghost = !ids.ghost;
    uiRequestOpacity(s, ids.ghost ? 0.86f : 1.0f);
}

void cmdFiles(void* user) {
    filesShow(*static_cast<UiState*>(user), true);
}

void cmdTheme(void* user) {
    auto& s = *static_cast<UiState*>(user);
    ShellIds& ids = shellIds(s);
    ids.warm = !ids.warm;
    uiApplyLook(s, ids.warm);
}

void uiBuildShell(UiState& s) {
    uiInstallCalendarBin(s);
    uiInstallColorBin(s);
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
    uiCal(s).calTarget = take("field_date");
    uiColor(s).colorTarget = take("field_color");
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
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.ent[id].try_get<UiField>() != nullptr && uiFieldMenu(s) < s.count) {
            uiSetContextMenu(s, id, uiFieldMenu(s));
        }
    }
    if (take("input_host") != kUiNone) {
        (void)uiImportBinFile(s, take("input_host"), BH_UI_SAMPLE);
    }
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
        const uint16_t id = spawn(s, root, flex, paintToken(s.look, UiRole::Panel, UiToken::Window, s.look.radius));
        uiShow(s, id, false);
        return id;
    };
    auto mini = [&](uint16_t host, const char* name, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 28.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, host, flex, paintToken(s.look, UiRole::Button, UiToken::Button, s.look.radiusSm));
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
    uiFiles(s).fileFrame = floatPanel(70.0f, 64.0f, 1100.0f, 700.0f);
    if (uiFiles(s).fileFrame != kUiNone) {
        s.ent[uiFiles(s).fileFrame].set<UiResizable>({});
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
    const uint16_t fileTools = spawn(s, uiFiles(s).fileFrame, fileBar, paintToken(s.look, UiRole::Button, UiToken::Title, 0.0f));
    if (fileTools != kUiNone) {
        s.ent[fileTools].set<UiTitleBar>({});
    }
    addText(s, spawn(s, fileTools, UiFlex{}, paintToken(s.look, UiRole::Label, UiToken::Text, 0.0f)), "FILES");
    UiFlex fileSpacer{};
    fileSpacer.grow = 1.0f;
    fileSpacer.shrink = 1.0f;
    spawn(s, fileTools, fileSpacer, paint(UiRole::Hidden, 0, 0, 0));
    mini(fileTools, "X", onFilesHide);

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
    stamp(s, ids.pageLabel, UiToken::Text);
    stamp(s, ids.compactRow, UiToken::Check);
    stamp(s, scroll, UiToken::Scroll);
    stamp(s, slider, UiToken::Slider);
    stamp(s, ids.preview, UiToken::Preview);
    stamp(s, uiField(s), UiToken::Field);
    stamp(s, status, UiToken::Status);
    stamp(s, ids.statusLabel, UiToken::Muted);
    stamp(s, uiMenu(s), UiToken::Menu);
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
        } else if (node->role == static_cast<uint8_t>(UiRole::Button) && (node->icon == 1 || node->icon == 2)) {
            stamp(s, id, UiToken::Chrome);
        }
    }
    const uint32_t rows = uiNodeChildCount(s, ids.listInner);
    for (uint32_t i = 0; i < rows; ++i) {
        stamp(s, uiNodeChild(s, ids.listInner, i), UiToken::Item);
    }
    uiRestyle(s);
}


} // namespace burnhope
