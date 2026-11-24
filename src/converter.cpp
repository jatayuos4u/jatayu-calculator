#include "converter.h"

namespace jatayu {

const std::vector<UnitCategory> &conversionCategories()
{
    static const std::vector<UnitCategory> categories{
        {"Length", "लंबाई", {
            {"mm", "Millimetres", "मिलीमीटर", "0.001"},
            {"cm", "Centimetres", "सेंटीमीटर", "0.01"},
            {"m", "Metres", "मीटर", "1"},
            {"km", "Kilometres", "किलोमीटर", "1000"},
            {"in", "Inches", "इंच", "0.0254"},
            {"ft", "Feet", "फ़ीट", "0.3048"},
            {"yd", "Yards", "गज", "0.9144"},
            {"mi", "Miles", "मील", "1609.344"}}},
        {"Mass", "द्रव्यमान", {
            {"mg", "Milligrams", "मिलीग्राम", "0.000001"},
            {"g", "Grams", "ग्राम", "0.001"},
            {"kg", "Kilograms", "किलोग्राम", "1"},
            {"t", "Metric tonnes", "मीट्रिक टन", "1000"},
            {"oz", "Ounces", "औंस", "0.028349523125"},
            {"lb", "Pounds", "पाउंड", "0.45359237"}}},
        {"Temperature", "तापमान", {
            {"°C", "Celsius", "सेल्सियस", "1"},
            {"°F", "Fahrenheit", "फ़ारेनहाइट", "5", "9", "-32"},
            {"K", "Kelvin", "केल्विन", "1", "1", "-273.15"}}},
        {"Speed", "गति", {
            {"m/s", "Metres per second", "मीटर प्रति सेकंड", "1"},
            {"km/h", "Kilometres per hour", "किलोमीटर प्रति घंटा", "1000", "3600"},
            {"mph", "Miles per hour", "मील प्रति घंटा", "0.44704"},
            {"kn", "Knots", "नॉट", "1852", "3600"}}},
        {"Data size", "डेटा आकार", {
            {"bit", "Bits", "बिट", "1", "8"},
            {"B", "Bytes", "बाइट", "1"},
            {"kB", "Kilobytes", "किलोबाइट", "1000"},
            {"MB", "Megabytes", "मेगाबाइट", "1000000"},
            {"GB", "Gigabytes", "गीगाबाइट", "1000000000"},
            {"TB", "Terabytes", "टेराबाइट", "1000000000000"},
            {"KiB", "Kibibytes", "किबिबाइट", "1024"},
            {"MiB", "Mebibytes", "मेबिबाइट", "1048576"},
            {"GiB", "Gibibytes", "गिबिबाइट", "1073741824"},
            {"TiB", "Tebibytes", "टेबिबाइट", "1099511627776"}}}
    };
    return categories;
}

Decimal convert(const Decimal &value, const Unit &from, const Unit &to)
{
    if (&from == &to)
        return value;
    const Decimal adjusted = value + Decimal(from.offset);
    return adjusted * Decimal(from.numerator) * Decimal(to.denominator)
        / (Decimal(from.denominator) * Decimal(to.numerator)) - Decimal(to.offset);
}

}
