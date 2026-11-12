#include "chain_rows.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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
    {
        using vkBasalt::instanceOfRow;
        using vkBasalt::rowNameOf;
        using vkBasalt::techniqueOfRow;
        expect(instanceOfRow("Clarity") == "Clarity" && techniqueOfRow("Clarity").empty(), "a bare row is its instance's first technique");
        expect(instanceOfRow("UIMask/UIMask_Bottom") == "UIMask" && techniqueOfRow("UIMask/UIMask_Bottom") == "UIMask_Bottom",
               "a technique row names its instance and its technique");
        expect(instanceOfRow("Clarity.2/Sharpen") == "Clarity.2" && techniqueOfRow("Clarity.2/Sharpen") == "Sharpen",
               "a repeated instance keeps its number in a technique row");
        const std::vector<std::string> techniques = {"UIMask_Top", "UIMask_Bottom"};
        expect(rowNameOf("UIMask", "UIMask_Top", techniques) == "UIMask", "the first technique's row is written bare, as profiles from before name it");
        expect(rowNameOf("UIMask", "UIMask_Bottom", techniques) == "UIMask/UIMask_Bottom", "another technique's row names it");
        expect(rowNameOf("UIMask", "", techniques) == "UIMask", "no technique named is the first");
    }
    {
        using vkBasalt::TechniqueInfo;
        const std::vector<TechniqueInfo> mask     = {{.name = "UIMask_Top"}, {.name = "UIMask_Mid", .label = "Middle"}, {.name = "UIMask_Bottom"}};
        const auto                       maskOnly = [&](const std::string& instance) {
            return instance == "UIMask" || instance == "UIMask.2" ? std::vector<std::string>{"UIMask_Top", "UIMask_Mid", "UIMask_Bottom"} : std::vector<std::string>{};
        };
        using vkBasalt::rowsWithEveryTechnique;
        expect(rowsWithEveryTechnique({"cas", "UIMask", "smaa"}, maskOnly) == std::vector<std::string>{"cas", "UIMask", "UIMask/UIMask_Mid", "UIMask/UIMask_Bottom", "smaa"},
               "every technique of a loaded file gets a row after the file's row, in file order");
        expect(rowsWithEveryTechnique({"UIMask/UIMask_Bottom", "cas", "UIMask", "UIMask/UIMask_Mid"}, maskOnly)
                   == std::vector<std::string>{"UIMask/UIMask_Bottom", "cas", "UIMask", "UIMask/UIMask_Mid"},
               "rows a profile already has keep their order and none is added twice");
        expect(rowsWithEveryTechnique({"UIMask", "UIMask.2/UIMask_Mid"}, maskOnly)
                   == std::vector<std::string>{"UIMask", "UIMask/UIMask_Mid", "UIMask/UIMask_Bottom", "UIMask.2/UIMask_Mid", "UIMask.2", "UIMask.2/UIMask_Bottom"},
               "each instance of a file gets its own rows, the missing ones after that instance's last row in file order");
        expect(rowsWithEveryTechnique({"cas", "smaa"}, maskOnly) == std::vector<std::string>{"cas", "smaa"}, "a built-in adds no row");

        using vkBasalt::techniqueOfRowIn;
        expect(techniqueOfRowIn("UIMask", mask) == &mask[0], "a bare row runs the first technique");
        expect(techniqueOfRowIn("UIMask/UIMask_Bottom", mask) == &mask[2], "a technique row runs the technique it names");
        expect(techniqueOfRowIn("UIMask/Gone", mask) == nullptr && techniqueOfRowIn("cas", {}) == nullptr, "a technique the file lacks, or a file with none, gives none");

        using vkBasalt::rowLabelOf;
        expect(rowLabelOf("UIMask/UIMask_Mid", mask) == "UIMask: Middle", "a row is labelled with its technique's ui_label");
        expect(rowLabelOf("UIMask/UIMask_Bottom", mask) == "UIMask: UIMask_Bottom", "a technique with no ui_label is labelled by its name");
        expect(rowLabelOf("Clarity", {{.name = "Clarity", .label = "Sharpen"}}) == "Clarity", "a file with one technique keeps its row name");

        using vkBasalt::drawnNeighbour;
        using vkBasalt::rowsDrawn;
        std::vector<TechniqueInfo> withHidden = mask;
        withHidden[1].hidden                  = true;
        expect(rowsDrawn({"cas", "UIMask", "UIMask/UIMask_Mid", "UIMask/UIMask_Bottom"}, {{}, withHidden, withHidden, withHidden})
                   == std::vector<bool>{true, true, false, true},
               "a hidden technique takes no row and the others of its file still do");
        std::vector<TechniqueInfo> allHidden = mask;
        for (TechniqueInfo& technique : allHidden)
            technique.hidden = true;
        expect(rowsDrawn({"UIMask/UIMask_Mid", "UIMask", "cas"}, {allHidden, allHidden, {}}) == std::vector<bool>{true, false, true},
               "a file whose every technique is hidden keeps its first row drawn, so it can be removed");
        bool refused = false;
        try
        {
            static_cast<void>(rowsDrawn({"cas", "smaa"}, {{}}));
        }
        catch (const std::invalid_argument&)
        {
            refused = true;
        }
        expect(refused, "rows without their techniques are refused");
        const std::vector<bool> drawn = {true, false, true, true};
        expect(drawnNeighbour(drawn, 2, false) == std::optional<size_t>{0}, "Up moves past a hidden row to the drawn row above");
        expect(drawnNeighbour(drawn, 0, true) == std::optional<size_t>{2}, "Down moves past a hidden row to the drawn row below");
        expect(!drawnNeighbour(drawn, 0, false) && !drawnNeighbour(drawn, 3, true), "the first and last drawn rows have nothing past them");
    }
    if (failures == 0)
        std::printf("chain rows: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
