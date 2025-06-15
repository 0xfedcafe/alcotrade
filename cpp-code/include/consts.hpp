#pragma once

#include <string_view>
inline int getIndex(std::string_view stock) {
    if(stock == "$CARD") {
        return 0;
    }
    if(stock == "$LOGN") {
        return 1;
    }
    if(stock == "$HEST") {
        return 2;
    }
    if(stock == "$JUMP") {
        return 3;
    }
    if(stock == "$GARR") {
        return 4;
    }
    if(stock == "$SIMP") {
        return 5;
    }
    return 0xdeadbeef;
}