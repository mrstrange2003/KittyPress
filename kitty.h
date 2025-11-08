// kitty.h
#pragma once
#include <string>

// Current write signature (KP02). We still accept KP01 when reading.
const std::string KITTY_MAGIC_V2 = "KP02";
const std::string KITTY_MAGIC_V1 = "KP01";
