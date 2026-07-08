#pragma once
#include <cstdlib>
#define ASSERTM(cond, ...) ((cond) ? void(0) : std::abort())
