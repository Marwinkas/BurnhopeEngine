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
    UiFlex col{};
    col.direction = 0;
    col.shrink = 1.0f;
    UiFlex row = col;
    row.direction = 1;

    UiFlex rootF = col;
    const uint16_t root = spawn(s, kUiNone, rootF, paint(UiRole::Panel, 0.102f, 0.106f, 0.122f));

    UiFlex titleF = row;
    titleF.heightMode = static_cast<uint8_t>(UiSize::Px);
    titleF.height = 40.0f;
    titleF.shrink = 0.0f;
    titleF.align = 1;
    const uint16_t title = spawn(s, root, titleF, paint(UiRole::Panel, 0.145f, 0.153f, 0.176f));

    UiFlex brandF{};
    brandF.marginL = 14.0f;
    brandF.align = 1;
    brandF.shrink = 1.0f;
    const uint16_t brand = spawn(s, title, brandF, paint(UiRole::Label, 0.36f, 0.55f, 0.92f));
    addText(s, brand, "BURNHOPE");

    UiFlex gapF{};
    gapF.grow = 1.0f;
    gapF.shrink = 1.0f;
    gapF.heightMode = static_cast<uint8_t>(UiSize::Px);
    gapF.height = 1.0f;
    spawn(s, title, gapF, paint(UiRole::Hidden, 0, 0, 0));

    UiFlex chrome = row;
    chrome.widthMode = static_cast<uint8_t>(UiSize::Px);
    chrome.width = 100.0f;
    chrome.heightMode = static_cast<uint8_t>(UiSize::Px);
    chrome.height = 28.0f;
    chrome.shrink = 0.0f;
    chrome.align = 1;
    chrome.justify = 1;
    chrome.gap = 4.0f;
    chrome.marginR = 8.0f;
    const uint16_t cluster = spawn(s, title, chrome, paint(UiRole::Hidden, 0, 0, 0));
    const uint16_t minB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Button, 0.22f, 0.24f, 0.28f, 6.0f, -2));
    const uint16_t maxB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Button, 0.22f, 0.24f, 0.28f, 6.0f, -3));
    const uint16_t closeB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Close, 0.55f, 0.28f, 0.28f, 6.0f));
    if (auto* icon = s.ent[minB].try_get_mut<UiPaint>()) {
        icon->icon = 1;
    }
    if (auto* icon = s.ent[maxB].try_get_mut<UiPaint>()) {
        icon->icon = 2;
    }
    if (auto* icon = s.ent[closeB].try_get_mut<UiPaint>()) {
        icon->icon = 3;
    }
    uiBind(s, minB, onMin, &s);
    uiBind(s, maxB, onMax, &s);

    UiFlex barF = row;
    barF.heightMode = static_cast<uint8_t>(UiSize::Px);
    barF.height = 30.0f;
    barF.shrink = 0.0f;
    barF.align = 1;
    barF.pad = 8.0f;
    barF.gap = 16.0f;
    const uint16_t bar = spawn(s, root, barF, paint(UiRole::Panel, 0.12f, 0.13f, 0.16f));
    auto makeBar = [&](const char* name, uint16_t& outBtn) {
        UiFlex item{};
        item.heightMode = static_cast<uint8_t>(UiSize::Px);
        item.height = 22.0f;
        item.shrink = 0.0f;
        UiPaint btn = paint(UiRole::Button, 0.93f, 0.94f, 0.96f, 0.0f, -7);
        btn.a = 0.0f;
        outBtn = spawn(s, bar, item, btn);
        addText(s, outBtn, name);
    };
    uint16_t fileBtn = kUiNone;
    uint16_t editBtn = kUiNone;
    makeBar("FILE", fileBtn);
    makeBar("EDIT", editBtn);
    auto popup = [&]() {
        UiFlex menuFlex{};
        menuFlex.position = 1;
        menuFlex.widthMode = static_cast<uint8_t>(UiSize::Px);
        menuFlex.width = s.menuW;
        menuFlex.pad = s.menuPad;
        menuFlex.gap = s.menuGap;
        menuFlex.shrink = 0.0f;
        const uint16_t id = spawn(s, root, menuFlex, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f));
        if (s.yoga[id] != nullptr) {
            YGNodeStyleSetDisplay(s.yoga[id], YGDisplayNone);
        }
        return id;
    };
    const uint16_t fileMenu = popup();
    const uint16_t editMenu = popup();
    s.contextOf[fileBtn] = fileMenu;
    s.contextOf[editBtn] = editMenu;
    uiCommandAddTo(s, fileMenu, "SCENE", cmdScene, &s);
    uiCommandAddTo(s, fileMenu, "LOOK", cmdLook, &s);
    uiCommandAddTo(s, fileMenu, "INPUT", cmdInput, &s);
    uiCommandAddTo(s, editMenu, "CLEAR", cmdClear, &s);
    uiCommandAddTo(s, editMenu, "COMPACT", cmdCompact, &s);

    UiFlex bodyF = row;
    bodyF.grow = 1.0f;
    bodyF.shrink = 1.0f;
    const uint16_t body = spawn(s, root, bodyF, paint(UiRole::Hidden, 0, 0, 0));

    UiFlex sideF = col;
    sideF.widthMode = static_cast<uint8_t>(UiSize::Px);
    sideF.width = 220.0f;
    sideF.shrink = 0.0f;
    sideF.pad = 10.0f;
    sideF.gap = 8.0f;
    const uint16_t side = spawn(s, body, sideF, paint(UiRole::Panel, 0.118f, 0.125f, 0.145f));
    s.sideId = side;

    UiFlex splitF = px(6.0f, 0.0f);
    splitF.heightMode = static_cast<uint8_t>(UiSize::Auto);
    splitF.grow = 0.0f;
    splitF.shrink = 0.0f;
    spawn(s, body, splitF, paint(UiRole::Panel, 0.28f, 0.32f, 0.40f, 0.0f, -5));

    const float idle[3][3] = {
        {0.20f, 0.24f, 0.32f},
        {0.20f, 0.28f, 0.24f},
        {0.30f, 0.24f, 0.20f},
    };
    for (int i = 0; i < 3; ++i) {
        UiFlex btnF{};
        btnF.heightMode = static_cast<uint8_t>(UiSize::Px);
        btnF.height = 36.0f;
        btnF.shrink = 0.0f;
        btnF.align = 1;
        btnF.justify = 1;
        const uint16_t btn = spawn(s, side, btnF, paint(UiRole::Button, idle[i][0], idle[i][1], idle[i][2], 6.0f, static_cast<int8_t>(i)));
        uiBind(s, btn, onPage, &s);
        UiFlex labF{};
        labF.align = 1;
        const uint16_t lab = spawn(s, btn, labF, paint(UiRole::Label, 0.93f, 0.94f, 0.96f));
        addText(s, lab, kPages[i]);
    }

    UiFlex sideGap{};
    sideGap.grow = 1.0f;
    spawn(s, side, sideGap, paint(UiRole::Hidden, 0, 0, 0));

    UiFlex contentF = col;
    contentF.grow = 1.0f;
    contentF.shrink = 1.0f;
    contentF.pad = 16.0f;
    contentF.gap = 12.0f;
    const uint16_t content = spawn(s, body, contentF, paint(UiRole::Panel, 0.086f, 0.090f, 0.106f));

    UiFlex pageF{};
    s.pageLabel = spawn(s, content, pageF, paint(UiRole::Label, 0.93f, 0.94f, 0.96f));
    addText(s, s.pageLabel, "SCENE");

    UiFlex pageCol = col;
    pageCol.grow = 1.0f;
    pageCol.shrink = 1.0f;
    pageCol.gap = 10.0f;

    s.pages[0] = spawn(s, content, pageCol, paint(UiRole::Panel, 0, 0, 0));
    auto* scenePaint = s.ent[s.pages[0]].try_get_mut<UiPaint>();
    scenePaint->a = 0.0f;

    UiFlex checkRow = row;
    checkRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    checkRow.height = 32.0f;
    checkRow.shrink = 0.0f;
    checkRow.align = 1;
    checkRow.gap = 10.0f;
    checkRow.pad = 6.0f;
    s.compactRow = spawn(s, s.pages[0], checkRow, paint(UiRole::Check, 0.16f, 0.17f, 0.21f, 6.0f));
    s.ent[s.compactRow].set<UiCheck>({0});
    uiBind(s, s.compactRow, onCompact, &s);
    UiFlex checkLabF{};
    checkLabF.marginL = 28.0f;
    const uint16_t checkLab = spawn(s, s.compactRow, checkLabF, paint(UiRole::Label, 0.86f, 0.88f, 0.92f));
    addText(s, checkLab, "COMPACT");

    UiFlex scrollF = col;
    scrollF.grow = 1.0f;
    scrollF.shrink = 1.0f;
    const uint16_t scroll = spawn(s, s.pages[0], scrollF, paint(UiRole::Scroll, 0.07f, 0.075f, 0.09f, 8.0f));

    UiFlex innerF = col;
    innerF.gap = 8.0f;
    innerF.pad = 8.0f;
    innerF.shrink = 0.0f;
    s.listInner = spawn(s, scroll, innerF, paint(UiRole::Panel, 0, 0, 0));
    s.ent[scroll].set<UiScroll>({0.0f, s.listInner});
    auto* innerPaint = s.ent[s.listInner].try_get_mut<UiPaint>();
    innerPaint->a = 0.0f;

    for (int i = 0; i < 8; ++i) {
        UiFlex itemF{};
        itemF.heightMode = static_cast<uint8_t>(UiSize::Px);
        itemF.height = 36.0f;
        itemF.shrink = 0.0f;
        itemF.align = 1;
        itemF.pad = 8.0f;
        const uint16_t item = spawn(s, s.listInner, itemF, paint(UiRole::Panel, 0.16f, 0.17f, 0.21f, 6.0f));
        const uint16_t itemLab = spawn(s, item, UiFlex{}, paint(UiRole::Label, 0.86f, 0.88f, 0.92f));
        char label[16] = {'I', 'T', 'E', 'M', ' ', '0', static_cast<char>('0' + i), '\0'};
        addText(s, itemLab, label);
    }

    s.pages[1] = spawn(s, content, pageCol, paint(UiRole::Panel, 0, 0, 0));
    auto* lookPaint = s.ent[s.pages[1]].try_get_mut<UiPaint>();
    lookPaint->a = 0.0f;

    UiFlex scaleRow = row;
    scaleRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    scaleRow.height = 28.0f;
    scaleRow.shrink = 0.0f;
    scaleRow.align = 1;
    scaleRow.gap = 10.0f;
    const uint16_t scaleRowId = spawn(s, s.pages[1], scaleRow, paint(UiRole::Hidden, 0, 0, 0));
    const uint16_t scaleLab = spawn(s, scaleRowId, UiFlex{}, paint(UiRole::Label, 0.75f, 0.78f, 0.84f));
    addText(s, scaleLab, "SCALE");
    UiFlex sliderF{};
    sliderF.grow = 1.0f;
    sliderF.heightMode = static_cast<uint8_t>(UiSize::Px);
    sliderF.height = 22.0f;
    sliderF.shrink = 1.0f;
    const uint16_t slider = spawn(s, scaleRowId, sliderF, paint(UiRole::Slider, 0.18f, 0.20f, 0.26f, 8.0f));
    s.ent[slider].set<UiRange>({1.0f, 0.5f, 2.0f});
    UiName scaleName{};
    copyText(scaleName.bytes, scaleName.len, "scale");
    s.ent[slider].set<UiName>(scaleName);
    s.ent[slider].set_name("scale");

    UiFlex previewF{};
    previewF.widthMode = static_cast<uint8_t>(UiSize::Percent);
    previewF.width = 100.0f;
    previewF.heightMode = static_cast<uint8_t>(UiSize::Px);
    previewF.height = 126.0f;
    previewF.shrink = 0.0f;
    s.preview = spawn(s, s.pages[1], previewF, paint(UiRole::Panel, 0.36f, 0.42f, 0.55f, 10.0f));
    UiName previewName{};
    copyText(previewName.bytes, previewName.len, "preview");
    s.ent[s.preview].set<UiName>(previewName);
    s.ent[s.preview].set_name("preview");

    UiFlex card{};
    card.widthMode = static_cast<uint8_t>(UiSize::Percent);
    card.width = 100.0f;
    card.heightMode = static_cast<uint8_t>(UiSize::Px);
    card.height = 78.0f;
    card.shrink = 0.0f;
    card.pad = 10.0f;
    UiPaint cardPaint = paint(UiRole::Panel, 0.16f, 0.18f, 0.24f, 8.0f);
    cardPaint.shadow = 14.0f;
    cardPaint.borderW = 1.5f;
    cardPaint.br = 0.45f;
    cardPaint.bg = 0.62f;
    cardPaint.bb = 0.95f;
    const uint16_t cardId = spawn(s, s.pages[1], card, cardPaint);
    UiFlex noteF{};
    noteF.widthMode = static_cast<uint8_t>(UiSize::Percent);
    noteF.width = 100.0f;
    noteF.heightMode = static_cast<uint8_t>(UiSize::Px);
    noteF.height = 52.0f;
    noteF.shrink = 1.0f;
    const uint16_t note = spawn(s, cardId, noteF, paint(UiRole::Label, 0.90f, 0.92f, 0.96f));
    addText(s, note, "Text wraps inside the parent, can underline, and clips to the box.");
    uiTextStyle(s, note, 0, 1, 0, 1, 1, 2.0f);
    if (auto* noteFlex = s.ent[note].try_get_mut<UiFlex>()) {
        noteFlex->widthMode = static_cast<uint8_t>(UiSize::Percent);
        noteFlex->width = 100.0f;
        noteFlex->heightMode = static_cast<uint8_t>(UiSize::Px);
        noteFlex->height = 52.0f;
        noteFlex->shrink = 1.0f;
    }

    UiFlex loadRow = row;
    loadRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    loadRow.height = 36.0f;
    loadRow.shrink = 0.0f;
    loadRow.align = 1;
    loadRow.gap = 10.0f;
    const uint16_t loadId = spawn(s, s.pages[1], loadRow, paint(UiRole::Hidden, 0, 0, 0));
    const uint16_t loadLab = spawn(s, loadId, UiFlex{}, paint(UiRole::Label, 0.75f, 0.78f, 0.84f));
    addText(s, loadLab, "LOAD");
    UiFlex loadBar{};
    loadBar.grow = 1.0f;
    loadBar.shrink = 1.0f;
    loadBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    loadBar.height = 12.0f;
    loadBar.minH = 12.0f;
    const uint16_t prog = spawn(s, loadId, loadBar, paint(UiRole::Progress, 0.14f, 0.15f, 0.19f, 6.0f));
    s.ent[prog].set<UiRange>({0.62f, 0.0f, 1.0f});
    (void)uiIcon(s, loadId, 1, 36.0f, 36.0f);

    s.pages[2] = spawn(s, content, pageCol, paint(UiRole::Panel, 0, 0, 0));
    auto* inputPaint = s.ent[s.pages[2]].try_get_mut<UiPaint>();
    inputPaint->a = 0.0f;
    UiFlex inputScrollF = col;
    inputScrollF.grow = 1.0f;
    inputScrollF.shrink = 1.0f;
    const uint16_t inputScroll = spawn(s, s.pages[2], inputScrollF, paint(UiRole::Scroll, 0.07f, 0.075f, 0.09f, 8.0f));
    UiFlex inputInner = col;
    inputInner.gap = 8.0f;
    inputInner.shrink = 0.0f;
    inputInner.pad = 4.0f;
    const uint16_t host = spawn(s, inputScroll, inputInner, paint(UiRole::Hidden, 0, 0, 0));
    s.ent[inputScroll].set<UiScroll>({0.0f, host});

    auto addLab = [&](const char* name) {
        const uint16_t lab = spawn(s, host, UiFlex{}, paint(UiRole::Label, 0.75f, 0.78f, 0.84f));
        addText(s, lab, name);
    };
    auto addField = [&](const char* value, uint8_t filter, uint8_t clear, bool disabled) {
        UiFlex fieldF{};
        fieldF.widthMode = static_cast<uint8_t>(UiSize::Percent);
        fieldF.width = 100.0f;
        fieldF.heightMode = static_cast<uint8_t>(UiSize::Px);
        fieldF.height = 36.0f;
        fieldF.shrink = 0.0f;
        UiPaint fill = paint(UiRole::Field, 0.14f, 0.15f, 0.19f, 6.0f);
        fill.borderW = 1.0f;
        fill.br = 0.32f;
        fill.bg = 0.38f;
        fill.bb = 0.52f;
        fill.hr = 0.20f;
        fill.hg = 0.24f;
        fill.hb = 0.34f;
        fill.disabled = disabled ? 1 : 0;
        const uint16_t id = spawn(s, host, fieldF, fill);
        UiField initial{};
        uint8_t n = 0;
        if (value != nullptr) {
            while (value[n] != '\0' && n < 95) {
                initial.bytes[n] = value[n];
                ++n;
            }
        }
        initial.bytes[n] = '\0';
        initial.len = n;
        initial.caret = n;
        initial.anchor = n;
        initial.filter = filter;
        initial.clear = clear;
        initial.padL = 12.0f;
        initial.padR = 12.0f;
        s.ent[id].set<UiField>(initial);
        return id;
    };
    auto tune = [&](uint16_t id, const char* hint, uint8_t maxChars, uint8_t required, uint8_t readOnly, uint8_t password) {
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr) {
            return;
        }
        field->maxChars = maxChars;
        field->required = required;
        field->readOnly = readOnly;
        field->password = password;
        field->hintLen = 0;
        if (hint != nullptr) {
            while (hint[field->hintLen] != '\0' && field->hintLen < 39) {
                field->hint[field->hintLen] = hint[field->hintLen];
                ++field->hintLen;
            }
            field->hint[field->hintLen] = '\0';
        }
    };
    auto formOf = [&](uint16_t id) {
        if (auto* field = s.ent[id].try_get_mut<UiField>()) {
            field->inForm = 1;
        }
        fieldMark(s, id);
    };
    addLab("NAME  required");
    s.field = addField("", 0, 1, false);
    tune(s.field, "Name", 24, 1, 0, 0);
    formOf(s.field);
    addLab("EMAIL  name@host");
    const uint16_t email = addField("bad", 3, 1, false);
    tune(email, "name@host", 0, 1, 0, 0);
    formOf(email);
    addLab("TEL  6 digits");
    const uint16_t tel = addField("12", 4, 1, false);
    tune(tel, "+7", 16, 1, 0, 0);
    formOf(tel);
    addLab("URL  needs a dot");
    const uint16_t url = addField("nope", 5, 1, false);
    tune(url, "https://", 0, 1, 0, 0);
    formOf(url);
    addLab("READONLY");
    const uint16_t locked = addField("cannot edit", 0, 0, false);
    tune(locked, "", 0, 0, 1, 0);
    if (auto* paint = s.ent[locked].try_get_mut<UiPaint>()) {
        paint->r = 0.09f;
        paint->g = 0.09f;
        paint->b = 0.11f;
    }
    addLab("PAD / COLOR");
    const uint16_t padded = addField("inside", 0, 0, false);
    if (auto* field = s.ent[padded].try_get_mut<UiField>()) {
        field->padL = 22.0f;
        field->padR = 22.0f;
        field->text[0] = 0.55f;
        field->text[1] = 0.90f;
        field->text[2] = 0.72f;
    }
    if (auto* paint = s.ent[padded].try_get_mut<UiPaint>()) {
        paint->r = 0.08f;
        paint->g = 0.16f;
        paint->b = 0.13f;
    }
    addLab("PASSWORD");
    tune(addField("secret", 0, 1, false), "", 0, 0, 0, 1);
    addLab("FILE");
    s.fileTarget = addField("", 0, 0, false);
    tune(s.fileTarget, "choose a file", 0, 0, 1, 0);
    formOf(s.fileTarget);
    addLab("DATE");
    s.calTarget = addField("", 0, 0, false);
    tune(s.calTarget, "YYYY-MM-DD", 0, 1, 1, 0);
    formOf(s.calTarget);
    addLab("COLOR");
    s.colorTarget = addField("", 0, 0, false);
    tune(s.colorTarget, "#RRGGBB", 0, 0, 1, 0);
    formOf(s.colorTarget);
    UiFlex tools = row;
    tools.gap = 8.0f;
    tools.shrink = 0.0f;
    tools.heightMode = static_cast<uint8_t>(UiSize::Px);
    tools.height = 36.0f;
    const uint16_t toolRow = spawn(s, host, tools, paint(UiRole::Hidden, 0, 0, 0));
    auto toolBtn = [&](const char* name, int8_t tag, float r, float g, float b, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 32.0f;
        flex.shrink = 0.0f;
        UiPaint btn = paint(UiRole::Button, r, g, b, 6.0f, tag);
        const uint16_t id = spawn(s, toolRow, flex, btn);
        UiText label{};
        copyText(label.bytes, label.len, name);
        s.ent[id].set<UiText>(label);
        auto* box = s.ent[id].try_get_mut<UiFlex>();
        if (box != nullptr) {
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = static_cast<float>(label.len) * s.look.advance + 28.0f;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
            box->height = 32.0f;
        }
        uiBind(s, id, fn, &s);
    };
    toolBtn("FILE", -59, 0.20f, 0.28f, 0.40f, onFile);
    toolBtn("DATE", -60, 0.20f, 0.32f, 0.28f, calOnOpen);
    toolBtn("COLOR", -61, 0.36f, 0.28f, 0.42f, colorOnOpen);
    toolBtn("SEND", -36, 0.45f, 0.32f, 0.22f, onSend);
    s.formNote = spawn(s, host, UiFlex{}, paint(UiRole::Label, 0.95f, 0.72f, 0.45f));
    addText(s, s.formNote, "form idle");
    addLab("IMAGE");
    UiFlex view{};
    view.widthMode = static_cast<uint8_t>(UiSize::Percent);
    view.width = 100.0f;
    view.heightMode = static_cast<uint8_t>(UiSize::Px);
    view.height = 220.0f;
    view.shrink = 0.0f;
    s.photoView = spawn(s, host, view, paint(UiRole::Panel, 0.05f, 0.05f, 0.06f, 8.0f, -90));
    UiImage photo{};
    photo.slot = 0;
    photo.frames = 1;
    photo.minimap = 1;
    photo.viewW = 1.0f;
    photo.viewH = 1.0f;
    s.ent[s.photoView].set<UiImage>(photo);
    UiFlex opt{};
    opt.widthMode = static_cast<uint8_t>(UiSize::Percent);
    opt.width = 100.0f;
    opt.heightMode = static_cast<uint8_t>(UiSize::Px);
    opt.height = 32.0f;
    opt.shrink = 0.0f;
    opt.align = 1;
    const uint16_t box = spawn(s, host, opt, paint(UiRole::Check, 0.16f, 0.17f, 0.21f, 6.0f));
    s.ent[box].set<UiCheck>({0, 0, 0});
    addText(s, box, "CHECK");
    if (auto* flex = s.ent[box].try_get_mut<UiFlex>()) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex->width = 100.0f;
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = 32.0f;
    }
    const uint16_t radioA = spawn(s, host, opt, paint(UiRole::Check, 0.16f, 0.17f, 0.21f, 6.0f));
    const uint16_t radioB = spawn(s, host, opt, paint(UiRole::Check, 0.16f, 0.17f, 0.21f, 6.0f));
    s.ent[radioA].set<UiCheck>({1, 1, 1});
    s.ent[radioB].set<UiCheck>({0, 1, 1});
    addText(s, radioA, "RADIO A");
    addText(s, radioB, "RADIO B");
    for (uint16_t id : {radioA, radioB}) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = static_cast<uint8_t>(UiSize::Percent);
            flex->width = 100.0f;
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->height = 32.0f;
        }
    }
    (void)uiImportJsonFile(s, host, "assets/ui/sample.json");
    (void)jsonLoadFile(s, "assets/ui/sample.json");

    UiFlex fieldMenuF{};
    fieldMenuF.position = 1;
    fieldMenuF.widthMode = static_cast<uint8_t>(UiSize::Px);
    fieldMenuF.width = 220.0f;
    fieldMenuF.pad = 6.0f;
    fieldMenuF.gap = 4.0f;
    fieldMenuF.shrink = 0.0f;
    s.fieldMenu = spawn(s, root, fieldMenuF, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f));
    if (s.yoga[s.fieldMenu] != nullptr) {
        YGNodeStyleSetDisplay(s.yoga[s.fieldMenu], YGDisplayNone);
    }
    uiCommandAddTo(s, s.fieldMenu, "CUT", cmdFieldCut, &s);
    uiCommandAddTo(s, s.fieldMenu, "COPY", cmdFieldCopy, &s);
    uiCommandAddTo(s, s.fieldMenu, "PASTE", cmdFieldPaste, &s);
    uiCommandAddTo(s, s.fieldMenu, "SELECT ALL", cmdFieldAll, &s);
    uiCommandAddTo(s, s.fieldMenu, "CLEAR", cmdFieldWipe, &s);
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.ent[id].try_get<UiField>() != nullptr) {
            s.contextOf[id] = s.fieldMenu;
        }
    }

    setPage(s, 0);

    UiFlex statusF = row;
    statusF.heightMode = static_cast<uint8_t>(UiSize::Px);
    statusF.height = 26.0f;
    statusF.shrink = 0.0f;
    statusF.align = 1;
    statusF.pad = 10.0f;
    const uint16_t status = spawn(s, root, statusF, paint(UiRole::Panel, 0.078f, 0.082f, 0.098f));
    s.statusLabel = spawn(s, status, UiFlex{}, paint(UiRole::Label, 0.62f, 0.66f, 0.74f));
    addText(s, s.statusLabel, "SCALE 1.00");
    UiName scaleText{};
    copyText(scaleText.bytes, scaleText.len, "scale_text");
    s.ent[s.statusLabel].set<UiName>(scaleText);
    s.ent[s.statusLabel].set_name("scale_text");

    UiFlex menuF = col;
    menuF.position = 1;
    menuF.widthMode = static_cast<uint8_t>(UiSize::Px);
    menuF.width = s.menuW;
    menuF.pad = s.menuPad;
    menuF.gap = s.menuGap;
    menuF.shrink = 0.0f;
    s.menu = spawn(s, root, menuF, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f));
    YGNodeStyleSetDisplay(s.yoga[s.menu], YGDisplayNone);

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
    auto mini = [&](uint16_t parent, const char* name, int8_t tag, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 28.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, parent, flex, paint(UiRole::Button, 0.20f, 0.24f, 0.32f, 6.0f, tag));
        UiText label{};
        copyText(label.bytes, label.len, name);
        s.ent[id].set<UiText>(label);
        auto* box = s.ent[id].try_get_mut<UiFlex>();
        if (box != nullptr) {
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = static_cast<float>(label.len) * s.look.advance + 24.0f;
            box->height = 28.0f;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
        }
        uiBind(s, id, fn, &s);
        return id;
    };
    s.calPanel = floatPanel(440.0f, 80.0f, 300.0f, 420.0f);
    UiFlex calBar = row;
    calBar.align = 1;
    calBar.shrink = 0.0f;
    calBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    calBar.height = 32.0f;
    calBar.gap = 8.0f;
    const uint16_t calTools = spawn(s, s.calPanel, calBar, paint(UiRole::Hidden, 0, 0, 0));
    mini(calTools, "<", -54, calOnPrev);
    s.calHead = spawn(s, calTools, UiFlex{}, paint(UiRole::Label, 0.9f, 0.92f, 0.96f));
    addText(s, s.calHead, "OCT 2026");
    mini(calTools, ">", -55, calOnNext);
    mini(calTools, "X", -57, calOnClose);
    UiFlex todayRow = row;
    todayRow.shrink = 0.0f;
    todayRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    todayRow.height = 32.0f;
    const uint16_t todayHost = spawn(s, s.calPanel, todayRow, paint(UiRole::Hidden, 0, 0, 0));
    mini(todayHost, "TODAY", -87, calOnToday);
    UiFlex weekHead = row;
    weekHead.gap = 4.0f;
    weekHead.shrink = 0.0f;
    weekHead.heightMode = static_cast<uint8_t>(UiSize::Px);
    weekHead.height = 20.0f;
    const uint16_t weekHeadId = spawn(s, s.calPanel, weekHead, paint(UiRole::Hidden, 0, 0, 0));
    const char* weekNames[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int day = 0; day < 7; ++day) {
        UiFlex cell{};
        cell.widthMode = static_cast<uint8_t>(UiSize::Px);
        cell.heightMode = static_cast<uint8_t>(UiSize::Px);
        cell.width = 32.0f;
        cell.height = 18.0f;
        cell.shrink = 0.0f;
        const uint16_t id = spawn(s, weekHeadId, cell, paint(UiRole::Label, 0.55f, 0.58f, 0.64f));
        addText(s, id, weekNames[day]);
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->width = 32.0f;
            flex->height = 18.0f;
        }
    }
    for (int week = 0; week < 6; ++week) {
        UiFlex weekRow = row;
        weekRow.gap = 4.0f;
        weekRow.shrink = 0.0f;
        weekRow.heightMode = static_cast<uint8_t>(UiSize::Px);
        weekRow.height = 32.0f;
        const uint16_t weekId = spawn(s, s.calPanel, weekRow, paint(UiRole::Hidden, 0, 0, 0));
        for (int day = 0; day < 7; ++day) {
            UiFlex cell{};
            cell.widthMode = static_cast<uint8_t>(UiSize::Px);
            cell.heightMode = static_cast<uint8_t>(UiSize::Px);
            cell.width = 32.0f;
            cell.height = 28.0f;
            cell.shrink = 0.0f;
            const uint16_t id = spawn(s, weekId, cell, paint(UiRole::Button, 0.16f, 0.18f, 0.24f, 4.0f, -56));
            uiBind(s, id, calOnDay, &s);
            s.calDay[week * 7 + day] = id;
            UiText label{};
            s.ent[id].set<UiText>(label);
        }
    }
    calFill(s);
    uiShow(s, s.calPanel, false);

    s.fileFrame = floatPanel(70.0f, 64.0f, 1100.0f, 700.0f);
    if (auto* framePaint = s.ent[s.fileFrame].try_get_mut<UiPaint>()) {
        framePaint->tag = -15;
    }
    UiFlex fileBar = row;
    fileBar.align = 0;
    fileBar.shrink = 0.0f;
    fileBar.widthMode = static_cast<uint8_t>(UiSize::Percent);
    fileBar.width = 100.0f;
    fileBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    fileBar.height = 32.0f;
    fileBar.gap = 8.0f;
    fileBar.pad = 8.0f;
    const uint16_t fileTools = spawn(s, s.fileFrame, fileBar, paint(UiRole::Button, 0.13f, 0.14f, 0.17f, 0.0f, -6));
    const uint16_t fileTitle = spawn(s, fileTools, UiFlex{}, paint(UiRole::Label, 0.9f, 0.92f, 0.96f));
    addText(s, fileTitle, "FILES");
    UiFlex fileSpacer{};
    fileSpacer.grow = 1.0f;
    fileSpacer.shrink = 1.0f;
    spawn(s, fileTools, fileSpacer, paint(UiRole::Hidden, 0, 0, 0));
    mini(fileTools, "X", -93, nullptr);

    if (s.book == nullptr || s.book->gen == 0) {
        rgbToHsv(s.colorR, s.colorG, s.colorB, s.colorH, s.colorS, s.colorV);
        s.colorA = 1.0f;
        s.colorUseAlpha = 1;
    } else {
        takeBook(s);
    }
    s.colorPanel = floatPanel(360.0f, 70.0f, 500.0f, 720.0f);
    fillColorBody(s, s.colorPanel);
    uiShow(s, s.colorPanel, false);

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
