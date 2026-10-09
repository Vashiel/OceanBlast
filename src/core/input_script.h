#pragma once
#include <cstdint>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace oceanblast {
struct InputEvent { uint64_t step; uint32_t mask; };
inline bool readInputScript(std::istream& input, std::vector<InputEvent>& events, std::string& error) {
    events.clear();
    error.clear();
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line);
        std::string stepText, maskText, extra;
        if (!(row >> stepText)) continue;
        try {
            if (!(row >> maskText) || (row >> extra) || stepText.front()=='-' || maskText.front()=='-') throw 0;
            size_t stepEnd = 0, maskEnd = 0;
            const uint64_t step = std::stoull(stepText, &stepEnd, 10);
            const uint64_t mask = std::stoull(maskText, &maskEnd, 16);
            if (stepEnd != stepText.size() || maskEnd != maskText.size() || mask > 0x1fff ||
                (!events.empty() && step <= events.back().step)) throw 0;
            events.push_back({step, static_cast<uint32_t>(mask)});
        } catch (...) {
            events.clear();
            error = "Invalid input event at line " + std::to_string(lineNumber);
            return false;
        }
    }
    return true;
}
}
