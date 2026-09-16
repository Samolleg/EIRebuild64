#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>

// Text lowering only. Executed commands and the program counter still belong
// to the engine, including suspension, local variables and serialization.
namespace MobIfElse {
struct Token { std::string text; int line; };
using Tokens = std::vector<Token>;
using Commands = std::vector<Tokens>;
struct Result { std::string text, error; int line = 1; bool changed = false; };

class Parser {
    Tokens tokens;
    size_t at = 0;
    int depth = 0;
    bool changed = false;
    static std::string Lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    bool Is(const char* s) const { return at < tokens.size() && Lower(tokens[at].text) == s; }
    Token Take() {
        if (at == tokens.size()) throw std::runtime_error("Unexpected end of if/else source");
        return tokens[at++];
    }
    Token Need(const char* s) {
        if (!Is(s)) throw std::runtime_error(std::string("Expected '") + s + "'");
        return Take();
    }
    static void Append(Tokens& a, const Tokens& b) { a.insert(a.end(), b.begin(), b.end()); }
    Tokens Balanced() {
        Tokens out{Need("(")}; int n = 1;
        while (n) {
            auto t = Take();
            if (t.text == "(") ++n;
            if (t.text == ")") --n;
            out.push_back(t);
        }
        return out;
    }
    Tokens Expression() {
        Tokens out{Take()};
        if (Is("(")) Append(out, Balanced());
        return out;
    }
    static Tokens Flatten(const Commands& cs) {
        Tokens out; for (const auto& c : cs) Append(out, c); return out;
    }
    Commands Branch(const Commands& conditions, const Commands& yes,
                    const Commands& no, int line, int endYes) {
        Commands out;
        const size_t tail = yes.size() + (no.empty() ? 0 : 1);
        for (size_t i = 0; i < conditions.size(); ++i) {
            Tokens cmd{{"JmvBranchUnless", line}, {"(", line}};
            Append(cmd, conditions[i]);
            const int last = cmd.back().line;
            cmd.push_back({",", last});
            cmd.push_back({std::to_string(conditions.size()-i-1+tail), last});
            cmd.push_back({")", last}); out.push_back(cmd);
        }
        out.insert(out.end(), yes.begin(), yes.end());
        if (!no.empty()) out.push_back({{"JmvBranchSkip",endYes},{"(",endYes},
                                       {std::to_string(no.size()),endYes},{")",endYes}});
        out.insert(out.end(), no.begin(), no.end());
        return out;
    }
    Commands Condition() {
        Need("("); Commands cs;
        while (!Is(")")) cs.push_back(Expression());
        Need(")"); return cs;
    }
    Commands Actions() {
        if (++depth > 64) throw std::runtime_error("if/else nesting exceeds 64");
        Need("("); Commands out;
        while (!Is(")")) {
            if (Is("if")) {
                const int line = Take().line;
                auto cond = Condition(); Need("then"); auto yes = Actions();
                const int endYes = tokens[at-1].line;
                Commands no;
                if (Is("else")) { Take(); no = Actions(); }
                auto branch = Branch(cond,yes,no,line,endYes);
                out.insert(out.end(),branch.begin(),branch.end()); changed = true;
            } else {
                if (Is("else")) throw std::runtime_error("else has no matching if");
                Tokens command{Take()};
                if (Is("=")) { command.push_back(Take()); Append(command,Expression()); }
                else Append(command,Balanced());
                if (Is("(")) {
                    auto open = tokens[at]; auto body = Actions();
                    command.push_back(open); Append(command,Flatten(body));
                    command.push_back(tokens[at-1]);
                }
                out.push_back(command);
            }
        }
        Need(")"); --depth; return out;
    }
    Tokens Script() {
        Tokens out{Need("(")};
        while (!Is(")")) {
            const size_t begin = at;
            auto head = Need("if"); auto cond = Condition(); Need("then");
            auto open = tokens[at]; auto yes = Actions(); auto close = tokens[at-1];
            if (Is("else")) {
                Take(); auto no = Actions(); auto last = tokens[at-1];
                // Unconditional native rule: Script lifetime still uses KillScript.
                out.insert(out.end(), {{"if",head.line},{"(",head.line},{")",head.line},
                                      {"then",head.line},{"(",head.line}});
                Append(out,Flatten(Branch(cond,yes,no,head.line,close.line)));
                out.push_back(last); changed = true;
            } else {
                // Keep the original waiting condition; lower only its action list.
                size_t prefix = begin;
                while (prefix < at && !(tokens[prefix].text == "(" && prefix > begin &&
                       Lower(tokens[prefix-1].text) == "then")) ++prefix;
                out.insert(out.end(),tokens.begin()+begin,tokens.begin()+prefix);
                out.push_back(open); Append(out,Flatten(yes)); out.push_back(close);
            }
        }
        out.push_back(Need(")")); return out;
    }
public:
    Result Run(const std::string& source) {
        Result result; result.text = source;
        int line = 1;
        try {
            for (size_t i=0; i<source.size();) {
                const unsigned char c = static_cast<unsigned char>(source[i]);
                if (std::isspace(c)) { if (c=='\n') ++line; ++i; continue; }
                if (source.compare(i,2,"//")==0) { while(i<source.size()&&source[i]!='\n')++i; continue; }
                if (source.compare(i,2,"/*")==0) {
                    i+=2;
                    while(i<source.size()&&source.compare(i,2,"*/")!=0) {if(source[i++]=='\n')++line;}
                    if(i==source.size()) throw std::runtime_error("Unclosed comment");
                    i+=2; continue;
                }
                size_t start=i; int tokenLine=line;
                if (c=='"') {
                    ++i; while(i<source.size()&&source[i]!='"') {if(source[i++]=='\n')++line;}
                    if(i==source.size()) throw std::runtime_error("Unclosed string");
                    ++i;
                } else if(c=='('||c==')'||c==','||c==':'||c=='=') ++i;
                else {
                    while(i<source.size()&&!std::isspace(static_cast<unsigned char>(source[i]))&&
                          source[i]!='('&&source[i]!=')'&&source[i]!=','&&source[i]!=':'&&source[i]!='='&&
                          source.compare(i,2,"//")!=0&&source.compare(i,2,"/*")!=0) ++i;
                }
                tokens.push_back({source.substr(start,i-start),tokenLine});
            }
            Tokens out;
            while(at<tokens.size()) {
                auto word=Take(); out.push_back(word);
                const auto kind=Lower(word.text);
                if(kind=="script") {out.push_back(Take());Append(out,Script());}
                else if(kind=="worldscript") {
                    auto open=tokens.at(at);auto cs=Actions();out.push_back(open);
                    Append(out,Flatten(cs));out.push_back(tokens[at-1]);
                } else if(kind=="declarescript") {out.push_back(Take());Append(out,Balanced());}
                else if(kind=="globalvars") Append(out,Balanced());
                else throw std::runtime_error("Unsupported source section in if/else lowering");
            }
            if(changed) {
                result.text.clear(); int currentLine=1;
                for(const auto& t:out) {
                    while(currentLine<t.line) {result.text+='\n';++currentLine;}
                    result.text+=t.text;result.text+=' ';
                    for(char c:t.text) if(c=='\n')++currentLine;
                }
                result.changed=true;
            }
        } catch(const std::exception& e) {
            // Old sources are always left to the retail parser. A new source
            // error is returned only when an actual structural keyword occurs.
            bool extension=changed;
            for(const auto& t:tokens) if(Lower(t.text)=="else") extension=true;
            if(extension) {result.error=e.what();result.line=at<tokens.size()?tokens[at].line:line;}
        }
        return result;
    }
};
inline Result Lower(const std::string& source) {return Parser().Run(source);}
} // namespace MobIfElse
