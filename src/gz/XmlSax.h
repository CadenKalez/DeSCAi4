// PORT-ONLY minimal non-validating SAX parser used by the property-set XML readers in place of the
// framework's expat-based parser (service 0x6b6b01d1, not recovered). It supports what the game's
// property-set files use: an optional <?xml?> declaration, comments, elements with quoted
// attributes, character data (entity references decoded) and CDATA sections.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace xml {

class Handler {
public:
    virtual ~Handler() = default;
    // Returning false aborts the parse (the binary's handlers use this to reject documents).
    virtual bool StartElement(const std::string& name, const std::vector<std::pair<std::string, std::string>>& attrs) = 0;
    virtual bool EndElement(const std::string& name) = 0;
    virtual bool CharData(const std::string& text) = 0;
};

bool Parse(const char* data, std::size_t size, Handler& handler);

}  // namespace xml
