#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "css_values.hpp"
#include "cascade.hpp"
#include "../html/dom.hpp"

namespace browser::css {

struct AnimationState {
    std::string name;
    f32 duration = 0;
    f32 delay = 0;
    f32 iteration_count = 1;
    std::string timing_function = "linear";
    std::string direction = "normal";
    std::string fill_mode = "none";
    std::string play_state = "running";
    f32 elapsed = 0;
    f32 iteration = 0;
    bool finished = false;
    bool started = false;
    const KeyframesRule* keyframes = nullptr;
};

class AnimationEngine {
public:
    void add_animation(const std::string& element_key, AnimationState anim);
    void remove_animation(const std::string& element_key);
    void update(f32 dt);
    bool has_active() const;

    std::vector<std::pair<std::string, std::vector<Declaration>>> get_interpolated_declarations() const;

    void register_keyframes(const KeyframesRule& kf);

private:
    std::unordered_map<std::string, AnimationState> animations_;
    std::unordered_map<std::string, KeyframesRule> keyframes_map_;
    std::unordered_map<std::string, AnimationState> finished_animations_;

    f32 apply_timing_function(f32 t, const std::string& func) const;
    f32 apply_direction(f32 t, const std::string& direction, f32 iteration) const;
    f32 cubic_bezier(f32 t, f32 x1, f32 y1, f32 x2, f32 y2) const;
    CSSValue interpolate_value(const CSSValue& from, const CSSValue& to, f32 t) const;
};

class TransitionManager {
public:
    struct TransitionState {
        std::string property;
        CSSValue from_value;
        CSSValue to_value;
        f32 duration = 0;
        f32 delay = 0;
        std::string timing_function = "linear";
        f32 elapsed = 0;
        bool started = false;
        bool finished = false;
    };

    void on_style_change(const std::string& element_key,
                         const std::unordered_map<std::string, CSSValue>& old_style,
                         const std::unordered_map<std::string, CSSValue>& new_style,
                         f32 transition_duration,
                         const std::string& timing_func);
    void update(f32 dt);
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, CSSValue>>>>
    get_transitioned_values() const;

private:
    std::unordered_map<std::string, std::vector<TransitionState>> transitions_;
    f32 apply_timing_function(f32 t, const std::string& func) const;
    CSSValue interpolate_value(const CSSValue& from, const CSSValue& to, f32 t) const;
};

}
