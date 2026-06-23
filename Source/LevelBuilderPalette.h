/**
 * @file LevelBuilderPalette.h
 * @brief A palette of LevelBuilderPaletteItems, owned by the core addon
 *        (so it survives modular hot-reload as long as core stays loaded).
 *
 * Palettes are typically populated by sibling addons (the modular addon
 * pushes one item per kit piece). The active palette and active item drive
 * the placement preview.
 */

#pragma once

#include "LevelBuilderCoreAPI.h"
#include <string>
#include <vector>

class LevelBuilderPalette
{
public:
    explicit LevelBuilderPalette(const std::string& name);

    const std::string& GetName() const { return mName; }

    void AddItem(const LevelBuilderPaletteItem& item);
    void Clear();

    int  GetItemCount() const { return (int)mItems.size(); }

    // Fills outItem with stable pointers into the palette's storage.
    // Returns false if index is out of range.
    bool GetItem(int index, LevelBuilderPaletteItem* outItem) const;

    // Mutable item access for the editor UI.
    int  GetActiveIndex() const { return mActiveIndex; }
    void SetActiveIndex(int index);

    void SetCategoryFilter(const char* cat);
    const std::string& GetCategoryFilter() const { return mCategoryFilter; }

    void SetSearchFilter(const char* search);
    const std::string& GetSearchFilter() const { return mSearchFilter; }

    // Returns true when the item at `index` passes the current category +
    // search filters. The UI hides items where this returns false.
    bool ItemMatchesFilter(int index) const;

    // Category enumeration (deduplicated, sorted insertion-order).
    int  GetCategoryCount() const { return (int)mCategories.size(); }
    const char* GetCategory(int index) const;

private:
    struct StoredItem
    {
        std::string displayName;
        std::string assetName;
        std::string category;
        std::string iconPath;
        std::string tags;
    };

    std::string              mName;
    std::vector<StoredItem>  mItems;
    std::vector<std::string> mCategories;
    std::string              mCategoryFilter;
    std::string              mSearchFilter;
    int                      mActiveIndex = -1;
};
