#pragma once
#include "../core/types.h"
#include <string>
#include <vector>

namespace oceanblast {

struct CartridgeInfo {
    std::string name;
    size_t size = 0;
    bool hasBootloader = false;
    bool hasLinuxKernel = false;
};

class CartParser {
public:
    static CartridgeInfo parse(const std::vector<u8>& data);
    static void printInfo(const CartridgeInfo& info);
};

} // namespace oceanblast
