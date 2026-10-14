#include "gz/XmlSax.h"

#include <cctype>
#include <cstdlib>

namespace {

std::string DecodeEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') { out += in[i]; continue; }
        const auto semi = in.find(';', i);
        if (semi == std::string::npos) { out += in[i]; continue; }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const long v = std::strtol(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            if (v > 0 && v < 128) out += static_cast<char>(v);
        } else {
            out += in.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

}  // namespace

bool xml::Parse(const char* data, std::size_t size, Handler& h) {
    std::size_t i = 0;
    std::vector<std::string> open;
    auto starts = [&](const char* lit) {
        std::size_t n = 0;
        while (lit[n]) ++n;
        return i + n <= size && std::string(data + i, n) == lit;
    };
    while (i < size) {
        if (data[i] != '<') {
            const std::size_t start = i;
            while (i < size && data[i] != '<') ++i;
            if (!open.empty() && !h.CharData(DecodeEntities(std::string(data + start, i - start)))) return false;
            continue;
        }
        if (starts("<?")) {
            const auto end = std::string(data, size).find("?>", i);
            if (end == std::string::npos) return false;
            i = end + 2;
        } else if (starts("<!--")) {
            const auto end = std::string(data, size).find("-->", i);
            if (end == std::string::npos) return false;
            i = end + 3;
        } else if (starts("<![CDATA[")) {
            const auto end = std::string(data, size).find("]]>", i);
            if (end == std::string::npos) return false;
            if (!h.CharData(std::string(data + i + 9, end - i - 9))) return false;
            i = end + 3;
        } else if (starts("</")) {
            const auto end = std::string(data, size).find('>', i);
            if (end == std::string::npos) return false;
            std::string name(data + i + 2, end - i - 2);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
            if (open.empty() || open.back() != name) return false;
            open.pop_back();
            if (!h.EndElement(name)) return false;
            i = end + 1;
        } else {
            ++i;  // '<'
            const std::size_t nameStart = i;
            while (i < size && !std::isspace(static_cast<unsigned char>(data[i])) && data[i] != '>' && data[i] != '/') ++i;
            const std::string name(data + nameStart, i - nameStart);
            std::vector<std::pair<std::string, std::string>> attrs;
            bool selfClose = false;
            while (i < size) {
                while (i < size && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
                if (i >= size) return false;
                if (data[i] == '>') { ++i; break; }
                if (data[i] == '/') { selfClose = true; ++i; continue; }
                const std::size_t an = i;
                while (i < size && data[i] != '=' && !std::isspace(static_cast<unsigned char>(data[i])) && data[i] != '>') ++i;
                const std::string attrName(data + an, i - an);
                while (i < size && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
                if (i >= size || data[i] != '=') return false;
                ++i;
                while (i < size && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
                if (i >= size || (data[i] != '"' && data[i] != '\'')) return false;
                const char quote = data[i++];
                const std::size_t vs = i;
                while (i < size && data[i] != quote) ++i;
                if (i >= size) return false;
                attrs.emplace_back(attrName, DecodeEntities(std::string(data + vs, i - vs)));
                ++i;
            }
            if (name.empty()) return false;
            open.push_back(name);
            if (!h.StartElement(name, attrs)) return false;
            if (selfClose) {
                open.pop_back();
                if (!h.EndElement(name)) return false;
            }
        }
    }
    return open.empty();
}
