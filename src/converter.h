#pragma once

#include "calculator.h"
#include <vector>

namespace jatayu {

struct Unit {
    const char *symbol;
    const char *english;
    const char *hindi;
    const char *numerator;
    const char *denominator = "1";
    const char *offset = "0";
};

struct UnitCategory {
    const char *english;
    const char *hindi;
    std::vector<Unit> units;
};

const std::vector<UnitCategory> &conversionCategories();
Decimal convert(const Decimal &value, const Unit &from, const Unit &to);

}
