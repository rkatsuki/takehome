#include "OrderRegistry.hpp"

void OrderRegistry::record(long id, const std::string& tag, const Location& loc) {
    idToLocation[id] = loc;
    if (!tag.empty()) {
        tagToId[tag] = id;
    }
}

void OrderRegistry::remove(long id, const std::string& tag) {
    // 1. Cleanup by ID if provided (or found via tag)
    if (id != 0) {
        idToLocation.erase(id);
    }

    // 2. Safe cleanup by Tag
    if (!tag.empty()) {
        auto it = tagToId.find(tag);
        if (it != tagToId.end()) {
            // ONLY erase if this tag still points to the ID we are removing.
            // This prevents a late cancel from deleting a newer order with the same tag.
            if (id == 0 || it->second == id) {
                tagToId.erase(it);
            }
        }
    }
}

std::optional<OrderRegistry::Location> OrderRegistry::getLocation(long id) const {
    auto it = idToLocation.find(id);
    if (it != idToLocation.end()) return it->second;
    return std::nullopt;
}

std::optional<OrderRegistry::Location> OrderRegistry::getLocationByTag(const std::string& tag) const {
    auto itTag = tagToId.find(tag);
    if (itTag != tagToId.end()) {
        return getLocation(itTag->second);
    }
    return std::nullopt;
}