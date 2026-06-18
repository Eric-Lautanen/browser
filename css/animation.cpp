#include "animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace browser::css {

    // ── AnimationEngine ────────────────────────────────────────────

    void AnimationEngine::register_keyframes(const KeyframesRule &kf) {
        keyframes_map_[kf.name] = kf;
    }

    void AnimationEngine::add_animation(const std::string &element_key, AnimationState anim) {
        if (anim.duration <= 0)
            anim.duration = 0.001f;
        animations_[element_key] = anim;
    }

    void AnimationEngine::remove_animation(const std::string &element_key) {
        animations_.erase(element_key);
        finished_animations_.erase(element_key);
    }

    bool AnimationEngine::has_active() const {
        return !animations_.empty();
    }

    f32 AnimationEngine::cubic_bezier(f32 t, f32 x1, f32 y1, f32 x2, f32 y2) const {
        // Newton-Raphson approximation
        f32 x = t;
        for (int i = 0; i < 8; i++) {
            f32 cx = 3.0f * (1.0f - x) * (1.0f - x) * x * x1 + 3.0f * (1.0f - x) * x * x * x2 + x * x * x;
            f32 dx =
                3.0f * (1.0f - x) * (1.0f - x) * x1 + 6.0f * (1.0f - x) * x * (x2 - x1) + 3.0f * x * x * (1.0f - x2);
            if (std::abs(dx) < 1e-6f)
                break;
            x -= (cx - t) / dx;
        }
        return 3.0f * (1.0f - x) * (1.0f - x) * x * y1 + 3.0f * (1.0f - x) * x * x * y2 + x * x * x;
    }

    f32 AnimationEngine::apply_timing_function(f32 t, const std::string &func) const {
        if (func == "linear")
            return t;
        if (func == "ease")
            return cubic_bezier(t, 0.25f, 0.1f, 0.25f, 1.0f);
        if (func == "ease-in")
            return cubic_bezier(t, 0.42f, 0.0f, 1.0f, 1.0f);
        if (func == "ease-out")
            return cubic_bezier(t, 0.0f, 0.0f, 0.58f, 1.0f);
        if (func == "ease-in-out")
            return cubic_bezier(t, 0.42f, 0.0f, 0.58f, 1.0f);
        // steps() - simplified
        if (func.find("steps(") != std::string::npos) {
            int n = 1;
            auto pos = func.find(',');
            if (pos != std::string::npos) {
                n = std::stoi(func.substr(6, pos - 6));
            }
            f32 step = 1.0f / n;
            return std::floor(t * n) * step;
        }
        // custom cubic-bezier()
        if (func.find("cubic-bezier(") != std::string::npos) {
            float x1, y1, x2, y2;
            if (sscanf(func.c_str(), "cubic-bezier(%f,%f,%f,%f)", &x1, &y1, &x2, &y2) == 4) {
                return cubic_bezier(t, x1, y1, x2, y2);
            }
        }
        return t;
    }

    f32 AnimationEngine::apply_direction(f32 t, const std::string &direction, f32 iteration) const {
        int iter_int = static_cast<int>(iteration);
        bool reverse = false;
        if (direction == "reverse")
            reverse = true;
        else if (direction == "alternate")
            reverse = (iter_int % 2 == 1);
        else if (direction == "alternate-reverse")
            reverse = (iter_int % 2 == 0);
        return reverse ? (1.0f - t) : t;
    }

    CSSValue AnimationEngine::interpolate_value(const CSSValue &from, const CSSValue &to, f32 t) const {
        CSSValue result = to;
        if (from.type == to.type) {
            switch (from.type) {
                case CSSValue::Type::NUMBER:
                    result.number = from.number + (to.number - from.number) * t;
                    break;
                case CSSValue::Type::LENGTH:
                    if (from.length.unit == to.length.unit) {
                        result.length.value = from.length.value + (to.length.value - from.length.value) * t;
                    }
                    break;
                case CSSValue::Type::COLOR:
                    result.color.r = static_cast<u8>(
                        std::max(0.0f, std::min(255.0f, from.color.r + (to.color.r - from.color.r) * t)));
                    result.color.g = static_cast<u8>(
                        std::max(0.0f, std::min(255.0f, from.color.g + (to.color.g - from.color.g) * t)));
                    result.color.b = static_cast<u8>(
                        std::max(0.0f, std::min(255.0f, from.color.b + (to.color.b - from.color.b) * t)));
                    result.color.a = static_cast<u8>(
                        std::max(0.0f, std::min(255.0f, from.color.a + (to.color.a - from.color.a) * t)));
                    break;
                case CSSValue::Type::PERCENTAGE:
                    result.number = from.number + (to.number - from.number) * t;
                    break;
                default:
                    result = (t < 0.5f) ? from : to;
                    break;
            }
        }
        return result;
    }

    void AnimationEngine::update(f32 dt) {
        std::vector<std::string> to_remove;

        for (auto &[key, anim] : animations_) {
            if (anim.finished)
                continue;

            if (!anim.started) {
                anim.started = true;
                if (anim.delay > 0) {
                    anim.elapsed = -anim.delay;
                } else {
                    anim.elapsed = 0;
                }
            }
            anim.elapsed += dt;

            if (anim.elapsed < 0)
                continue;

            f32 iteration_duration = anim.duration;
            if (iteration_duration <= 0)
                iteration_duration = 0.001f;

            bool infinite = anim.iteration_count < 0;
            f32 total_duration = infinite ? std::numeric_limits<f32>::max() : iteration_duration * anim.iteration_count;
            if (!infinite && anim.elapsed >= total_duration) {
                anim.finished = true;
                anim.iteration = anim.iteration_count;
                to_remove.push_back(key);
                continue;
            }

            anim.iteration = anim.elapsed / iteration_duration;
        }

        for (const auto &key : to_remove) {
            auto it = animations_.find(key);
            if (it != animations_.end()) {
                finished_animations_[key] = it->second;
                animations_.erase(it);
            }
        }
    }

    std::vector<std::pair<std::string, std::vector<Declaration>>> AnimationEngine::get_interpolated_declarations()
        const {
        std::vector<std::pair<std::string, std::vector<Declaration>>> result;

        for (const auto &[key, anim] : animations_) {
            auto kf_it = keyframes_map_.find(anim.name);
            if (kf_it == keyframes_map_.end() || kf_it->second.blocks.empty())
                continue;

            f32 iteration_duration = anim.duration;
            if (iteration_duration <= 0)
                iteration_duration = 0.001f;

            f32 local_time = std::fmod(anim.elapsed, iteration_duration);
            f32 progress = local_time / iteration_duration;
            progress = apply_direction(progress, anim.direction, anim.iteration);
            progress = apply_timing_function(progress, anim.timing_function);
            progress = std::max(0.0f, std::min(1.0f, progress));

            f32 pct = progress * 100.0f;

            const auto &blocks = kf_it->second.blocks;
            std::vector<Declaration> decls;

            for (size_t bi = 0; bi + 1 < blocks.size(); bi++) {
                const auto &block_a = blocks[bi];
                const auto &block_b = blocks[bi + 1];

                if (block_a.positions.empty() || block_b.positions.empty())
                    continue;

                f32 pos_a = block_a.positions[0];
                f32 pos_b = block_b.positions[0];

                if (pct < pos_a) {
                    // Before first keyframe — apply fill mode
                    if (anim.fill_mode == "backwards" || anim.fill_mode == "both") {
                        for (const auto &d : block_a.declarations) {
                            decls.push_back(d);
                        }
                    }
                    break;
                }

                if (pct >= pos_b)
                    continue;

                f32 range = pos_b - pos_a;
                if (range <= 0)
                    continue;
                f32 t = (pct - pos_a) / range;

                // Interpolate between declarations in block_a and block_b
                for (const auto &da : block_a.declarations) {
                    for (const auto &db : block_b.declarations) {
                        if (da.property != db.property)
                            continue;
                        Declaration interpolated;
                        interpolated.property = da.property;
                        if (!da.values.empty() && !db.values.empty()) {
                            interpolated.values.push_back(interpolate_value(da.values[0], db.values[0], t));
                        } else if (!da.values.empty()) {
                            interpolated.values = da.values;
                        } else {
                            interpolated.values = db.values;
                        }
                        decls.push_back(std::move(interpolated));
                        break;
                    }
                }

                // After interpolation, break so we don't process later keyframes
                break;
            }

            result.push_back({key, std::move(decls)});
        }

        // Also include finished animations with fill-mode: forwards/both
        for (const auto &[key, anim] : finished_animations_) {
            if (anim.fill_mode == "forwards" || anim.fill_mode == "both") {
                auto kf_it = keyframes_map_.find(anim.name);
                if (kf_it == keyframes_map_.end() || kf_it->second.blocks.empty())
                    continue;
                const auto &last_block = kf_it->second.blocks.back();
                std::vector<Declaration> decls;
                for (const auto &d : last_block.declarations) {
                    decls.push_back(d);
                }
                result.push_back({key, std::move(decls)});
            }
        }

        return result;
    }

    // ── TransitionManager ──────────────────────────────────────────

    CSSValue TransitionManager::interpolate_value(const CSSValue &from, const CSSValue &to, f32 t) const {
        CSSValue result = to;
        if (from.type == to.type) {
            switch (from.type) {
                case CSSValue::Type::NUMBER:
                    result.number = from.number + (to.number - from.number) * t;
                    break;
                case CSSValue::Type::LENGTH:
                    if (from.length.unit == to.length.unit) {
                        result.length.value = from.length.value + (to.length.value - from.length.value) * t;
                    }
                    break;
                case CSSValue::Type::COLOR: {
                    auto lerp = [t](u8 a, u8 b) -> u8 {
                        return static_cast<u8>(std::max(0.0f, std::min(255.0f, a + (b - a) * t)));
                    };
                    result.color = {lerp(from.color.r, to.color.r),
                                    lerp(from.color.g, to.color.g),
                                    lerp(from.color.b, to.color.b),
                                    lerp(from.color.a, to.color.a)};
                    break;
                }
                case CSSValue::Type::PERCENTAGE:
                    result.number = from.number + (to.number - from.number) * t;
                    break;
                default:
                    result = (t < 0.5f) ? from : to;
                    break;
            }
        }
        return result;
    }

    f32 TransitionManager::cubic_bezier(f32 t, f32 x1, f32 y1, f32 x2, f32 y2) const {
        f32 x = t;
        for (int i = 0; i < 8; i++) {
            f32 cx = 3.0f * (1.0f - x) * (1.0f - x) * x * x1 + 3.0f * (1.0f - x) * x * x * x2 + x * x * x;
            f32 dx =
                3.0f * (1.0f - x) * (1.0f - x) * x1 + 6.0f * (1.0f - x) * x * (x2 - x1) + 3.0f * x * x * (1.0f - x2);
            if (std::abs(dx) < 1e-6f)
                break;
            x -= (cx - t) / dx;
        }
        return 3.0f * (1.0f - x) * (1.0f - x) * x * y1 + 3.0f * (1.0f - x) * x * x * y2 + x * x * x;
    }

    f32 TransitionManager::apply_timing_function(f32 t, const std::string &func) const {
        if (func == "linear")
            return t;
        if (func == "ease")
            return cubic_bezier(t, 0.25f, 0.1f, 0.25f, 1.0f);
        if (func == "ease-in")
            return t * t;
        if (func == "ease-out")
            return t * (2.0f - t);
        if (func == "ease-in-out")
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        return t;
    }

    void TransitionManager::on_style_change(const std::string &element_key,
                                            const std::unordered_map<std::string, CSSValue> &old_style,
                                            const std::unordered_map<std::string, CSSValue> &new_style,
                                            f32 transition_duration,
                                            const std::string &timing_func) {
        if (transition_duration <= 0) {
            transitions_.erase(element_key);
            return;
        }

        auto &element_transitions = transitions_[element_key];
        std::vector<std::string> handled;

        for (auto &ts : element_transitions) {
            auto it = new_style.find(ts.property);
            if (it != new_style.end()) {
                // Check if value changed
                bool changed = false;
                if (it->second.type != ts.to_value.type)
                    changed = true;
                else if (it->second.type == CSSValue::Type::NUMBER && it->second.number != ts.to_value.number)
                    changed = true;
                else if (it->second.type == CSSValue::Type::LENGTH &&
                         it->second.length.value != ts.to_value.length.value)
                    changed = true;
                else if (it->second.type == CSSValue::Type::COLOR &&
                         (it->second.color.r != ts.to_value.color.r || it->second.color.g != ts.to_value.color.g ||
                          it->second.color.b != ts.to_value.color.b))
                    changed = true;

                if (changed) {
                    ts.from_value = ts.to_value;
                    ts.to_value = it->second;
                    ts.elapsed = 0;
                    ts.started = false;
                    ts.finished = false;
                }
                handled.push_back(ts.property);
            }
        }

        // Find new properties not already transitioned
        for (const auto &[prop, val] : new_style) {
            if (std::find(handled.begin(), handled.end(), prop) != handled.end())
                continue;
            auto old_it = old_style.find(prop);
            if (old_it != old_style.end()) {
                // Value changed, start transition
                bool changed = false;
                if (old_it->second.type != val.type)
                    changed = true;
                else if (val.type == CSSValue::Type::NUMBER && old_it->second.number != val.number)
                    changed = true;
                else if (val.type == CSSValue::Type::LENGTH && old_it->second.length.value != val.length.value)
                    changed = true;
                else if (val.type == CSSValue::Type::COLOR &&
                         (old_it->second.color.r != val.color.r || old_it->second.color.g != val.color.g ||
                          old_it->second.color.b != val.color.b))
                    changed = true;

                if (changed) {
                    TransitionState ts;
                    ts.property = prop;
                    ts.from_value = old_it->second;
                    ts.to_value = val;
                    ts.duration = transition_duration;
                    ts.timing_function = timing_func;
                    element_transitions.push_back(std::move(ts));
                }
            }
        }
    }

    void TransitionManager::update(f32 dt) {
        std::vector<std::pair<std::string, size_t>> to_remove;

        for (auto &[key, transitions] : transitions_) {
            for (size_t i = 0; i < transitions.size(); i++) {
                auto &ts = transitions[i];
                if (ts.finished)
                    continue;

                if (!ts.started) {
                    if (ts.delay > 0) {
                        ts.delay -= dt;
                        if (ts.delay > 0)
                            continue;
                        ts.started = true;
                        ts.elapsed = -ts.delay;
                    } else {
                        ts.started = true;
                        ts.elapsed = dt;
                    }
                } else {
                    ts.elapsed += dt;
                }

                if (ts.elapsed >= ts.duration) {
                    ts.finished = true;
                    continue;
                }
            }
        }
    }

    std::vector<std::pair<std::string, std::vector<std::pair<std::string, CSSValue>>>>
    TransitionManager::get_transitioned_values() const {
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, CSSValue>>>> result;

        for (const auto &[key, transitions] : transitions_) {
            std::vector<std::pair<std::string, CSSValue>> values;
            for (const auto &ts : transitions) {
                if (ts.finished) {
                    values.push_back({ts.property, ts.to_value});
                } else {
                    f32 progress = ts.elapsed / ts.duration;
                    progress = std::max(0.0f, std::min(1.0f, progress));
                    progress = apply_timing_function(progress, ts.timing_function);
                    auto interpolated = interpolate_value(ts.from_value, ts.to_value, progress);
                    values.push_back({ts.property, interpolated});
                }
            }
            result.push_back({key, std::move(values)});
        }

        return result;
    }

}  // namespace browser::css
