#include "calculator.h"
#include "converter.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

using jatayu::Calculator;

void expect(bool result, const char *description)
{
    if (!result) {
        std::cerr << "Failed: " << description << '\n';
        std::exit(1);
    }
}

void enter(Calculator &calculator, std::string_view input)
{
    for (const auto key : input) {
        switch (key) {
        case '+': case '-': case '*': case '/': case '^': calculator.operation(key); break;
        case '%': case 'r': case 's': case 'i': calculator.unary(key); break;
        case '=': calculator.equals(); break;
        default: calculator.digit(key); break;
        }
    }
}

int main()
{
    Calculator calculator;
    const auto verify = [&calculator](std::string_view input, std::string_view answer) {
        calculator.clear();
        enter(calculator, input);
        expect(calculator.display() == answer, input.data());
    };
    verify("1500+10%=", "1,650");
    verify("1500-10%=", "1,350");
    verify("200*15%=", "30");
    verify("200/50%=", "400");
    verify("0.1+0.2=", "0.3");
    verify("2+3*4=", "14");
    verify("5+=", "10");
    verify("144r=", "12");
    verify("12s=", "144");
    verify("8i=", "0.125");
    verify("12345678901234567890+1=", "12,345,678,901,234,567,891");
    calculator.clear();
    enter(calculator, "12+47");
    calculator.clearEntry();
    enter(calculator, "5=");
    expect(calculator.display() == "17", "clear entry keeps pending addition");
    calculator.clear();
    enter(calculator, "123");
    calculator.backspace();
    calculator.sign();
    expect(calculator.display() == "-12", "backspace and sign");
    calculator.memory(Calculator::Memory::Store);
    calculator.clear();
    enter(calculator, "5");
    calculator.memory(Calculator::Memory::Add);
    calculator.clear();
    enter(calculator, "9+");
    calculator.memory(Calculator::Memory::Recall);
    calculator.equals();
    expect(calculator.display() == "2", "memory recalled as pending operand");
    calculator.clear();
    enter(calculator, "1500+10%");
    const auto snapshot = calculator.snapshot();
    calculator.equals();
    expect(calculator.expression() == "1,500 + 10% =", "percentage annotation");
    expect(calculator.restore(snapshot), "restore history snapshot");
    enter(calculator, "20%=");
    expect(calculator.display() == "1,800", "edit recalled calculation");
    expect(!calculator.restore("bad"), "malformed snapshot rejected");
    expect(calculator.hasMemory(), "history restore preserves memory");
    calculator.memory(Calculator::Memory::Clear);
    expect(!calculator.hasMemory(), "memory cleared");
    try {
        verify("10/0=", "");
        expect(false, "division by zero");
    } catch (jatayu::Error issue) {
        expect(issue == jatayu::Error::DivisionByZero, "division by zero");
    }
    calculator.clear();
    enter(calculator, "4");
    calculator.sign();
    try {
        calculator.unary('r');
        expect(false, "negative square root");
    } catch (jatayu::Error issue) {
        expect(issue == jatayu::Error::NegativeRoot, "negative square root");
    }
    verify("2*3^2+1=", "19");
    verify("2^3^2=", "512");
    verify("2^0=", "1");
    verify("0^2=", "0");
    calculator.clear();
    enter(calculator, "2");
    calculator.sign();
    enter(calculator, "^3=");
    expect(calculator.display() == "-8", "negative base, integer exponent");

    calculator.clear();
    enter(calculator, "30");
    calculator.scientific('a', false);
    expect(calculator.display() == "0.5", "sin(30°)");
    calculator.clear();
    enter(calculator, "0");
    calculator.scientific('b', true);
    expect(calculator.display() == "1", "cos(0 rad)");
    calculator.clear();
    enter(calculator, "90");
    try {
        calculator.scientific('t', false);
        expect(false, "tan(90°)");
    } catch (jatayu::Error issue) {
        expect(issue == jatayu::Error::Domain, "tan(90°)");
    }
    calculator.clear();
    enter(calculator, "100");
    calculator.scientific('l', false);
    expect(calculator.display() == "2", "log(100)");
    calculator.clear();
    calculator.constant('e');
    calculator.scientific('n', false);
    expect(calculator.display() == "1", "ln(e)");

    const auto &categories = jatayu::conversionCategories();
    auto check = [&](size_t group, size_t from, size_t to, const char *value, const char *expected) {
        const auto &units = categories.at(group).units;
        expect(jatayu::numberText(jatayu::convert(jatayu::Decimal(value), units.at(from), units.at(to)))
            == expected, "unit conversion");
    };
    check(0, 7, 3, "1", "1.609344");
    check(1, 5, 2, "1", "0.45359237");
    check(2, 1, 0, "32", "0");
    check(2, 0, 1, "100", "212");
    check(2, 2, 0, "0", "-273.15");
    check(3, 1, 0, "36", "10");
    check(4, 7, 6, "1", "1024");
    check(4, 3, 2, "1", "1000");
    expect(calculator.restore(R"(1 / 0 "8" "" + 320 "320")"), "legacy history format");
    expect(calculator.equals() && calculator.display() == "40", "legacy history result");
    expect(!calculator.restore(R"(0 + 0 "bad" "")"), "invalid saved number rejected");
    expect(!calculator.restore(R"(0 + 0 "5" "" garbage)"), "invalid saved flags rejected");
    expect(!calculator.restore(std::string(Calculator::maxSnapshotBytes + 1, ' ')), "oversized snapshot rejected");
    expect(calculator.restore(R"(0 + 0 "5e2" "" 0)"), "valid scientific notation accepted");
    calculator.digit('2');
    expect(calculator.display() == "2", "typing replaces recalled scientific notation");
    calculator.clear();
    calculator.digit('1');
    bool hitLimit = false;
    try {
        for (size_t i = 0; i < Calculator::maxExpressionBytes; ++i)
            calculator.unary('r');
    } catch (jatayu::Error issue) {
        hitLimit = issue == jatayu::Error::Range;
    }
    expect(hitLimit, "nested function descriptions are bounded");
    std::cout << "Calculator regression checks passed.\n";
}
