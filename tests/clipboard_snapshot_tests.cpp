#include "clipboard_snapshot.h"

#include <windows.h>

#include <string>

namespace {

ClipboardSnapshot makeSnapshot() {
    ClipboardSnapshot snapshot;
    snapshot.formats = {
        {0, "HTML Format", "Version:1.0\r\n<html><body>table</body></html>"},
        {0, "Rich Text Format", "{\\rtf1 table}"},
        {CF_UNICODETEXT, {}, "table\0"},
        {0, "Spreadsheet Private Format", std::string("\0\xFF\x01\x00", 4)},
    };
    return snapshot;
}

bool sameFormat(const ClipboardSnapshotFormat& first, const ClipboardSnapshotFormat& second) {
    return first.standardFormat == second.standardFormat && first.name == second.name &&
        first.data == second.data;
}

bool sameSnapshot(const ClipboardSnapshot& first, const ClipboardSnapshot& second) {
    if (first.formats.size() != second.formats.size()) return false;
    for (std::size_t index = 0; index < first.formats.size(); ++index) {
        if (!sameFormat(first.formats[index], second.formats[index])) return false;
    }
    return true;
}

} // namespace

int main() {
    if (!isClipboardHtmlHeaderText(
            "Version:1.0\r\nStartHTML:0000000128\r\nEndHTML:0000001733\r\n"
            "StartFragment:0000000160\r\nEndFragment:0000001701\r\n")) return 11;
    if (isClipboardHtmlHeaderText("Version: 1.0 is a plain text version string")) return 12;
    if (isClipboardHtmlHeaderText("StartHTML: is ordinary text without a CF_HTML header")) return 13;

    ClipboardSnapshot original = makeSnapshot();
    std::string envelope;
    if (!makeClipboardSnapshotEnvelope(original, envelope)) return 1;

    ClipboardSnapshot restored;
    if (!parseClipboardSnapshotEnvelope(envelope, restored) || !sameSnapshot(original, restored)) return 2;
    if (clipboardSnapshotFingerprint(original) != clipboardSnapshotFingerprint(restored)) return 3;

    std::string html;
    if (!buildClipboardSnapshotHtml(original, "table", html)) return 4;
    ClipboardSnapshot restoredFromHtml;
    std::string displayText;
    if (!parseClipboardSnapshotHtml(html, restoredFromHtml, &displayText) ||
        !sameSnapshot(original, restoredFromHtml) || displayText != "table") return 5;

    ClipboardSnapshot different = original;
    different.formats.back().data.push_back('x');
    if (clipboardSnapshotFingerprint(original) == clipboardSnapshotFingerprint(different)) return 6;

    if (!findClipboardSnapshotFormat(restored, "spreadsheet private format")) return 7;

    const std::string truncated = envelope.substr(0, envelope.size() - 1);
    if (parseClipboardSnapshotEnvelope(truncated, restored)) return 8;

    std::string malformedHtml = html;
    const std::size_t marker = malformedHtml.find("<!--ClipLite-CB1:");
    if (marker == std::string::npos) return 9;
    malformedHtml.replace(marker, sizeof("<!--ClipLite-CB1:") - 1, "<!--NotClipLite:");
    if (parseClipboardSnapshotHtml(malformedHtml, restored)) return 10;

    return 0;
}
