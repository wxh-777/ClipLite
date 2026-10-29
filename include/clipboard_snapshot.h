#ifndef CLIPLITE_CLIPBOARD_SNAPSHOT_H
#define CLIPLITE_CLIPBOARD_SNAPSHOT_H

#include <cstdint>
#include <string>
#include <vector>

struct ClipboardSnapshotFormat {
    std::uint16_t standardFormat = 0;
    std::string name;
    std::string data;
};

struct ClipboardSnapshot {
    std::vector<ClipboardSnapshotFormat> formats;
};

// 从已打开的系统剪贴板读取可持久化的 HGLOBAL 格式。
bool captureClipboardSnapshot(ClipboardSnapshot& snapshot, std::string& displayText,
                             bool requireRichFormat = true);

// 序列化和解析多格式快照；注册格式按名称保存，避免数字 ID 跨会话失效。
bool makeClipboardSnapshotEnvelope(const ClipboardSnapshot& snapshot, std::string& envelope);
bool parseClipboardSnapshotEnvelope(const std::string& envelope, ClipboardSnapshot& snapshot);

// 将快照封装成标准 CF_HTML，并从 CF_HTML 中恢复快照。
bool buildClipboardSnapshotHtml(const ClipboardSnapshot& snapshot,
                                const std::string& displayText, std::string& html);
bool parseClipboardSnapshotHtml(const std::string& html, ClipboardSnapshot& snapshot,
                                std::string* displayText = nullptr);

// 在 OLE 初始化的线程上回放快照。
bool setClipboardSnapshot(const ClipboardSnapshot& snapshot);

// 按稳定格式键计算快照指纹，用于历史去重。
std::uint64_t clipboardSnapshotFingerprint(const ClipboardSnapshot& snapshot);

const ClipboardSnapshotFormat* findClipboardSnapshotFormat(
    const ClipboardSnapshot& snapshot, const char* name);

#endif
