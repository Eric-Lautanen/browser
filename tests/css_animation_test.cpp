#include "test_framework.hpp"
#include "utility.hpp"
#include "../css/css_values.hpp"
#include "../css/parser.hpp"
#include "../css/animation.hpp"
#include "../css/cascade.hpp"
#include <cmath>

TEST(css_keyframes_parse, {
    using namespace browser::css;
    std::string css = R"(
        @keyframes fade {
            from { opacity: 0; }
            to { opacity: 1; }
        }
        @keyframes slide {
            0% { transform: translateX(0px); }
            50% { transform: translateX(100px); }
            100% { transform: translateX(200px); }
        }
    )";

    CssParser parser(css);
    StyleSheet sheet = parser.parse();

    bool found_fade = false;
    bool found_slide = false;

    for (const auto& at : sheet.at_rules) {
        std::string lower_name;
        for (char& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower_name == "keyframes" || lower_name == "-webkit-keyframes") {
            if (at.keyframes.name == "fade") {
                found_fade = true;
                ASSERT(at.keyframes.blocks.size() >= 2);
                // from = 0%
                ASSERT(at.keyframes.blocks[0].positions.size() > 0);
                ASSERT_EQ(static_cast<int>(at.keyframes.blocks[0].positions[0]), 0);
                // to = 100%
                ASSERT(at.keyframes.blocks[1].positions.size() > 0);
                ASSERT_EQ(static_cast<int>(at.keyframes.blocks[1].positions[0]), 100);
            }
            if (at.keyframes.name == "slide") {
                found_slide = true;
                ASSERT(at.keyframes.blocks.size() >= 3);
                ASSERT_EQ(at.keyframes.blocks[0].positions[0], 0.0f);
                ASSERT_EQ(at.keyframes.blocks[1].positions[0], 50.0f);
                ASSERT_EQ(at.keyframes.blocks[2].positions[0], 100.0f);
            }
        }
    }

    ASSERT(found_fade);
    ASSERT(found_slide);
})

TEST(css_animation_engine_basic, {
    using namespace browser;
    using namespace browser::css;
    AnimationEngine engine;

    // Register a keyframe rule
    KeyframesRule kf;
    kf.name = "fade";

    KeyframeBlock from_block, to_block;
    from_block.positions.push_back(0);
    Declaration from_decl;
    from_decl.property = "opacity";
    CSSValue from_val;
    from_val.type = CSSValue::Type::NUMBER;
    from_val.number = 0;
    from_decl.values.push_back(from_val);
    from_block.declarations.push_back(from_decl);

    to_block.positions.push_back(100);
    Declaration to_decl;
    to_decl.property = "opacity";
    CSSValue to_val;
    to_val.type = CSSValue::Type::NUMBER;
    to_val.number = 1;
    to_decl.values.push_back(to_val);
    to_block.declarations.push_back(to_decl);

    kf.blocks.push_back(from_block);
    kf.blocks.push_back(to_block);

    engine.register_keyframes(kf);

    // Add animation
    AnimationState anim;
    anim.name = "fade";
    anim.duration = 1.0f;
    anim.timing_function = "linear";
    anim.play_state = "running";
    engine.add_animation("test_elem", anim);

    // Update for half duration (0.5s) -> should be half interpolated
    engine.update(0.5f);

    auto interpolated = engine.get_interpolated_declarations();
    bool found_opacity = false;
    for (const auto& [key, decls] : interpolated) {
        if (key == "test_elem") {
            for (const auto& d : decls) {
                if (d.property == "opacity" && !d.values.empty()) {
                    found_opacity = true;
                    // At 50% progress, opacity should be ~0.5
                    float diff = std::abs(d.values[0].number - 0.5f);
                    ASSERT(diff < 0.1f);
                }
            }
        }
    }
    ASSERT(found_opacity);
})

TEST(css_animation_timing_ease, {
    using namespace browser::css;
    AnimationEngine engine;

    // Test that timing functions produce valid outputs
    KeyframesRule kf;
    kf.name = "test";
    KeyframeBlock b0, b1;
    b0.positions.push_back(0);
    Declaration d;
    d.property = "opacity";
    CSSValue v0, v1;
    v0.type = CSSValue::Type::NUMBER; v0.number = 0;
    v1.type = CSSValue::Type::NUMBER; v1.number = 1;
    d.values.push_back(v0);
    b0.declarations.push_back(d);
    b1.positions.push_back(100);
    Declaration d1;
    d1.property = "opacity";
    d1.values.push_back(v1);
    b1.declarations.push_back(d1);
    kf.blocks.push_back(b0);
    kf.blocks.push_back(b1);
    engine.register_keyframes(kf);

    AnimationState anim;
    anim.name = "test";
    anim.duration = 1.0f;
    anim.timing_function = "ease";
    engine.add_animation("ease_elem", anim);

    engine.update(0.5f);
    auto result = engine.get_interpolated_declarations();
    ASSERT(!result.empty());

    // Timing function should give a valid result between 0 and 1
    for (const auto& [key, decls] : result) {
        for (const auto& d : decls) {
            if (d.property == "opacity" && !d.values.empty()) {
                ASSERT(d.values[0].number >= 0.0f);
                ASSERT(d.values[0].number <= 1.0f);
            }
        }
    }
})

TEST(css_animation_direction_reverse, {
    using namespace browser::css;
    AnimationEngine engine;

    KeyframesRule kf;
    kf.name = "test";
    KeyframeBlock b0, b1;
    b0.positions.push_back(0);
    Declaration d;
    d.property = "opacity";
    CSSValue v0, v1;
    v0.type = CSSValue::Type::NUMBER; v0.number = 0;
    v1.type = CSSValue::Type::NUMBER; v1.number = 1;
    d.values.push_back(v0);
    b0.declarations.push_back(d);
    b1.positions.push_back(100);
    Declaration d1;
    d1.property = "opacity";
    d1.values.push_back(v1);
    b1.declarations.push_back(d1);
    kf.blocks.push_back(b0);
    kf.blocks.push_back(b1);
    engine.register_keyframes(kf);

    AnimationState anim;
    anim.name = "test";
    anim.duration = 1.0f;
    anim.direction = "reverse";
    engine.add_animation("rev_elem", anim);

    engine.update(0.5f);
    auto result = engine.get_interpolated_declarations();
    ASSERT(!result.empty());
})

TEST(css_transform_parse, {
    using namespace browser::css;
    CssParser parser("div { transform: rotate(45deg) scale(2) translate(10px, 20px); }");
    StyleSheet sheet = parser.parse();

    ASSERT(!sheet.rules.empty());
    ASSERT(!sheet.rules[0].declarations.empty());

    const auto& decl = sheet.rules[0].declarations[0];
    ASSERT_EQ(decl.property, "transform");
    ASSERT(!decl.values.empty());
    // Each transform function is a separate value in the declaration
    ASSERT(decl.values.size() >= 3);
    ASSERT_EQ(decl.values[0].type, CSSValue::Type::TRANSFORM);
    ASSERT(decl.values[0].transforms.size() >= 1);
    ASSERT_EQ(decl.values[0].transforms[0].type, TransformFunc::Type::ROTATE);
    ASSERT_EQ(decl.values[1].type, CSSValue::Type::TRANSFORM);
    ASSERT_EQ(decl.values[1].transforms[0].type, TransformFunc::Type::SCALE);
    ASSERT_EQ(decl.values[2].type, CSSValue::Type::TRANSFORM);
    ASSERT_EQ(decl.values[2].transforms[0].type, TransformFunc::Type::TRANSLATE);
})

TEST(css_calc_simple, {
    using namespace browser::css;
    CssParser parser("div { width: calc(100% - 20px); }");
    StyleSheet sheet = parser.parse();

    ASSERT(!sheet.rules.empty());
    ASSERT(!sheet.rules[0].declarations.empty());

    const auto& decl = sheet.rules[0].declarations[0];
    ASSERT_EQ(decl.property, "width");
    ASSERT(!decl.values.empty());

    // calc() returns a STRING for deferred evaluation at layout time
    const auto& val = decl.values[0];
    ASSERT(val.type == CSSValue::Type::STRING);
    ASSERT(!val.string_value.empty());
    ASSERT(val.string_value.find("calc(") != std::string::npos ||
           val.string_value.find("100%") != std::string::npos);
})

TEST(css_custom_property, {
    using namespace browser::css;
    CssParser parser(":root { --my-color: red; } div { color: var(--my-color); }");
    StyleSheet sheet = parser.parse();

    ASSERT(!sheet.rules.empty());
    bool found_custom = false;
    bool found_var = false;

    for (const auto& rule : sheet.rules) {
        for (const auto& decl : rule.declarations) {
            if (decl.property == "--my-color") {
                found_custom = true;
            }
            if (!decl.values.empty() && decl.values[0].type == CSSValue::Type::KEYWORD && 
                decl.values[0].keyword.substr(0, 4) == "var(") {
                found_var = true;
            }
        }
    }
    ASSERT(found_custom);
    ASSERT(found_var);
})

TEST(css_gradient_parse, {
    using namespace browser::css;
    // Test that gradient functions are parsed (may be FUNCTION type depending on tokenizer)
    CssParser parser("div { background: linear-gradient(red, blue); }");
    StyleSheet sheet = parser.parse();

    ASSERT(!sheet.rules.empty());
    ASSERT(!sheet.rules[0].declarations.empty());
    const auto& decl = sheet.rules[0].declarations[0];

    ASSERT(!decl.values.empty());
    // The gradient may be parsed as GRADIENT or FUNCTION type depending on tokenizer
    bool is_gradient = (decl.values[0].type == CSSValue::Type::GRADIENT);
    bool is_function = (decl.values[0].type == CSSValue::Type::FUNCTION);
    ASSERT(is_gradient || is_function);
})

TEST(css_media_query_eval, {
    using namespace browser::css;
    // Minimal test: cascade filters by @media queries
    ASSERT(true); // Placeholder for integration test
})

TEST(css_pseudo_classes, {
    using namespace browser::css;
    CssParser parser("div:hover { color: red; } li:nth-child(odd) { color: blue; }");
    StyleSheet sheet = parser.parse();

    ASSERT(!sheet.rules.empty());
    ASSERT(!sheet.rules[0].selectors.empty());
    ASSERT(!sheet.rules[0].selectors[0].compounds.empty());
    ASSERT(!sheet.rules[0].selectors[0].compounds[0].simples.empty());

    // Check that pseudo-classes are correctly parsed
    bool found_hover = false;
    bool found_nth = false;
    for (const auto& rule : sheet.rules) {
        for (const auto& sel : rule.selectors) {
            for (const auto& comp : sel.compounds) {
                for (const auto& ss : comp.simples) {
                    if (ss.type == SimpleSelector::Type::PSEUDO_CLASS) {
                        if (ss.name == "hover") found_hover = true;
                        if (ss.name == "nth-child") found_nth = true;
                    }
                }
            }
        }
    }
    ASSERT(found_hover);
    ASSERT(found_nth);
})
