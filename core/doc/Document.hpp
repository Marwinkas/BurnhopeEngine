#pragma once

#include "doc/UndoStack.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kDocCap = 2048;

struct Document {
    char text[kDocCap]{};
    uint32_t length = 0;
    uint32_t caret = 0;
    UndoStack history{};
};

static_assert(std::is_trivially_copyable_v<Document>);

inline bool docSplice(Document& doc, uint32_t pos, uint32_t cut, const char* add, uint32_t addLen) {
    if (pos > doc.length || cut > doc.length - pos) {
        return false;
    }
    if (doc.length - cut + addLen > kDocCap) {
        return false;
    }
    std::memmove(doc.text + pos + addLen, doc.text + pos + cut, doc.length - (pos + cut));
    if (addLen > 0 && add != nullptr) {
        std::memcpy(doc.text + pos, add, addLen);
    }
    doc.length = doc.length - cut + addLen;
    doc.caret = pos + addLen;
    if (doc.length < kDocCap) {
        doc.text[doc.length] = '\0';
    }
    return true;
}

inline void docHistoryClear(Document& doc) {
    doc.history.poolUsed = 0;
    doc.history.count = 0;
    doc.history.at = 0;
}

inline bool docRemember(Document& doc, uint8_t kind, uint32_t pos, const char* bytes, uint32_t length) {
    UndoStack& stack = doc.history;
    stack.count = stack.at;
    if (stack.at == kUndoCap || stack.poolUsed + length > kUndoPool) {
        docHistoryClear(doc);
    }
    UndoRecord& rec = stack.records[stack.at];
    rec.pos = pos;
    rec.length = length;
    rec.pool = stack.poolUsed;
    rec.kind = kind;
    if (length > 0 && bytes != nullptr) {
        std::memcpy(stack.pool + stack.poolUsed, bytes, length);
    }
    stack.poolUsed += length;
    stack.at += 1;
    stack.count = stack.at;
    return true;
}

inline bool docInsert(Document& doc, uint32_t pos, const char* bytes, uint32_t length) {
    if (!docSplice(doc, pos, 0, bytes, length)) {
        return false;
    }
    return docRemember(doc, kUndoInsert, pos, bytes, length);
}

inline bool docDelete(Document& doc, uint32_t pos, uint32_t length) {
    if (pos > doc.length || length > doc.length - pos) {
        return false;
    }
    char saved[kUndoPool];
    if (length > kUndoPool) {
        return false;
    }
    if (length > 0) {
        std::memcpy(saved, doc.text + pos, length);
    }
    if (!docSplice(doc, pos, length, nullptr, 0)) {
        return false;
    }
    return docRemember(doc, kUndoDelete, pos, saved, length);
}

inline bool docApply(Document& doc, const UndoRecord& rec, bool undo) {
    const char* bytes = doc.history.pool + rec.pool;
    if (rec.kind == kUndoInsert) {
        return undo ? docSplice(doc, rec.pos, rec.length, nullptr, 0) : docSplice(doc, rec.pos, 0, bytes, rec.length);
    }
    return undo ? docSplice(doc, rec.pos, 0, bytes, rec.length) : docSplice(doc, rec.pos, rec.length, nullptr, 0);
}

inline bool docUndo(Document& doc) {
    if (doc.history.at == 0) {
        return false;
    }
    doc.history.at -= 1;
    return docApply(doc, doc.history.records[doc.history.at], true);
}

inline bool docRedo(Document& doc) {
    if (doc.history.at >= doc.history.count) {
        return false;
    }
    const bool ok = docApply(doc, doc.history.records[doc.history.at], false);
    if (ok) {
        doc.history.at += 1;
    }
    return ok;
}

} // namespace burnhope
