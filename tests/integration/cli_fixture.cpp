#include "fixtures/golden_corpus.hpp"
#include "session_fixture.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace {

void write(std::string_view name, std::string_view contents) {
    std::ofstream stream(std::string(name), std::ios::binary);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    if (!stream) {
        throw std::runtime_error("Could not write CLI fixture");
    }
}

void write_fixture(const minisearch::test::SessionFixture &fixture, bool crlf) {
    if (crlf) {
        std::string input;
        for (const auto byte : fixture.input) {
            if (byte == '\n') {
                input += '\r';
            }
            input += byte;
        }
        // The final command runs at EOF without a terminator.
        input.resize(input.size() - 2);
        write("session.in", input);
    } else {
        write("session.in", fixture.input);
    }
    write("expected.out", fixture.output);
    write("expected.err", fixture.errors);
}

} // namespace

int main(int argc, char *argv[]) {
    using minisearch::test::golden_documents;
    using minisearch::test::SessionFixture;
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    SessionFixture fixture;
    const auto scenario = argc == 2 ? std::string_view(argv[1]) : std::string_view{};
    if (scenario == "fresh") {
        fixture.search("search python", "python", 10);
        fixture.statistics();
        fixture.command("quit");
        write_fixture(fixture, false);
        return 0;
    }
    if (scenario != "lf" && scenario != "crlf") {
        return 2;
    }
    fixture.error("index missing corpus file.txt", "File not found: missing corpus file.txt");
    for (std::size_t id = 0; id < golden_documents.size(); ++id) {
        const auto text = golden_documents[id];
        if (id % 4 == 0) {
            fixture.add("add " + std::string(text), text);
        } else {
            const auto name = "golden document " + std::to_string(id) + ".txt";
            write(name, text);
            fixture.add("index " + name, text);
        }
    }
    fixture.command("help");
    fixture.output += "Available commands\n\n"
                      "  add <text>\n  index <path>\n  search [--top K] [--] <query>\n"
                      "  stats\n  help\n  exit\n  quit\n\n"
                      "Documents are stored in memory for the current session.\n"
                      "Queries and document contents must be ASCII.\n"
                      "Default result limit: 10. File size limit: 16 MiB.\n";
    fixture.error("add \xFF", "Non-ASCII byte at offset 0");
    fixture.error("search \xFF", "Non-ASCII byte at offset 0");
    fixture.error("search --top -1 python",
                  "Invalid result limit. Expected an unsigned decimal integer.");
    fixture.error("search --top " + std::to_string(max) + "0 python",
                  "Invalid result limit. Expected an unsigned decimal integer.");
    fixture.search("search python", "python", 10);
    fixture.search("search --top 3 tree index", "tree index", 3);
    fixture.search("search --top " + std::to_string(max) + " -- CACHE--network", "CACHE--network",
                   max);
    fixture.search("search --top 0 sql", "sql", 0);
    fixture.search("search quokka", "quokka", 10);
    fixture.statistics();
    if (scenario == "lf") {
        fixture.command("exit");
        fixture.input += "add must not be processed\n";
    }
    write_fixture(fixture, scenario == "crlf");
}
