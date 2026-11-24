#include "../src/process_policy.h"
#include <cassert>

int main() {
    using namespace process_policy;
    std::vector<Rule> rules;
    for (const auto* line :
         {L"[3]game*.exe", L"[3]!game editor.exe", L"[4]game runner.exe", L"[3]!game runner.exe"}) {
        Rule rule;
        assert(parse(line, rule));
        rules.push_back(rule);
    }
    for (int pass = 0; pass < 2; ++pass) {
        assert(blocked(rules, L"Game.exe"));
        assert(blocked(rules, L"GAME CLIENT.EXE"));
        assert(blocked(rules, L"game runner.exe"));
        assert(!blocked(rules, L"game editor.exe"));
        assert(!blocked(rules, L"my game.exe"));
        assert(!blocked(rules, L"game.dll"));
        std::reverse(rules.begin(), rules.end());
    }
    Rule rule;
    for (auto* bad : {L"*.exe", L"", L"[2147483648]a.exe", L"[-1]a.exe", L"a/b.exe", L"game*more.exe",
                      L"foo.exe --run"}) {
        assert(!parse(bad, rule));
    }
}
