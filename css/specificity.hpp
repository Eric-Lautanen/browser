#pragma once
#include "css_values.hpp"

namespace browser::css {

struct Specificity {
    u32 bits = 0;

    auto operator<=>(const Specificity&) const = default;

    static Specificity make(u8 ids, u8 classes, u8 tags) {
        Specificity s;
        s.bits = (u32(ids) << 16) | (u32(classes) << 8) | u32(tags);
        return s;
    }
};

static_assert(sizeof(Specificity) == 4, "Specificity must be exactly 4 bytes");

inline Specificity compute_specificity(const Selector& sel) {
    u8 ids = 0, classes = 0, tags = 0;
    for (const auto& compound : sel.compounds) {
        for (const auto& ss : compound.simples) {
            switch (ss.type) {
                case SimpleSelector::Type::ID:
                    ids++;
                    break;
                case SimpleSelector::Type::CLASS:
                case SimpleSelector::Type::ATTRIBUTE:
                case SimpleSelector::Type::PSEUDO_CLASS:
                    classes++;
                    break;
                case SimpleSelector::Type::TAG:
                case SimpleSelector::Type::PSEUDO_ELEMENT:
                    tags++;
                    break;
                case SimpleSelector::Type::UNIVERSAL:
                    break;
            }
        }
    }
    return Specificity::make(ids, classes, tags);
}

}
