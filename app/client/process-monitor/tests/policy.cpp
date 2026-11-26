#include "../src/process_policy.h"
#include "policy_snapshot.h"
#include "windows_handle.h"
#include <cassert>

static void test_policy_delivery() {
    bb::Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                          sizeof(bb::PolicySnapshot), nullptr));
    bb::PolicyView<bb::PolicySnapshot> writer;
    bb::PolicyView<const bb::PolicySnapshot> reader;
    assert(mapping && writer.open(mapping.get(), FILE_MAP_WRITE) &&
           reader.open(mapping.get(), FILE_MAP_READ));
    std::string bytes;
    std::uint32_t revision = 0;
    std::vector<process_policy::Rule> rules;
    assert(!reader.get()->read(revision, bytes));
    auto receive = [&](std::string_view text) {
        assert(writer.get()->publish(text));
        assert(reader.get()->read(revision, bytes) && bytes == text);
        assert(!reader.get()->read(revision, bytes));
        return process_policy::apply(bytes, rules);
    };
    assert(receive("game*.exe\n!game editor.exe"));
    assert(process_policy::blocked(rules, L"game.exe"));
    assert(!process_policy::blocked(rules, L"game editor.exe"));
    assert(!receive("other.exe\n*.exe"));
    assert(!receive(std::string_view("\xff", 1)));
    assert(process_policy::blocked(rules, L"game.exe"));
    assert(!process_policy::blocked(rules, L"other.exe"));
    assert(!writer.get()->publish(std::string(bb::PolicySnapshot::capacity + 1, 'x')));
    assert(!reader.get()->read(revision, bytes));
    assert(receive("other.exe"));
    assert(process_policy::blocked(rules, L"Other.exe"));
    assert(!process_policy::blocked(rules, L"game.exe"));
    assert(receive("") && rules.empty());
}

int main() {
    test_policy_delivery();
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
