#include "compiler.h"

static bool isNum(const std::string &s) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        std::stod(s, &pos);
        return pos == s.size(); // reject partial parses like "1.2.3" or "3abc"
    } catch (...) {
        return false;
    }
}

static bool isOp(const std::string &s) {
    if (s == "+" || s == "-" || s == "*" || s == "/" || s == "^" || s == "%" || s == "..")
        return true;
    return false;
}

static bool isOp(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%';
    // '..' can't be represented as a single char, so its excluded here
}

static int getPrec(const std::string &op) {
    if (op == "..") return 1;
    if (op == "+" || op == "-") return 2;
    if (op == "*" || op == "/" || op == "%") return 3;
    if (op == "^") return 4;
    return 0;
}

static bool isRightAssoc(const std::string &op) {
    return op == "^";
}

static std::vector<std::string> tokenize(const std::string &expr) {
    std::vector<std::string> tokens;
    std::string buf;
    for (size_t i = 0; i < expr.size(); i++) {
        char ch = expr[i];

        // string literal: consume until matching closing quote
        if (ch == '\'' || ch == '"') {
            if (!buf.empty()) {
                tokens.push_back(buf);
                buf.clear();
            }
            char quote = ch;
            std::string literal;
            literal += quote; // keep opening quote
            size_t j = i + 1;
            for (; j < expr.size() && expr[j] != quote; j++) {
                literal += expr[j];
            }
            if (j < expr.size()) literal += expr[j]; // keep closing quote
            tokens.push_back(literal);
            i = j;
            continue;
        }

        // unary minus: at start, after another operator, or after '('
        if (ch == '-' && (i == 0 || isOp(expr[i - 1]) || expr[i - 1] == '(')) {
            buf += ch;
            continue;
        }

        // dereference 
        if (ch == '*' && buf.empty() &&
            i + 1 < expr.size() &&
            (std::isalpha(static_cast<unsigned char>(expr[i + 1])) ||
            expr[i + 1] == '_' || expr[i + 1] == '&')) {
            buf += ch;
            continue;
        }

        if ((ch == '+' || ch == '-') && !buf.empty() &&
            (buf.back() == 'e' || buf.back() == 'E') &&
            isNum(buf + "0")) { // buf+"0" e.g. "3e0" parses as a number => buf is a numeric prefix
            buf += ch;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' ||
            std::isalpha(static_cast<unsigned char>(ch)) || ch == '_' || ch == '&') {
            buf += ch;
        } else {
            if (!buf.empty()) {
                tokens.push_back(buf);
                buf.clear();
            }
            if (isOp(ch) || ch == '(' || ch == ')')
                tokens.push_back(std::string(1, ch));
        }
    }
    if (!buf.empty())
        tokens.push_back(buf);
    return tokens;
}

static bool isStringLit(const std::string &t) {
    return t.size() >= 2 && (t.front() == '\'' || t.front() == '"') &&
           t.back() == t.front();
}

static std::vector<std::string> shuntingYard(const std::vector<std::string> &tokens) {
    std::vector<std::string> out;
    std::vector<std::string> ops;

    for (const std::string &t : tokens) {
        if (isNum(t) || isVar(t) || isStringLit(t)) {
            out.push_back(t);
        } else if (t == "(") {
            ops.push_back(t);
        } else if (t == ")") {
            while (!ops.empty() && ops.back() != "(") {
                out.push_back(ops.back());
                ops.pop_back();
            }
            if (!ops.empty()) ops.pop_back(); // discard '('
        } else {
            const std::string &op = t;

            // type check
            if ((op == "+" || op == "-" || op == "*" || op == "/") &&
                !out.empty() && isStringLit(out.back())) {
                throw std::runtime_error("type error: arithmetic operator '" + op +
                                          "' cannot be applied to string literal " + out.back());
            }
            
            // type check
            if (op == ".." && !out.empty() && isNum(out.back())) {
                throw std::runtime_error("type error: '..' cannot be applied to numeric literal " + out.back());
            }
            
            while (!ops.empty() && ops.back() != "(" &&
                (getPrec(ops.back()) > getPrec(op) ||
                    (getPrec(ops.back()) == getPrec(op) && !isRightAssoc(op)))) {
                out.push_back(ops.back());
                ops.pop_back();
            }

            ops.push_back(op);
        }
    }
    while (!ops.empty()) {
        out.push_back(ops.back());
        ops.pop_back();
    }
    return out;
}

static void evalRPN(const std::vector<std::string> &rpn, CompilerData* data, std::vector<uint8_t>& bytecode) {
    std::vector<std::string> stack;

    for (const std::string &t : rpn) {
        if (isStringLit(t)) {
            std::string value = t.substr(1, t.size() - 2); // strip quotes
            int constIndex = resolveString(value, data);

            bytecode.push_back(0x03);
            bytecode.push_back(0x01); // TAG_STRING
            bytecode.push_back(constIndex);
            stack.push_back(t); // keep quotes so isStringLit still matches later
        } else if (isNum(t)) {
            bool isFloat = isFloatLiteral(t);
            TypeTag tag = isFloat ? TAG_FLOAT : TAG_INT;
            uint8_t dataType = isFloat ? 0x05 : 0x02;
            double constValue = std::stod(t);
            int constIndex = resolveConst(constValue, tag, data);

            bytecode.push_back(0x03);
            bytecode.push_back(dataType);
            bytecode.push_back(constIndex);
            stack.push_back(t);
        } else if (isVar(t)) {
            bool deref = t.starts_with("*");
            std::string rest = deref ? t.substr(1) : t;
            bool ref = rest.starts_with("&"); // resolveVariableIndex strips '&' itself

            int varIndex = resolveVariableIndex(rest, data);
            bytecode.push_back(0x03);
            bytecode.push_back(ref ? 0x04 : 0x03);
            bytecode.push_back(static_cast<uint8_t>(varIndex));

            if (deref) {
                bytecode.push_back(0xDE); // dereference: pointer on stack -> value on stack
            }

            stack.push_back(t);
        } else {
            if (stack.size() < 2) { return; }
            std::string b = stack.back(); stack.pop_back();
            std::string a = stack.back(); stack.pop_back();

            bool aIsStr = isStringLit(a);
            bool bIsStr = isStringLit(b);

            if (t == "..") {
                // concat: literal operands must not be numeric
                if ((isNum(a) && !aIsStr) || (isNum(b) && !bIsStr)) {
                    throw std::runtime_error("type error: '..' cannot be applied to a numeric literal");
                }

                // push count
                bytecode.push_back(0x03);
                bytecode.push_back(0x04);
                bytecode.push_back(2); // string count for JOIN

                bytecode.push_back(0xAA); // JOIN
            } else {
                // arithmetic: literal operands must not be strings
                if (aIsStr || bIsStr) {
                    throw std::runtime_error("type error: arithmetic operator '" + t +
                                              "' cannot be applied to a string literal");
                }
                switch (t[0]) {
                    case '+': bytecode.push_back(0xA0); break; // ADD
                    case '-': bytecode.push_back(0xA1); break; // SUB
                    case '*': bytecode.push_back(0xA2); break; // MUL
                    case '/': bytecode.push_back(0xA3); break; // DIV
                    case '^': bytecode.push_back(0xA4); break; // POW
                    case '%': bytecode.push_back(0xA5); break; // MOD
                }
            }

            stack.push_back(t); // placeholder for result (see caveat below)
        }
    }
}

void compileExpression(std::string expr, CompilerData* data, std::vector<uint8_t>& bytecode) {
    auto tokens = tokenize(expr);
    auto rpn    = shuntingYard(tokens);
    return evalRPN(rpn, data, bytecode);
}