#pragma once

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace jatayu {

using Decimal = boost::multiprecision::cpp_dec_float_50;

enum class Error { DivisionByZero, NegativeRoot, Domain, Range };

std::string numberText(const Decimal &value);
std::string grouped(std::string value);

struct Term {
    char operation;
    Decimal value;
    std::string label;
};

class Calculator {
public:
    static constexpr size_t maxExpressionBytes = 4096;
    static constexpr size_t maxSnapshotBytes = 64 * 1024;

    enum class Memory { Clear, Recall, Add, Subtract, Store };

    void digit(char key);
    void operation(char key);
    void backspace();
    void clearEntry();
    void clear();
    void sign();
    void unary(char key);
    void scientific(char key, bool radians);
    void constant(char key);
    void memory(Memory key);
    bool equals();
    std::string snapshot() const;
    bool restore(const std::string &snapshot);
    bool hasMemory() const { return memoryAvailable; }
    bool isFinished() const { return finished; }
    std::string display() const;
    std::string expression() const;

private:
    std::vector<Term> terms;
    std::string entry = "0";
    std::string label;
    std::string lastExpression;
    char pending = '+';
    bool replaceEntry = true;
    bool waiting = false;
    bool finished = false;
    bool memoryAvailable = false;
    Decimal memoryValue = 0;

    Decimal current() const;
    void setValue(const Decimal &value, std::string description = {});
    void startEntry();
    Decimal evaluate(bool includeEntry) const;
};

}
