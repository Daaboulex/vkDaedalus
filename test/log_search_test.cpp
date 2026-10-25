#include "overlay/log_search.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

int main()
{
    const std::string overlay = vkBasalt::loweredSearch("OverLay");
    expect(overlay == "overlay", "the typed search is lowered once");
    expect(vkBasalt::matchesSearch("the Overlay opened", overlay), "a line matches its words in another case");
    expect(!vkBasalt::matchesSearch("effects on", overlay), "a line without the words does not match");
    expect(vkBasalt::matchesSearch("anything", ""), "an empty search matches every line");
    expect(!vkBasalt::matchesSearch("", overlay), "an empty line matches no search");
    expect(vkBasalt::matchesSearch("ends in OVERLAY", overlay), "the words may end the line");
    expect(!vkBasalt::matchesSearch("over lay", overlay), "the words must sit together");
    expect(vkBasalt::matchesSearch("\xc3\xa9\xc3\xa8", vkBasalt::loweredSearch("\xc3\xa8")), "bytes outside ASCII match as they are");
    return failures == 0 ? 0 : 1;
}
