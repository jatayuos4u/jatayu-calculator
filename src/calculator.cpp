#include "calculator.h"
#include <utility>
#include <iomanip>
#include <boost/math/constants/constants.hpp>
#include <sstream>
#include <regex>

namespace jatayu {

namespace {
constexpr size_t maxTerms = 512;

bool validDecimal(const std::string &value)
{
    static const std::regex decimal(R"([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]{1,8})?)");
    return value.size() <= 96 && std::regex_match(value, decimal);
}
}


std::string numberText(const Decimal &value)
{
    if (!boost::multiprecision::isfinite(value) || abs(value) >= Decimal("1e999"))
        throw Error::Range;
    if (value == 0)
        return "0";
    return value.str(34, std::ios_base::fmtflags(0));
}

std::string grouped(std::string value)
{
    if (value.find_first_of("eE") != std::string::npos)
        return value;
    const auto dot = value.find('.');
    const auto start = !value.empty() && value.front() == '-' ? 1 : 0;
    auto position = dot == std::string::npos ? value.size() : dot;
    while (position > static_cast<size_t>(start + 3)) {
        position -= 3;
        value.insert(position, ",");
    }
    return value;
}

Decimal Calculator::current() const
{
    return Decimal(entry);
}

void Calculator::setValue(const Decimal &value, std::string description)
{
    if (description.size() > maxExpressionBytes)
        throw Error::Range;
    (void)numberText(value);
    entry = value.str(0, std::ios_base::fmtflags(0));
    label = std::move(description);
    replaceEntry = true;
    waiting = false;
}

void Calculator::startEntry()
{
    if (finished)
        clear();
    if (replaceEntry) {
        entry = "0";
        label.clear();
        replaceEntry = false;
    }
    waiting = false;
}

void Calculator::digit(char key)
{
    startEntry();
    if (key == '.') {
        if (entry.find('.') == std::string::npos)
            entry += '.';
        return;
    }
    if (key < '0' || key > '9' || entry.size() >= 34)
        return;
    if (entry == "0")
        entry.clear();
    else if (entry == "-0")
        entry = "-";
    entry += key;
}

void Calculator::operation(char key)
{
    if (finished) {
        terms.clear();
        label.clear();
        pending = '+';
        finished = false;
    }
    if (!waiting && (terms.size() >= maxTerms || expression().size() > maxExpressionBytes))
        throw Error::Range;
    if (!waiting)
        terms.push_back({pending, current(), label.empty() ? display() : label});
    pending = key;
    replaceEntry = true;
    waiting = true;
}

void Calculator::backspace()
{
    if (finished || waiting || replaceEntry)
        return;
    entry.pop_back();
    if (entry.empty() || entry == "-")
        entry = "0";
}

void Calculator::clearEntry()
{
    if (finished) {
        clear();
        return;
    }
    setValue(0);
}

void Calculator::clear()
{
    terms.clear();
    entry = "0";
    label.clear();
    lastExpression.clear();
    pending = '+';
    replaceEntry = true;
    waiting = false;
    finished = false;
}

void Calculator::sign()
{
    if (finished)
        finished = false;
    if (waiting) {
        startEntry();
        entry = "-0";
        return;
    }
    if (replaceEntry && entry != "0") {
        setValue(-current());
        return;
    }
    replaceEntry = false;
    if (entry.front() == '-')
        entry.erase(0, 1);
    else
        entry.insert(0, "-");
}

Decimal Calculator::evaluate(bool includeEntry) const
{
    Decimal sum = 0;
    Decimal product = 0;
    const auto apply = [&](char operation, const Decimal &value) {
        switch (operation) {
        case '+': sum += product; product = value; break;
        case '-': sum += product; product = -value; break;
        case '*': product *= value; break;
        case '/':
            if (value == 0)
                throw Error::DivisionByZero;
            product /= value;
            break;
        }
    };
    const size_t count = terms.size() + includeEntry;
    const auto operatorAt = [&](size_t index) {
        return index < terms.size() ? terms[index].operation : pending;
    };
    const auto valueAt = [&](size_t index) {
        return index < terms.size() ? terms[index].value : current();
    };
    for (size_t first = 0; first < count;) {
        size_t last = first;
        while (last + 1 < count && operatorAt(last + 1) == '^')
            ++last;
        Decimal value = valueAt(last);
        for (size_t index = last; index > first; --index) {
            const auto base = valueAt(index - 1);
            if (base == 0 && value <= 0)
                throw value == 0 ? Error::Domain : Error::DivisionByZero;
            if (base < 0 && floor(value) != value)
                throw Error::Domain;
            const bool negative = base < 0 && abs(fmod(value, Decimal(2))) == 1;
            value = pow(abs(base), value);
            if (negative)
                value = -value;
            (void)numberText(value);
        }
        apply(operatorAt(first), value);
        first = last + 1;
    }
    return sum + product;
}

void Calculator::unary(char key)
{
    if (waiting)
        startEntry();
    finished = false;
    const auto value = current();
    const auto operand = label.empty() ? display() : label;
    switch (key) {
    case '%': {
        Decimal result = value / 100;
        if (!terms.empty() && (pending == '+' || pending == '-'))
            result *= evaluate(false);
        setValue(result, operand + "%");
        break;
    }
    case 'r':
        if (value < 0)
            throw Error::NegativeRoot;
        setValue(sqrt(value), "√(" + operand + ")");
        break;
    case 's':
        setValue(value * value, "(" + operand + ")²");
        break;
    case 'i':
        if (value == 0)
            throw Error::DivisionByZero;
        setValue(1 / value, "1/(" + operand + ")");
        break;
    }
}

void Calculator::scientific(char key, bool radians)
{
    if (waiting)
        startEntry();
    finished = false;

    const Decimal value = current();
    const auto operand = label.empty() ? display() : label;
    const auto &pi = boost::math::constants::pi<Decimal>();
    if (key == 'l' || key == 'n') {
        if (value <= 0)
            throw Error::Domain;
        if (key == 'l')
            setValue(log10(value), "log(" + operand + ")");
        else
            setValue(log(value), "ln(" + operand + ")");
        return;
    }

    if (radians && abs(value) >= Decimal("1e30"))
        throw Error::Range;
    const Decimal degrees = radians ? Decimal(0) : fmod(value, Decimal(360));
    const Decimal angle = radians ? value : Decimal(degrees * pi / 180);
    const std::string argument = operand + (radians ? " rad)" : "°)");
    switch (key) {
    case 'a':
        setValue(!radians && fmod(degrees, Decimal(180)) == 0 ? Decimal(0) : Decimal(sin(angle)),
                 "sin(" + argument);
        break;
    case 'b':
        setValue(!radians && abs(fmod(degrees, Decimal(180))) == 90 ? Decimal(0) : Decimal(cos(angle)),
                 "cos(" + argument);
        break;
    case 't': {
        if ((!radians && abs(fmod(degrees, Decimal(180))) == 90)
            || (radians && abs(cos(angle)) < Decimal("1e-33")))
            throw Error::Domain;
        setValue(!radians && fmod(degrees, Decimal(180)) == 0 ? Decimal(0) : Decimal(tan(angle)),
                 "tan(" + argument);
        break;
    }
    }
}

void Calculator::constant(char key)
{
    if (finished)
        clear();
    if (key == 'p')
        setValue(boost::math::constants::pi<Decimal>(), "π");
    else
        setValue(boost::math::constants::e<Decimal>(), "e");
}

void Calculator::memory(Memory key)
{
    switch (key) {
    case Memory::Clear: memoryValue = 0; memoryAvailable = false; return;
    case Memory::Recall:
        if (memoryAvailable) {
            finished = false;
            setValue(memoryValue);
        }
        return;
    case Memory::Store: memoryValue = current(); break;
    case Memory::Add: memoryValue += current(); break;
    case Memory::Subtract: memoryValue -= current(); break;
    }
    memoryAvailable = true;
}

bool Calculator::equals()
{
    if (finished || (terms.empty() && label.empty()))
        return false;
    waiting = false;
    auto description = expression();
    if (description.size() > maxExpressionBytes)
        throw Error::Range;
    const auto result = evaluate(true);
    setValue(result);
    terms.clear();
    pending = '+';
    finished = true;
    lastExpression = description + " =";
    return true;
}

std::string Calculator::snapshot() const
{
    std::ostringstream output;
    output << terms.size() << ' ' << pending << ' ' << waiting << ' '
           << std::quoted(entry) << ' ' << std::quoted(label);
    for (const auto &term : terms)
        output << ' ' << term.operation << ' ' << term.value.str(0, std::ios_base::fmtflags(0))
               << ' ' << std::quoted(term.label);
    output << ' ' << replaceEntry;
    return output.str();
}

bool Calculator::restore(const std::string &snapshot)
{
    if (snapshot.size() > maxSnapshotBytes)
        return false;
    std::istringstream input(snapshot);
    Calculator restored;
    size_t count = 0;
    if (!(input >> count >> restored.pending >> restored.waiting
                >> std::quoted(restored.entry) >> std::quoted(restored.label))
        || count > maxTerms || !validDecimal(restored.entry) || std::string("+-*/^").find(restored.pending) == std::string::npos)
        return false;
    try {
        (void)numberText(restored.current());
        for (size_t index = 0; index < count; ++index) {
            Term term;
            std::string value;
            if (!(input >> term.operation >> value >> std::quoted(term.label))
                || !validDecimal(value)
                || (index == 0 && term.operation != '+')
                || std::string("+-*/^").find(term.operation) == std::string::npos)
                return false;
            term.value = Decimal(value);
            (void)numberText(term.value);
            restored.terms.push_back(std::move(term));
        }
    } catch (const std::exception &) {
        return false;
    } catch (Error) {
        return false;
    }
    restored.replaceEntry = restored.waiting || !restored.label.empty()
        || restored.entry.find('e') != std::string::npos;
    input >> std::ws;
    if (!input.eof()) {
        bool savedReplaceEntry = false;
        if (!(input >> savedReplaceEntry))
            return false;
        restored.replaceEntry = savedReplaceEntry;
        input >> std::ws;
        if (!input.eof())
            return false;
    }
    if (restored.entry.front() == '+')
        restored.entry.erase(0, 1);
    if (restored.waiting || restored.entry.size() > 34
        || restored.entry.find_first_of("eE") != std::string::npos || !restored.label.empty())
        restored.replaceEntry = true;
    if (restored.expression().size() > maxExpressionBytes)
        return false;
    restored.memoryValue = memoryValue;
    restored.memoryAvailable = memoryAvailable;
    *this = std::move(restored);
    return true;
}

std::string Calculator::display() const
{
    return grouped(replaceEntry ? numberText(current()) : entry);
}

std::string Calculator::expression() const
{
    if (finished)
        return lastExpression;
    if (terms.empty())
        return label;
    const auto symbol = [](char operation) -> std::string {
        switch (operation) {
        case '*': return "×";
        case '^': return "^";
        case '/': return "÷";
        case '-': return "−";
        default: return "+";
        }
    };
    std::string result = terms.front().label;
    for (size_t index = 1; index < terms.size(); ++index)
        result += " " + symbol(terms[index].operation) + " " + terms[index].label;
    result += " " + symbol(pending);
    if (!waiting)
        result += " " + (label.empty() ? display() : label);
    return result;
}

}
