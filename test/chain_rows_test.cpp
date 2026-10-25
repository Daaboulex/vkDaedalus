#include "chain_rows.hpp"

#include <cstdio>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

using Call = std::tuple<size_t, int, int>;

static constexpr int target = -1;

struct Run
{
    std::vector<Call>                         calls;
    vkBasalt::BuiltRows<std::shared_ptr<int>> built;
};

static Run wire(size_t firstRow, size_t rows, const std::set<Call>& refused)
{
    static const std::vector<std::string> names = {"a", "b", "c", "d", "e"};
    Run        run;
    const auto slotOf = [](size_t s) { return static_cast<int>(s); };
    const auto build  = [&](size_t row, int from, int to) {
        run.calls.emplace_back(row, from, to);
        return refused.count(Call(row, from, to)) ? nullptr : std::make_shared<int>(static_cast<int>(row));
    };
    run.built = vkBasalt::buildRowsSkippingDropped<std::shared_ptr<int>>(names, firstRow, rows, slotOf, target, build);
    return run;
}

int main()
{
    {
        const Run run = wire(0, 3, {});
        expect(run.calls == std::vector<Call>{{0, 0, 1}, {1, 1, 2}, {2, 2, target}}, "every row built reads the slot before it and the last writes the target");
        expect(run.built.rows == std::vector<size_t>{0, 1, 2} && run.built.dropped.empty(), "nothing dropped when every row builds");
    }
    {
        const Run run = wire(0, 3, {{1, 1, 2}});
        expect(run.calls == std::vector<Call>{{0, 0, 1}, {1, 1, 2}, {2, 1, target}}, "the row after a dropped one reads the slot the dropped one would have read");
        expect(run.built.rows == std::vector<size_t>{0, 2} && run.built.dropped == std::vector<std::string>{"b"}, "a dropped middle row takes no slot");
        expect(run.built.effects.size() == 2, "a dropped middle row leaves no effect in its place");
    }
    {
        const Run run = wire(0, 3, {{2, 2, target}});
        expect(run.calls == std::vector<Call>{{0, 0, 1}, {1, 1, 2}, {2, 2, target}, {1, 1, target}},
               "when the last row drops, the row before it is built again to write the target");
        expect(run.built.rows == std::vector<size_t>{0, 1} && run.built.effects.size() == 2, "a dropped last row leaves no copy in its place");
        expect(run.built.dropped == std::vector<std::string>{"c"}, "the dropped last row is named");
    }
    {
        const Run run = wire(0, 3, {{2, 2, target}, {1, 1, target}});
        expect(run.calls.back() == Call(0, 0, target), "when the rebuilt row fails as well, the one before it writes the target");
        expect(run.built.rows == std::vector<size_t>{0} && run.built.dropped == std::vector<std::string>{"c", "b"}, "a row that fails on its rebuild is dropped too");
    }
    {
        const Run run = wire(0, 3, {{0, 0, 1}, {1, 0, 1}, {2, 0, target}});
        expect(run.built.effects.empty() && run.built.rows.empty(), "a chain whose every row drops builds nothing");
        expect(run.built.dropped == std::vector<std::string>{"a", "b", "c"}, "every dropped row is named, in order");
        expect(run.calls.size() == 3, "no row is built again when nothing before it was built");
    }
    {
        const Run run = wire(3, 2, {{3, 0, 1}});
        expect(run.calls == std::vector<Call>{{3, 0, 1}, {4, 0, target}}, "a segment's rows are counted from its first row and read its own slots");
        expect(run.built.rows == std::vector<size_t>{4} && run.built.dropped == std::vector<std::string>{"d"}, "a segment names its dropped rows");
    }
    {
        const Run run = wire(0, 0, {});
        expect(run.calls.empty() && run.built.effects.empty(), "no rows, no builds");
    }
    if (failures == 0)
        std::printf("chain rows: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
