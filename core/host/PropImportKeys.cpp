#include "host/PropImportSetters.hpp"
#include "Str.hpp"
#include "ui/Icon.hpp"
#include "ui/State.hpp"

#include <cstring>
#include <string_view>

namespace burnhope::propimport {
namespace {

bool setClass(Cur& c, Import&, Node& n) {
    json::skip(c);
    n.clsAt = c.p;
    uint32_t len = 0;
    return json::parseStringCap(c, n.cls, sizeof(n.cls), len);
}

bool setUse(Cur& c, Import&, Node& n) {
    json::skip(c);
    n.useAt = c.p;
    uint32_t len = 0;
    return json::parseStringCap(c, n.use, sizeof(n.use), len);
}

bool setPaint(Cur& c, Import&, Node& n) {
    char text[16]{};
    uint32_t len = 0;
    if (!json::parseStringCap(c, text, sizeof(text), len)) {
        return false;
    }
    for (const char* name : {"hidden", "dock", "content", "sep", "line", "glyph", "accent", "veil"}) {
        if (std::strcmp(text, name) == 0) {
            strCopyCap(n.paintName, sizeof(n.paintName), text);
            return true;
        }
    }
    return false;
}

bool setDir(Cur& c, Import&, Node& n) {
    const int w = word(c, {"row", "column", "col"});
    n.row = w == 0;
    return w >= 0;
}

bool setAlign(Cur& c, Import&, Node& n) {
    const int w = word(c, {"center", "start"});
    n.alignCenter = w == 0;
    return w >= 0;
}

bool setSelf(Cur& c, Import&, Node& n) {
    const int w = word(c, {"center", "start"});
    n.selfCenter = w == 0;
    return w >= 0;
}

// "icon": "Name" (UiIcon) or a number.
bool setIcon(Cur& c, Import&, Node& n) {
    json::skip(c);
    if (c.p < c.end && *c.p == '"') {
        char text[24]{};
        uint32_t len = 0;
        if (!json::parseStringCap(c, text, sizeof(text), len)) {
            return false;
        }
        n.icon = static_cast<uint8_t>(uiIconFromName(text));
    } else {
        double v = 0.0;
        if (!json::parseNumber(c, v)) {
            return false;
        }
        n.icon = static_cast<uint8_t>(v);
    }
    n.hasIcon = true;
    return true;
}

// "photo": an asset path resolved to a photo slot, or a slot number.
bool setPhoto(Cur& c, Import& in, Node& n) {
    json::skip(c);
    if (c.p < c.end && *c.p == '"') {
        char path[kUiIconPathCap]{};
        uint32_t len = 0;
        if (!json::parseStringCap(c, path, sizeof(path), len)) {
            return false;
        }
        n.photo = in.state->iconResolve != nullptr ? in.state->iconResolve(*in.state, path, in.state->iconUser) : kPhotoNone;
    } else {
        double v = 0.0;
        if (!json::parseNumber(c, v)) {
            return false;
        }
        n.photo = static_cast<uint8_t>(v);
    }
    n.hasPhoto = n.photo < kPhotoSlots;
    return true;
}

bool setItems(Cur& c, Import& in, Node& n) { return parseItems(in, c, n); }
bool setShow(Cur& c, Import& in, Node& n) { return parseShow(in, c, n); }

struct KeyDef {
    std::string_view name;
    Setter set;
};

constexpr KeyDef kKeys[] = {
    {"role", setStr<&Node::role>}, {"label", setStr<&Node::label>}, {"name", setStr<&Node::name>},
    {"caption", setStr<&Node::caption>}, {"command", setStr<&Node::command>}, {"glyph", setStr<&Node::glyph>},
    {"class", setClass}, {"use", setUse}, {"paint", setPaint}, {"dir", setDir}, {"align", setAlign}, {"self", setSelf},
    {"icon", setIcon}, {"photo", setPhoto}, {"items", setItems}, {"showWhen", setShow},
    {"bind", setIntHas<&Node::bind, &Node::hasBind>}, {"cat", setInt<&Node::cat>}, {"owner", setInt<&Node::owner>},
    {"kind", setInt<&Node::kind>}, {"group", setInt<&Node::groupN>}, {"slot", setInt<&Node::slotN>},
    {"channel", setInt<&Node::channel>}, {"cell", setInt<&Node::cell>},
    {"value", setF<&Node::value>}, {"value2", setF<&Node::value2>}, {"value3", setF<&Node::value3>},
    {"min", setF<&Node::minV>}, {"max", setF<&Node::maxV>}, {"step", setFHas<&Node::step, &Node::hasStep>},
    {"g", setF<&Node::g>}, {"b", setF<&Node::b>},
    {"w", setF<&Node::w>}, {"h", setF<&Node::h>}, {"wp", setF<&Node::wp>}, {"hp", setF<&Node::hp>},
    {"grow", setF<&Node::grow>}, {"shrink", setFHas<&Node::shrink, &Node::hasShrink>},
    {"gap", setF<&Node::gap>}, {"pad", setF<&Node::pad>}, {"padL", setF<&Node::padL>}, {"padR", setF<&Node::padR>},
    {"padT", setF<&Node::padT>}, {"padB", setF<&Node::padB>}, {"minW", setF<&Node::minW>}, {"minH", setF<&Node::minH>},
    {"posX", setF<&Node::posX>}, {"posY", setF<&Node::posY>}, {"inset", setF<&Node::inset>},
    {"bar", setBool<&Node::bar>}, {"slide", setBool<&Node::slide>}, {"hide", setBool<&Node::hide>},
    {"center", setBool<&Node::center>}, {"ghost", setBool<&Node::ghost>}, {"pass", setBool<&Node::pass>},
    {"fill", setBool<&Node::fill>}, {"wrap", setBool<&Node::wrap>}, {"proportional", setBool<&Node::proportional>},
    {"absolute", setBool<&Node::absolute>}, {"vertical", setBool<&Node::vertical>}, {"iconbtn", setBool<&Node::iconBtn>},
    {"on", setBool<&Node::on>}, {"reset", setBool<&Node::reset>},
};

} // namespace

bool parseNodeKeys(Cur& c, Import& in, Node& node, const char*& children) {
    children = nullptr;
    if (!json::eat(c, '{')) {
        return fail(in, c.p, "expected a node object");
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        json::skip(c);
        const char* keyAt = c.p;
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return fail(in, keyAt, "expected a key of at most 15 characters");
        }
        json::skip(c);
        const char* valueAt = c.p;
        if (std::strcmp(key, "children") == 0) {
            if (c.p >= c.end || *c.p != '[' || !json::skipValue(c)) {
                return fail(in, valueAt, "children must be an array");
            }
            children = valueAt;
        } else {
            const KeyDef* def = nullptr;
            for (const KeyDef& entry : kKeys) {
                if (entry.name == key) {
                    def = &entry;
                    break;
                }
            }
            if (def == nullptr) {
                return fail(in, keyAt, "unknown key", key);
            }
            if (!def->set(c, in, node)) {
                return fail(in, valueAt, "bad value for key", key);
            }
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            break;
        }
        if (!json::eat(c, ',')) {
            return fail(in, c.p, "expected , or } after the value of", key);
        }
    }
    return node.cls[0] == '\0' || applyClass(in, node.clsAt, node, node.cls);
}

} // namespace burnhope::propimport
