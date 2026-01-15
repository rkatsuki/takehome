#pragma once
#include <unordered_map>
#include <string>
#include <optional>
#include <list>
#include "Type.hpp"

class OrderRegistry {
public:
    struct Location {
        Side side;
        double price;
        std::list<Order>::iterator it;
    };

    // record mapping for both ID and Tag
    void record(long id, const std::string& tag, const Location& loc);
    
    // remove mapping (Safe cleanup)
    void remove(long id, const std::string& tag);

    // Lookups
    std::optional<Location> getLocation(long id) const;
    std::optional<Location> getLocationByTag(const std::string& tag) const;

private:
    std::unordered_map<long, Location> idToLocation;
    std::unordered_map<std::string, long> tagToId;
};