#include "clip_store.h"

#include <windows.h>

#include <cstdio>

int main() {
    ClipStore store(10);
    if (!store.open()) return 1;
    store.clear();
    const std::string text = "ClipLite store test with a long searchable suffix";
    if (!store.append(ClipType::Text, text, clipLiteHash(text))) return 2;
    if (store.activeCount() != 1) return 3;
    std::string restored;
    if (!store.readPayload(0, restored) || restored != text) return 4;
    if (store.search("searchable suffix").size() != 1) return 5;
    if (!store.togglePinned(0) || !store.items()[0].pinned) return 6;
    if (!store.setCategory(0, 2) || store.items()[0].category != 2) return 7;
    if (!store.remove(0) || store.activeCount() != 0) return 8;
    if (!store.append(ClipType::Text, "older", clipLiteHash("older"))) return 9;
    if (!store.append(ClipType::Text, "newer", clipLiteHash("newer"))) return 10;
    ClipStore reopened(10);
    if (!reopened.open() || reopened.activeCount() != 2 || reopened.items()[0].preview != "newer") return 11;
    reopened.clear();
    store.clear();
    return 0;
}
