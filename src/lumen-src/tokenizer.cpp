#include "tokenizer.h"
#include <algorithm>
#include <cctype>

std::string trim_tabs(const std::string& str) {
    const std::string targets = "\t";
    size_t start = str.find_first_not_of(targets);
    if (start == std::string::npos) return "";

    size_t end = str.find_last_not_of(targets);
    return str.substr(start, end - start + 1);
}

std::vector<std::string> tokenizeFormula(std::string formula) {
    std::vector<std::string> tokens;
    std::string token = "";
    bool isQuoteOpen = false;
    trim_tabs(formula);
    for(int i = 0; i < formula.size(); i++) {
        char c = formula[i];
        if (c == '(' || c == ')') {
            if(!isQuoteOpen) {
                if (token.length() > 0) tokens.push_back(token);
                tokens.push_back(std::string(1, c));
                token = "";
                continue;
            }
        } else if (c == ',') {
            if(!isQuoteOpen) {
                if (token.length() > 0) tokens.push_back(token);
                tokens.push_back(",");
                token = "";
                continue;
            }
        } else if (c == '.' && !isQuoteOpen) {
            // '.' inside a numeric literal being built (e.g. "3" + "." -> "3.14") stays glued
            bool numericContext = !token.empty() &&
                std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch); });
            if (numericContext) {
                token += c;
                continue;
            }
            // otherwise '.' starts/continues a concat operator '..'
            if (i + 1 < formula.size() && formula[i + 1] == '.') {
                if (token.length() > 0) tokens.push_back(token);
                tokens.push_back(" .. ");
                token = "";
                i++; // consume both dots
                continue;
            }
            // lone '.' with no numeric context and no second dot: treat as ordinary char
            token += c;
            continue;
        } else if (c == '*' && token.empty() && !isQuoteOpen &&
                   (tokens.empty() || tokens.back() == "(" || tokens.back() == "," ||
                    tokens.back() == "+" || tokens.back() == "-" || tokens.back() == "*" ||
                    tokens.back() == "/" || tokens.back() == "%" || tokens.back() == " .. ")) {
            // dereference prefix: '*' at start of an operand position, not after a value
            token += c;
            continue;
        } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
            if(!isQuoteOpen) {
                if (token.length() > 0) tokens.push_back(token);
                tokens.push_back(std::string(1, c));
                token = "";
                continue;
            }
        } else if (c == ' ') {
            if(!isQuoteOpen) {
                if (token.length() > 0) tokens.push_back(token);
                token = "";
                continue;
            }
        } else if(c == '\'') {
            isQuoteOpen = !isQuoteOpen;
            if(!isQuoteOpen) {
                if (token.length() > 0) tokens.push_back(token + '\'');
                token = "";
                continue;
            }
        } else if(c == '#') {
            // comment
            break;
        } else if(c == '\\') {
            if(isQuoteOpen) {
                char next = formula[i+1];
                switch (next) {
                    case 'n': token += '\n'; break;
                    case 't': token += '\t'; break;
                    case '\\': token += '\\'; break;
                    case '\'': token += '\''; break;
                    default: token += next; // or error
                }
                i++;
                continue;
            }
        }
        token += c;
    }
    if (token.length() != 0) {
        tokens.push_back(token);
    }
    return tokens;
}