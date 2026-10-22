#include "sc4/MiscTypes.h"

#include <strings.h>

namespace sc4 {

bool HTTPMessage::GetMessageHeaderEntryCopy(const std::string& name, std::string* value, unsigned index) const {
    unsigned seen = 0;
    for (const HTTPHeaderEntry& entry : headers) {
        if (strcasecmp(entry.name.c_str(), name.c_str()) != 0) continue;
        if (seen == index) {
            if (value) *value = entry.value;
            return true;
        }
        ++seen;
    }
    if (value) value->clear();
    return false;
}

namespace {
constexpr const char* kHeaderFieldNames[54] = {
    "Cache-Control",
    "Connection",
    "Date",
    "Pragma",
    "Transfer-Encoding",
    "Upgrade",
    "Via",
    "Accept",
    "Accept-Charset",
    "Accept-Encoding",
    "Accept-Language",
    "Authorization",
    "From",
    "Host",
    "If-Modified-Since",
    "If-Match",
    "If-None-Match",
    "If-Range",
    "If-Unmodified-Since",
    "Max-Forwards",
    "Proxy-Authorization",
    "Range",
    "Referer",
    "User-Agent",
    "Age",
    "Location",
    "Proxy-Authenticate",
    "Public",
    "Retry-After",
    "Server",
    "Vary",
    "Warning",
    "WWW-Authenticate",
    "Allow",
    "Content-Base",
    "Content-Encoding",
    "Content-Language",
    "Content-Length",
    "Content-Location",
    "Content-MD5",
    "Content-Range",
    "Content-Type",
    "ETag",
    "Expires",
    "Last-Modified",
    "Alternates",
    "Content-Version",
    "Derived-From",
    "Link",
    "URI",
    "Keep-Alive",
    "Set-Cookie",
    "Set-Cookie2",
    "Cookie"};

void TrimBlanks(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
}
}  // namespace

const char* HTTPMessage::MessageHeaderFieldToFieldString(int field) {
    return static_cast<unsigned>(field - 1) < 0x36u ? kHeaderFieldNames[field - 1] : nullptr;
}

bool HTTPMessage::GetMessageHeaderEntryCopy(int field, std::string* value, unsigned index) const {
    const char* name = MessageHeaderFieldToFieldString(field);
    return name ? GetMessageHeaderEntryCopy(std::string(name), value, index) : false;
}

unsigned HTTPMessage::GetSeparatedStringList(const std::string& text, std::string* out, unsigned maxCount, bool quoted, char separator, bool trim) {
    if (maxCount == 0) return 0;
    for (unsigned i = 0; i < maxCount; ++i) out[i].clear();
    unsigned filled = 0;
    bool inQuote = false, full = false;
    for (std::size_t at = 0; at < text.size() && !full; ++at) {
        const char c = text[at];
        if (c == '"' && quoted) {
            inQuote = !inQuote;
        } else if (inQuote || c != separator) {
            out[filled].push_back(c);
        } else {
            if (trim) TrimBlanks(out[filled]);
            if (quoted && out[filled].size() > 1 && out[filled].front() == '"' && out[filled].back() == '"') {
                out[filled].erase(0, 1);
                out[filled].pop_back();
            }
            if (++filled >= maxCount) full = true;
        }
    }
    if (!full && !out[filled].empty()) {   // the last part
        if (trim) TrimBlanks(out[filled]);
        ++filled;
    }
    if (filled != 0) return filled;
    if (text.empty()) return 0;
    out[0] = text;
    if (trim) TrimBlanks(out[0]);
    return 0;
}

int SharedMemoryPointer::Release() {
    if (refCount < 2) {
        delete[] data;
        delete this;
        return 0;
    }
    return --refCount;
}

short CacheData::Release() {
    if (refCount < 2) {
        delete[] data;
        delete this;
        return 0;
    }
    return static_cast<short>(--refCount);
}

void cAnimatedSpriteTableData::Init(ISpriteAnimTable* newTable) {
    if (table) {
        table->Release();
        table = nullptr;
        frames = nullptr;
    }
    if (!newTable) return;
    newTable->AddRef();
    if (table) table->Release();
    table = newTable;
    const SpriteAnimTableHeader* header = table->GetData();
    frames = reinterpret_cast<const unsigned char*>(header) + 0x20;
    countA = table->GetData()->countA;
    stepA = 6.2831855f / static_cast<float>(countA);
    stepB = 6.2831855f / static_cast<float>(table->GetData()->countB);
    stepC = 6.2831855f / static_cast<float>(table->GetData()->countC);
    startB = table->GetData()->startB;
    countB = static_cast<std::uint32_t>(static_cast<long>((table->GetData()->endB - startB) / stepB + 0.1f)) + 1;
    startC = table->GetData()->startC;
    countC = static_cast<std::uint32_t>(static_cast<long>((table->GetData()->endC - startC) / stepC + 0.1f)) + 1;
    frameCount = countC * countB;
}

}  // namespace sc4
