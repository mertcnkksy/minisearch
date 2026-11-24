#pragma once

#include <ostream>
#include <string_view>

namespace minisearch::cli {

inline void write_text(std::ostream &output, std::string_view text) {
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Escape untrusted bytes at the terminal boundary, including C1 control bytes.
inline void write_escaped(std::ostream &output, std::string_view text) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\\') {
            write_text(output, "\\\\");
        } else if (byte >= 0x20 && byte <= 0x7E) {
            output.put(character);
        } else {
            const char escaped[]{'\\', 'x', hex[byte >> 4], hex[byte & 0x0F]};
            output.write(escaped, sizeof(escaped));
        }
    }
}

} // namespace minisearch::cli
