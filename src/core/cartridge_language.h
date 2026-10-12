#pragma once
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace oceanblast {
inline int resolveCartridgeLanguageByte(const std::string& mode, unsigned uiLang, const std::string& cartPath) {
    static constexpr int kLangBytes[7] = {1, 3, 9, 8, 16, 2, 15};
    static constexpr const char* kLangTags[7] = {"EN", "DE", "ES", "IT", "NL", "FR", "TR"};
    if (mode == "keep") return -1;
    if (mode == "ask" || mode == "menu" || mode == "flags") return 0xFF;
    if (mode == "en") return 1;
    if (mode == "de") return 3;
    if (mode == "es") return 9;
    if (mode == "it") return 8;
    if (mode == "nl") return 16;
    if (mode == "fr") return 2;
    if (mode == "tr") return 15;
    const unsigned idx = uiLang < 7 ? uiLang : 0;
    const std::string stem = std::filesystem::path(cartPath).stem().string();
    const auto openPos = stem.rfind('(');
    const auto closePos = stem.rfind(')');
    if (openPos != std::string::npos && closePos != std::string::npos && closePos > openPos + 1) {
        const std::string inside = stem.substr(openPos + 1, closePos - openPos - 1);
        if (inside != "EU") {
            std::istringstream iss(inside);
            std::string tok;
            std::vector<std::string> tokens;
            while (iss >> tok) tokens.push_back(tok);
            bool found = false;
            for (const auto& t : tokens) {
                if (t == kLangTags[idx]) { found = true; break; }
            }
            bool explicitList = !tokens.empty();
            for (const auto& t : tokens) {
                bool known = false;
                for (const auto* tag : kLangTags) if (t == tag) known = true;
                if (!known) explicitList = false;
            }
            if (!found && explicitList) return 0xFF;
        }
    }
    return kLangBytes[idx];
}

}
