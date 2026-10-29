#include "clipboard_snapshot.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kSnapshotMagic = 0x3153504C; // CLP1
constexpr std::uint16_t kSnapshotVersion = 1;
constexpr std::size_t kMaxSnapshotFormats = 12;
constexpr std::size_t kMaxFormatNameBytes = 512;
constexpr std::size_t kMaxFormatDataBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxSnapshotDataBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxDisplayTextBytes = 64u * 1024u;
constexpr std::size_t kMaxCfHtmlBytes = 16u * 1024u * 1024u;
inline constexpr char kSnapshotHtmlMarker[] = "<!--ClipLite-CB1:";

#pragma pack(push, 1)
struct SnapshotHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t headerSize;
    std::uint32_t formatCount;
    std::uint32_t totalSize;
};

struct SnapshotEntry {
    std::uint16_t standardFormat;
    std::uint16_t reserved;
    std::uint32_t nameSize;
    std::uint32_t dataSize;
};
#pragma pack(pop)

struct ResolvedFormat {
    UINT format = 0;
    std::string data;
    bool textLike = false;
};

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return value;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0,
                                            nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), required,
                            nullptr, nullptr) != required) return {};
    return result;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), required) != required) {
        return {};
    }
    return result;
}

bool isExcludedRegisteredFormat(const std::string& name) {
    const std::string lower = lowerAscii(name);
    static constexpr const char* kExcluded[] = {
        "clipboard viewer ignore", "excludeclipboardcontentfrommonitorprocessing",
        "canincludeinclipboardhistory", "canuploadtocloudclipboard",
        "embed source", "object descriptor", "link source", "link source descriptor",
        "ole private data", "ownerlink", "objectlink", "dataobject",
        "shell idlist array", "shell object offsets", "filedescriptor", "filecontents",
        "preferred dropeffect",
    };
    for (const char* excluded : kExcluded) {
        if (lower == excluded) return true;
    }
    return false;
}

int formatPriority(const ClipboardSnapshotFormat& format) {
    const std::string lower = lowerAscii(format.name);
    if (lower == "biff12") return 0;
    if (lower == "biff8") return 1;
    if (lower == "biff5") return 2;
    if (lower == "xml spreadsheet") return 3;
    if (lower == "html format") return 4;
    if (lower == "rich text format") return 5;
    if (lower == "mathml" || lower == "mathml presentation") return 6;
    if (format.standardFormat == CF_DIB || format.standardFormat == CF_DIBV5) return 7;
    if (format.standardFormat == CF_UNICODETEXT) return 8;
    if (format.standardFormat == CF_TEXT || format.standardFormat == CF_OEMTEXT) return 9;
    return 10;
}

std::string formatKey(const ClipboardSnapshotFormat& format) {
    if (format.standardFormat != 0) {
        return "#" + std::to_string(format.standardFormat);
    }
    return "@" + lowerAscii(format.name);
}

bool formatLess(const ClipboardSnapshotFormat& first, const ClipboardSnapshotFormat& second) {
    const int firstPriority = formatPriority(first);
    const int secondPriority = formatPriority(second);
    if (firstPriority != secondPriority) return firstPriority < secondPriority;
    const std::string firstKey = formatKey(first);
    const std::string secondKey = formatKey(second);
    if (firstKey != secondKey) return firstKey < secondKey;
    return first.data.size() < second.data.size();
}

void sortSnapshot(ClipboardSnapshot& snapshot) {
    std::sort(snapshot.formats.begin(), snapshot.formats.end(), formatLess);
}

bool sameFormat(const ClipboardSnapshotFormat& first, const ClipboardSnapshotFormat& second) {
    if (first.standardFormat != 0 || second.standardFormat != 0) {
        return first.standardFormat == second.standardFormat;
    }
    return lowerAscii(first.name) == lowerAscii(second.name);
}

bool appendUniqueFormat(ClipboardSnapshot& snapshot, ClipboardSnapshotFormat format,
                        std::size_t& totalBytes) {
    if (format.standardFormat == 0 && format.name.empty()) return false;
    if (format.standardFormat == 0 && format.name.size() > kMaxFormatNameBytes) return false;
    if (format.data.empty() || format.data.size() > kMaxFormatDataBytes ||
        totalBytes > kMaxSnapshotDataBytes - format.data.size()) return false;
    for (const ClipboardSnapshotFormat& existing : snapshot.formats) {
        if (sameFormat(existing, format)) return false;
    }
    if (snapshot.formats.size() >= kMaxSnapshotFormats) return false;
    totalBytes += format.data.size();
    snapshot.formats.push_back(std::move(format));
    return true;
}

bool readGlobalData(UINT format, std::string& data) {
    data.clear();
    HANDLE handle = GetClipboardData(format);
    if (!handle) return false;
    const SIZE_T size = GlobalSize(handle);
    if (size == 0 || size > kMaxFormatDataBytes) return false;
    const void* source = GlobalLock(handle);
    if (!source) return false;
    data.assign(static_cast<const char*>(source), static_cast<std::size_t>(size));
    GlobalUnlock(handle);
    return !data.empty();
}

bool readDisplayText(std::string& displayText) {
    displayText.clear();
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const SIZE_T size = GlobalSize(handle);
        if (size >= sizeof(wchar_t) && size <= 1024u * 1024u) {
            const wchar_t* source = static_cast<const wchar_t*>(GlobalLock(handle));
            if (source) {
                const std::size_t maxChars = static_cast<std::size_t>(size / sizeof(wchar_t));
                std::size_t length = 0;
                while (length < maxChars && source[length] != L'\0') ++length;
                displayText = wideToUtf8(std::wstring(source, length));
                GlobalUnlock(handle);
                if (!displayText.empty() && displayText.size() <= kMaxDisplayTextBytes) return true;
                displayText.clear();
            }
        }
    }
    std::string ansi;
    if (!readGlobalData(CF_TEXT, ansi)) return false;
    const std::size_t terminator = ansi.find('\0');
    if (terminator != std::string::npos) ansi.resize(terminator);
    if (ansi.empty()) return false;
    const int required = MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), nullptr, 0);
    if (required <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()),
                            wide.data(), required) != required) return false;
    displayText = wideToUtf8(wide);
    return !displayText.empty() && displayText.size() <= kMaxDisplayTextBytes;
}

bool getClipboardFormatName(UINT format, std::string& name) {
    name.clear();
    wchar_t buffer[512]{};
    const int length = GetClipboardFormatNameW(format, buffer, ARRAYSIZE(buffer));
    if (length <= 0) return false;
    name = wideToUtf8(std::wstring(buffer, static_cast<std::size_t>(length)));
    return !name.empty() && name.size() <= kMaxFormatNameBytes;
}

bool isSupportedStandardFormat(UINT format) {
    return format == CF_UNICODETEXT || format == CF_TEXT || format == CF_OEMTEXT ||
        format == CF_DIB || format == CF_DIBV5;
}

bool setCfHtmlOffset(std::string& header, const char* label, std::size_t offset) {
    if (offset > 9999999999ULL) return false;
    char value[11]{};
    const int written = std::snprintf(value, sizeof(value), "%010llu",
                                      static_cast<unsigned long long>(offset));
    if (written != 10) return false;
    const std::size_t field = header.find(label);
    if (field == std::string::npos) return false;
    const std::size_t start = field + std::strlen(label);
    if (start + 10 > header.size()) return false;
    header.replace(start, 10, value, 10);
    return true;
}

bool encodeBase64(const std::string& data, std::string& encoded) {
    encoded.clear();
    if (data.empty() || data.size() > kMaxSnapshotDataBytes ||
        data.size() > std::numeric_limits<DWORD>::max()) return false;
    DWORD required = 0;
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.data()),
                              static_cast<DWORD>(data.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &required) || required == 0) return false;
    encoded.resize(required);
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.data()),
                              static_cast<DWORD>(data.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              encoded.data(), &required)) {
        encoded.clear();
        return false;
    }
    if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
    return !encoded.empty();
}

bool decodeBase64(const std::string& encoded, std::string& data) {
    data.clear();
    if (encoded.empty() || encoded.size() > 12u * 1024u * 1024u ||
        encoded.size() > std::numeric_limits<DWORD>::max()) return false;
    DWORD required = 0;
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()),
                              CRYPT_STRING_BASE64, nullptr, &required, nullptr, nullptr) ||
        required == 0 || required > kMaxSnapshotDataBytes) return false;
    data.resize(required);
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()),
                              CRYPT_STRING_BASE64,
                              reinterpret_cast<BYTE*>(data.data()), &required, nullptr, nullptr)) {
        data.clear();
        return false;
    }
    data.resize(required);
    return !data.empty();
}

std::string escapeHtml(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

std::string unescapeHtml(std::string value) {
    const std::pair<const char*, char> entities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''},
    };
    for (const auto& entity : entities) {
        std::size_t offset = 0;
        const std::size_t length = std::strlen(entity.first);
        while ((offset = value.find(entity.first, offset)) != std::string::npos) {
            value.replace(offset, length, 1, entity.second);
            ++offset;
        }
    }
    return value;
}

bool resolveSnapshotFormats(const ClipboardSnapshot& snapshot,
                            std::vector<ResolvedFormat>& resolved) {
    resolved.clear();
    ClipboardSnapshot sorted = snapshot;
    sortSnapshot(sorted);
    for (const ClipboardSnapshotFormat& item : sorted.formats) {
        if (item.data.empty() || item.data.size() > kMaxFormatDataBytes) return false;
        UINT format = item.standardFormat;
        if (format == 0) {
            const std::wstring name = utf8ToWide(item.name);
            if (name.empty()) return false;
            format = RegisterClipboardFormatW(name.c_str());
        }
        if (format == 0) return false;
        const std::string lower = lowerAscii(item.name);
        const bool textLike = item.standardFormat == CF_UNICODETEXT || item.standardFormat == CF_TEXT ||
            item.standardFormat == CF_OEMTEXT || lower == "html format" ||
            lower == "rich text format" || lower == "mathml" || lower == "mathml presentation";
        resolved.push_back({format, item.data, textLike});
    }
    return !resolved.empty();
}

} // namespace

bool captureClipboardSnapshot(ClipboardSnapshot& snapshot, std::string& displayText,
                              bool requireRichFormat) {
    snapshot = ClipboardSnapshot{};
    displayText.clear();
    std::size_t totalBytes = 0;
    readDisplayText(displayText);
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        std::string name;
        const bool standard = isSupportedStandardFormat(format);
        if (!standard && !getClipboardFormatName(format, name)) continue;
        if (!standard && isExcludedRegisteredFormat(name)) continue;
        if (standard && format != CF_UNICODETEXT && format != CF_TEXT && format != CF_OEMTEXT &&
            format != CF_DIB && format != CF_DIBV5) continue;
        std::string data;
        if (!readGlobalData(format, data)) continue;
        ClipboardSnapshotFormat item;
        item.standardFormat = standard ? static_cast<std::uint16_t>(format) : 0;
        item.name = std::move(name);
        item.data = std::move(data);
        appendUniqueFormat(snapshot, std::move(item), totalBytes);
    }
    sortSnapshot(snapshot);
    bool hasNamedFormat = false;
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        if (!item.name.empty()) {
            hasNamedFormat = true;
            break;
        }
    }
    if (requireRichFormat && !hasNamedFormat) {
        snapshot = ClipboardSnapshot{};
        return false;
    }
    return !snapshot.formats.empty();
}

bool makeClipboardSnapshotEnvelope(const ClipboardSnapshot& input, std::string& envelope) {
    envelope.clear();
    ClipboardSnapshot snapshot = input;
    sortSnapshot(snapshot);
    if (snapshot.formats.empty() || snapshot.formats.size() > kMaxSnapshotFormats) return false;
    std::size_t totalData = 0;
    std::size_t totalSize = sizeof(SnapshotHeader) + snapshot.formats.size() * sizeof(SnapshotEntry);
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        if (item.standardFormat == 0 &&
            (item.name.empty() || item.name.size() > kMaxFormatNameBytes)) return false;
        if (item.standardFormat != 0 && !item.name.empty()) return false;
        if (item.data.empty() || item.data.size() > kMaxFormatDataBytes ||
            totalData > kMaxSnapshotDataBytes - item.data.size()) return false;
        if (totalSize > std::numeric_limits<std::size_t>::max() - item.name.size() - item.data.size()) return false;
        totalData += item.data.size();
        totalSize += item.name.size() + item.data.size();
    }
    if (totalSize > sizeof(SnapshotHeader) + kMaxSnapshotDataBytes +
                         kMaxSnapshotFormats * (sizeof(SnapshotEntry) + kMaxFormatNameBytes)) return false;
    SnapshotHeader header{kSnapshotMagic, kSnapshotVersion, static_cast<std::uint16_t>(sizeof(SnapshotHeader)),
                          static_cast<std::uint32_t>(snapshot.formats.size()),
                          static_cast<std::uint32_t>(totalSize)};
    envelope.resize(totalSize);
    std::memcpy(envelope.data(), &header, sizeof(header));
    std::size_t offset = sizeof(header);
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        SnapshotEntry entry{item.standardFormat, 0, static_cast<std::uint32_t>(item.name.size()),
                            static_cast<std::uint32_t>(item.data.size())};
        std::memcpy(envelope.data() + offset, &entry, sizeof(entry));
        offset += sizeof(entry);
    }
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        if (!item.name.empty()) {
            std::memcpy(envelope.data() + offset, item.name.data(), item.name.size());
            offset += item.name.size();
        }
        std::memcpy(envelope.data() + offset, item.data.data(), item.data.size());
        offset += item.data.size();
    }
    return offset == envelope.size();
}

bool parseClipboardSnapshotEnvelope(const std::string& envelope, ClipboardSnapshot& snapshot) {
    snapshot = ClipboardSnapshot{};
    if (envelope.size() < sizeof(SnapshotHeader) || envelope.size() > kMaxSnapshotDataBytes + 64u * 1024u) {
        return false;
    }
    SnapshotHeader header{};
    std::memcpy(&header, envelope.data(), sizeof(header));
    if (header.magic != kSnapshotMagic || header.version != kSnapshotVersion ||
        header.headerSize != sizeof(SnapshotHeader) || header.formatCount == 0 ||
        header.formatCount > kMaxSnapshotFormats || header.totalSize != envelope.size()) return false;
    const std::size_t entriesBytes = static_cast<std::size_t>(header.formatCount) * sizeof(SnapshotEntry);
    if (entriesBytes > envelope.size() - sizeof(SnapshotHeader)) return false;
    std::vector<SnapshotEntry> entries(header.formatCount);
    std::memcpy(entries.data(), envelope.data() + sizeof(SnapshotHeader), entriesBytes);
    std::size_t offset = sizeof(SnapshotHeader) + entriesBytes;
    std::size_t totalData = 0;
    for (const SnapshotEntry& entry : entries) {
        if ((entry.standardFormat == 0 && entry.nameSize == 0) ||
            (entry.standardFormat != 0 && entry.nameSize != 0) ||
            entry.nameSize > kMaxFormatNameBytes || entry.dataSize == 0 ||
            entry.dataSize > kMaxFormatDataBytes || offset > envelope.size() ||
            entry.nameSize > envelope.size() - offset) return false;
        offset += entry.nameSize;
        if (entry.dataSize > envelope.size() - offset ||
            totalData > kMaxSnapshotDataBytes - entry.dataSize) return false;
        totalData += entry.dataSize;
        offset += entry.dataSize;
    }
    if (offset != envelope.size()) return false;
    offset = sizeof(SnapshotHeader) + entriesBytes;
    for (const SnapshotEntry& entry : entries) {
        ClipboardSnapshotFormat item;
        item.standardFormat = entry.standardFormat;
        if (entry.nameSize != 0) {
            item.name.assign(envelope.data() + offset, entry.nameSize);
            offset += entry.nameSize;
            if (utf8ToWide(item.name).empty()) return false;
        }
        item.data.assign(envelope.data() + offset, entry.dataSize);
        offset += entry.dataSize;
        std::size_t ignored = 0;
        if (!appendUniqueFormat(snapshot, std::move(item), ignored)) return false;
    }
    sortSnapshot(snapshot);
    return snapshot.formats.size() == header.formatCount;
}

bool buildClipboardSnapshotHtml(const ClipboardSnapshot& snapshot, const std::string& displayText,
                                std::string& html) {
    html.clear();
    std::string envelope;
    std::string encoded;
    if (displayText.size() > kMaxDisplayTextBytes ||
        !makeClipboardSnapshotEnvelope(snapshot, envelope) || !encodeBase64(envelope, encoded)) return false;
    std::string fragment = kSnapshotHtmlMarker;
    fragment += encoded;
    fragment += "--><span>";
    fragment += escapeHtml(displayText);
    fragment += "</span>";
    const std::string prefix = "<html><body><!--StartFragment-->";
    const std::string suffix = "<!--EndFragment--></body></html>";
    std::string header = "Version:1.0\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\n"
                         "StartFragment:0000000000\r\nEndFragment:0000000000\r\n"
                         "SourceURL:about:blank\r\n";
    const std::size_t startHtml = header.size();
    const std::size_t startFragment = startHtml + prefix.size();
    const std::size_t endFragment = startFragment + fragment.size();
    const std::size_t endHtml = endFragment + suffix.size();
    if (endHtml > kMaxCfHtmlBytes || !setCfHtmlOffset(header, "StartHTML:", startHtml) ||
        !setCfHtmlOffset(header, "EndHTML:", endHtml) ||
        !setCfHtmlOffset(header, "StartFragment:", startFragment) ||
        !setCfHtmlOffset(header, "EndFragment:", endFragment)) return false;
    html = header + prefix + fragment + suffix;
    return html.size() == endHtml;
}

bool parseClipboardSnapshotHtml(const std::string& html, ClipboardSnapshot& snapshot,
                                std::string* displayText) {
    snapshot = ClipboardSnapshot{};
    const std::size_t marker = html.find(kSnapshotHtmlMarker);
    if (marker == std::string::npos) return false;
    const std::size_t start = marker + sizeof(kSnapshotHtmlMarker) - 1;
    const std::size_t end = html.find("-->", start);
    if (end == std::string::npos || end == start) return false;
    std::string envelope;
    if (!decodeBase64(html.substr(start, end - start), envelope) ||
        !parseClipboardSnapshotEnvelope(envelope, snapshot)) return false;
    if (displayText) {
        const std::size_t spanStart = html.find("<span>", end + 3);
        const std::size_t spanEnd = spanStart == std::string::npos ? std::string::npos
                                                                    : html.find("</span>", spanStart + 6);
        if (spanStart != std::string::npos && spanEnd != std::string::npos) {
            *displayText = unescapeHtml(html.substr(spanStart + 6, spanEnd - spanStart - 6));
        } else {
            displayText->clear();
        }
    }
    return true;
}

bool setSnapshotDirect(const ClipboardSnapshot& snapshot) {
    std::vector<ResolvedFormat> resolved;
    if (!resolveSnapshotFormats(snapshot, resolved)) return false;
    bool opened = false;
    for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = OpenClipboard(nullptr) != FALSE;
        if (!opened) Sleep(5);
    }
    if (!opened || !EmptyClipboard()) {
        if (opened) CloseClipboard();
        return false;
    }
    std::vector<HGLOBAL> pending;
    pending.reserve(resolved.size());
    for (const ResolvedFormat& item : resolved) {
        const std::size_t extra = item.textLike ? 1 : 0;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | (extra ? GMEM_ZEROINIT : 0),
                                     item.data.size() + extra);
        if (!memory) {
            for (HGLOBAL handle : pending) GlobalFree(handle);
            CloseClipboard();
            return false;
        }
        void* destination = GlobalLock(memory);
        if (!destination) {
            GlobalFree(memory);
            for (HGLOBAL handle : pending) GlobalFree(handle);
            CloseClipboard();
            return false;
        }
        std::memcpy(destination, item.data.data(), item.data.size());
        GlobalUnlock(memory);
        pending.push_back(memory);
    }
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        if (!SetClipboardData(resolved[index].format, pending[index])) {
            GlobalFree(pending[index]);
            for (std::size_t remaining = index + 1; remaining < pending.size(); ++remaining) {
                GlobalFree(pending[remaining]);
            }
            CloseClipboard();
            return false;
        }
        pending[index] = nullptr;
    }
    CloseClipboard();
    return true;
}

bool setClipboardSnapshot(const ClipboardSnapshot& snapshot) {
    try {
        return setSnapshotDirect(snapshot);
    } catch (...) {
        return false;
    }
}

std::uint64_t clipboardSnapshotFingerprint(const ClipboardSnapshot& input) {
    ClipboardSnapshot snapshot = input;
    sortSnapshot(snapshot);
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&hash](const char* data, std::size_t size) {
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= static_cast<unsigned char>(data[index]);
            hash *= 1099511628211ULL;
        }
    };
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        const std::string key = formatKey(item);
        add(key.data(), key.size());
        const char separator = '\0';
        add(&separator, 1);
        add(item.data.data(), item.data.size());
        add(&separator, 1);
    }
    return hash == 0 ? 1 : hash;
}

const ClipboardSnapshotFormat* findClipboardSnapshotFormat(const ClipboardSnapshot& snapshot,
                                                           const char* name) {
    if (!name) return nullptr;
    const std::string expected = lowerAscii(name);
    for (const ClipboardSnapshotFormat& item : snapshot.formats) {
        if (item.standardFormat == 0 && lowerAscii(item.name) == expected) return &item;
    }
    return nullptr;
}
