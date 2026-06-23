#include "LevelBuilderPalette.h"

#include <algorithm>
#include <cctype>

static bool ContainsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b)
        {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    return it != haystack.end();
}

LevelBuilderPalette::LevelBuilderPalette(const std::string& name)
    : mName(name)
{
}

void LevelBuilderPalette::AddItem(const LevelBuilderPaletteItem& item)
{
    StoredItem s;
    s.displayName = item.displayName ? item.displayName : "";
    s.assetName   = item.assetName   ? item.assetName   : "";
    s.category    = item.category    ? item.category    : "";
    s.iconPath    = item.iconPath    ? item.iconPath    : "";
    s.tags        = item.tags        ? item.tags        : "";

    if (!s.category.empty())
    {
        auto it = std::find(mCategories.begin(), mCategories.end(), s.category);
        if (it == mCategories.end())
            mCategories.push_back(s.category);
    }

    mItems.push_back(std::move(s));
}

void LevelBuilderPalette::Clear()
{
    mItems.clear();
    mCategories.clear();
    mActiveIndex = -1;
}

bool LevelBuilderPalette::GetItem(int index, LevelBuilderPaletteItem* outItem) const
{
    if (!outItem) return false;
    if (index < 0 || index >= (int)mItems.size()) return false;
    const StoredItem& s = mItems[index];
    outItem->displayName = s.displayName.c_str();
    outItem->assetName   = s.assetName.c_str();
    outItem->category    = s.category.c_str();
    outItem->iconPath    = s.iconPath.c_str();
    outItem->tags        = s.tags.c_str();
    return true;
}

void LevelBuilderPalette::SetActiveIndex(int index)
{
    if (index < -1 || index >= (int)mItems.size())
        mActiveIndex = -1;
    else
        mActiveIndex = index;
}

void LevelBuilderPalette::SetCategoryFilter(const char* cat)
{
    mCategoryFilter = cat ? cat : "";
}

void LevelBuilderPalette::SetSearchFilter(const char* search)
{
    mSearchFilter = search ? search : "";
}

bool LevelBuilderPalette::ItemMatchesFilter(int index) const
{
    if (index < 0 || index >= (int)mItems.size()) return false;
    const StoredItem& s = mItems[index];
    if (!mCategoryFilter.empty() && s.category != mCategoryFilter)
        return false;
    if (!mSearchFilter.empty())
    {
        if (!ContainsCI(s.displayName, mSearchFilter)
            && !ContainsCI(s.assetName, mSearchFilter)
            && !ContainsCI(s.tags, mSearchFilter))
            return false;
    }
    return true;
}

const char* LevelBuilderPalette::GetCategory(int index) const
{
    if (index < 0 || index >= (int)mCategories.size()) return "";
    return mCategories[index].c_str();
}
