#pragma once

#include "reference_search.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace minisearch::test {

// Expected output is written from independent operations chosen by the generator.
struct SessionFixture {
    ReferenceSearch corpus;
    std::string input;
    std::string output = "MiniSearch\nIn-memory document search engine.\n"
                         "Type 'help' for available commands.\n\n";
    std::string errors;

    void command(std::string_view line) {
        input.append(line).push_back('\n');
        output += "minisearch> ";
    }

    void add(std::string_view line, std::string_view document) {
        command(line);
        output += "Indexed document " + std::to_string(corpus.add(document)) + ".\n";
    }

    void error(std::string_view line, std::string_view message) {
        command(line);
        errors.append("Error: ").append(message).push_back('\n');
    }

    void statistics() {
        command("stats");
        std::ostringstream stats;
        stats.imbue(std::locale::classic());
        stats << "Documents:      " << corpus.documents() << "\nUnique terms:   " << corpus.terms()
              << "\nTotal tokens:   " << corpus.tokens() << "\nAvg. length:    " << std::fixed
              << std::setprecision(6) << corpus.average_length() << '\n';
        output += stats.str();
    }

    void search(std::string_view line, std::string_view query, std::size_t k) {
        command(line);
        const auto ranked = corpus.rank(query);
        const auto count = std::min(k, ranked.size());
        if (count == 0) {
            output += "No results.\n";
            return;
        }
        std::ostringstream results;
        results.imbue(std::locale::classic());
        results << "Rank  Doc ID  Score\n" << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < count; ++i) {
            results << i + 1 << "     " << ranked[i].id << "       " << ranked[i].score << '\n';
        }
        results << "\nReturned " << count << (count == 1 ? " result.\n" : " results.\n");
        output += results.str();
    }
};

} // namespace minisearch::test
