/*
 * preprocessor library by daniel marjamäki
 */

#include "preprocessor.h"

#include <iostream>
#include <cctype>
#include <list>
#include <map>
#include <set>
#include <vector>

const TokenString DEFINE("define");
const TokenString IFDEF("ifdef");
const TokenString IFNDEF("ifndef");
const TokenString ELSE("else");
const TokenString ENDIF("endif");

TokenList::TokenList() : first(nullptr), last(nullptr) {}

TokenList::TokenList(const TokenList &other) {
    *this = other;
}

TokenList::~TokenList() {
    clear();
}

void TokenList::operator=(const TokenList &other) {
    if (this == &other)
        return;
    clear();
    for (const Token *tok = other.cbegin(); tok; tok = tok->next)
        push_back(new Token(tok->str, tok->location));
}

void TokenList::clear() {
    while (first) {
        Token *next = first->next;
        delete first;
        first = next;
    }
    last = nullptr;
}

void TokenList::push_back(Token *tok) {
    if (!first)
        first = tok;
    else
        last->next = tok;
    tok->previous = last;
    last = tok;
}

class Macro {
public:
    Macro() : nameToken(nullptr) {}

    explicit Macro(const Token *tok) : nameToken(nullptr) {
        if (tok->previous && tok->previous->location.line == tok->location.line)
            throw 1;
        if (tok->op != '#')
            throw 1;
        tok = tok->next;
        if (!tok || tok->str != DEFINE)
            throw 1;
        tok = tok->next;
        if (!tok || !tok->name)
            throw 1;
        parsedef(tok);
    }

    Macro(const Macro &macro) {
        *this = macro;
    }

    void operator=(const Macro &macro) {
        if (this != &macro)
            parsedef(macro.nameToken);
    }

    const Token * expand(TokenList * const output, const Location &loc, const Token *tok, const std::map<TokenString,Macro> &macros, std::set<TokenString> expandedmacros) const {
        expandedmacros.insert(nameToken->str);
        if (args.empty()) {
            for (const Token *macro = valueToken; macro != endToken;) {
                const std::map<TokenString, Macro>::const_iterator it = macros.find(macro->str);
                if (it != macros.end() && expandedmacros.find(macro->str) == expandedmacros.end()) {
                    macro = it->second.expand(output, loc, macro, macros, expandedmacros);
                } else {
                    output->push_back(new Token(macro->str, loc));
                    macro = macro->next;
                }
            }
            return tok->next;
        }

        if (!tok->next || tok->next->op != '(') {
            std::cerr << "error: macro call" << std::endl;
            return tok->next;
        }

        // Parse macro-call
        std::vector<const Token*> parametertokens;
        parametertokens.push_back(tok->next);
        unsigned int par = 0U;
        for (const Token *calltok = tok->next->next; calltok; calltok = calltok->next) {
            if (calltok->op == '(')
                ++par;
            else if (calltok->op == ')') {
                if (par == 0U) {
                    parametertokens.push_back(calltok);
                    break;
                }
                --par;
            }
            else if (par == 0U && calltok->op == ',')
                parametertokens.push_back(calltok);
        }

        if (parametertokens.size() != args.size() + 1U) {
            std::cerr << "error: macro call" << std::endl;
            return tok->next;
        }

        // expand
        for (const Token *macro = valueToken; macro != endToken;) {
            if (macro->name) {
                // Handling macro parameter..
                unsigned int par = 0;
                while (par < args.size()) {
                    if (macro->str == args[par])
                        break;
                    par++;
                }
                if (par < args.size()) {
                    for (const Token *partok = parametertokens[par]->next; partok != parametertokens[par+1]; partok = partok->next)
                        output->push_back(new Token(partok->str, loc));
                    macro = macro->next;
                    continue;
                }

                // Macro..
                const std::map<TokenString, Macro>::const_iterator it = macros.find(macro->str);
                if (it != macros.end() && expandedmacros.find(macro->str) == expandedmacros.end()) {
                    macro = it->second.expand(output, loc, macro, macros, expandedmacros);
                    continue;
                }
            }
            output->push_back(new Token(macro->str, loc));
            macro = macro->next;
        }

        return parametertokens[args.size()]->next;
    }

    TokenString name() const {
        return nameToken->str;
    }

private:
    void parsedef(const Token *nametoken) {
        nameToken = nametoken;
        if (!nameToken) {
            valueToken = endToken = nullptr;
            args.clear();
            return;
        }

        // function like macro..
        if (nameToken->next &&
                nameToken->next->op == '(' &&
                nameToken->location.line == nameToken->next->location.line &&
                nameToken->next->location.col == nameToken->location.col + nameToken->str.size()) {
            args.clear();
            const Token *argtok = nameToken->next->next;
            while (argtok && argtok->op != ')') {
                if (argtok->op != ',')
                    args.push_back(argtok->str);
                argtok = argtok->next;
            }
            valueToken = argtok->next;
        } else {
            args.clear();
            valueToken = nameToken->next;
        }

        if (valueToken && valueToken->location.line != nameToken->location.line)
            valueToken = nullptr;
        endToken = valueToken;
        while (endToken && endToken->location.line == nameToken->location.line)
            endToken = endToken->next;
    }

    const Token *nameToken;
    std::vector<TokenString> args;
    const Token *valueToken;
    const Token *endToken;
};

static bool sameline(const Token *tok1, const Token *tok2) {
    return (tok1 && tok2 && tok1->location.line == tok2->location.line);
}

TokenList readfile(std::istream &istr)
{
    TokenList tokens;
    Location location;
    location.file = 0U;
    location.line = 1U;
    location.col  = 0U;
    while (istr.good()) {
        unsigned char ch = (unsigned char)istr.get();
        if (!istr.good())
            break;
        location.col = (ch == '\t') ? ((location.col + 8) & (~7)) : (location.col + 1);

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && istr.peek() == '\n')
                istr.get();
            ++location.line;
            location.col = 0;
            continue;
        }

        if (std::isspace(ch))
            continue;

        TokenString currentToken;

        // number or name
        if (std::isalnum(ch) || ch == '_') {
            while (istr.good() && (std::isalnum(ch) || ch == '_')) {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // comment
        else if (ch == '/' && istr.peek() == '/') {
            while (istr.good() && ch != '\r' && ch != '\n') {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // comment
        else if (ch == '/' && istr.peek() == '*') {
            while (istr.good() && !(currentToken.size() > 2U && ch == '*' && istr.peek() == '/')) {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // string / char literal
        else if (ch == '\"' || ch == '\'') {
            do {
                currentToken += ch;
                ch = (unsigned char)istr.get();
                if (istr.good() && ch == '\\') {
                    currentToken += ch;
                    ch = (unsigned char)istr.get();
                    currentToken += ch;
                    ch = (unsigned char)istr.get();
                }
            } while (istr.good() && ch != '\"' && ch != '\'');
            currentToken += ch;
        }

        else {
            currentToken += ch;
        }

        tokens.push_back(new Token(currentToken, location));
        location.col += currentToken.size() - 1U;
    }

    return tokens;
}

const Token *skipcode(const Token *rawtok) {
    int state = 0;
    while (rawtok) {
        if (rawtok->op == '#')
            state = 1;
        else if (state == 1 && (rawtok->str == ELSE || rawtok->str == ENDIF))
            return rawtok->previous;
        else
            state = 0;
        rawtok = rawtok->next;
    }
    return nullptr;
}

TokenList preprocess(const TokenList &rawtokens)
{
    TokenList output;
    std::map<TokenString, Macro> macros;
    for (const Token *rawtok = rawtokens.cbegin(); rawtok;) {
        if (rawtok->op == '#' && !sameline(rawtok->previous, rawtok)) {
            if (rawtok->next->str == DEFINE) {
                try {
                    const Macro &macro = Macro(rawtok);
                    macros[macro.name()] = macro;
                    const unsigned int line = rawtok->location.line;
                    while (rawtok && rawtok->location.line == line)
                        rawtok = rawtok->next;
                    continue;
                } catch (...) {
                }
            } else if (rawtok->next->str == IFDEF) {
                if (macros.find(rawtok->next->next->str) != macros.end()) {
                    rawtok = rawtok->next->next->next;
                } else {
                    rawtok = skipcode(rawtok);
                    if (rawtok)
                        rawtok = rawtok->next;
                    if (rawtok)
                        rawtok = rawtok->next;
                    if (!rawtok)
                        break;
                }
            } else if (rawtok->next->str == ELSE) {
                rawtok = skipcode(rawtok->next);
                if (rawtok)
                    rawtok = rawtok->next;
                if (rawtok)
                    rawtok = rawtok->next;
                if (!rawtok)
                    break;
            } else if (rawtok->next->str == ENDIF) {
                if (rawtok)
                    rawtok = rawtok->next;
                if (rawtok)
                    rawtok = rawtok->next;
                if (!rawtok)
                    break;
            }
        }

        if (macros.find(rawtok->str) != macros.end()) {
            std::map<TokenString,Macro>::const_iterator macro = macros.find(rawtok->str);
            if (macro != macros.end()) {
                std::set<TokenString> expandedmacros;
                rawtok = macro->second.expand(&output,rawtok->location,rawtok,macros,expandedmacros);
                continue;
            }
        }

        output.push_back(new Token(*rawtok));
        rawtok = rawtok->next;
    }
    return output;
}
