#include "gc.hpp"

#include <windows.h>

namespace browser::js {

    GCHeap::GCHeap() {}

    static f32 elapsed_ms_from(LARGE_INTEGER start) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        return (f32)((f64)(now.QuadPart - start.QuadPart) * 1000.0 / (f64)freq.QuadPart);
    }

    GCHeap::~GCHeap() {
        for (auto *o : objects_) delete o;
        for (auto *f : functions_) delete f;
    }

    GCJSObject *GCHeap::alloc_object() {
        auto *obj = new GCJSObject();
        objects_.push_back(obj);
        obj_map_[&obj->obj] = obj;
        allocated_ += sizeof(GCJSObject);
        return obj;
    }

    GCJSFunction *GCHeap::alloc_function() {
        auto *fn = new GCJSFunction();
        functions_.push_back(fn);
        fn_map_[&fn->fn] = fn;
        allocated_ += sizeof(GCJSFunction);
        return fn;
    }

    void GCHeap::collect(const std::vector<JSValue *> &roots) {
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        for (auto *obj : objects_) obj->unmark();
        for (auto *fn : functions_) fn->unmark();
        mark_roots(roots);
        u32 pre_count = (u32)(objects_.size() + functions_.size());
        sweep();
        u32 freed = pre_count - (u32)(objects_.size() + functions_.size());
        cycle_count_++;
        last_collected_ = freed;
        total_collected_ += freed;
        last_pause_ms_ = elapsed_ms_from(start);
    }

    void GCHeap::mark_roots(const std::vector<JSValue *> &roots) {
        for (auto *r : roots) {
            if (r->type == JSValue::Type::OBJECT && r->object_val) {
                auto it = obj_map_.find(r->object_val);
                if (it != obj_map_.end() && !it->second->is_marked()) {
                    it->second->mark(*this);
                }
            }
            if (r->type == JSValue::Type::FUNCTION && r->function_val) {
                auto it = fn_map_.find(r->function_val);
                if (it != fn_map_.end() && !it->second->is_marked()) {
                    it->second->mark(*this);
                }
            }
        }
    }

    void GCHeap::sweep() {
        auto it = objects_.begin();
        while (it != objects_.end()) {
            if (!(*it)->is_marked()) {
                obj_map_.erase(&(*it)->obj);
                allocated_ -= sizeof(GCJSObject);
                delete *it;
                it = objects_.erase(it);
            } else {
                (*it)->unmark();
                ++it;
            }
        }
        auto fit = functions_.begin();
        while (fit != functions_.end()) {
            if (!(*fit)->is_marked()) {
                fn_map_.erase(&(*fit)->fn);
                allocated_ -= sizeof(GCJSFunction);
                delete *fit;
                fit = functions_.erase(fit);
            } else {
                (*fit)->unmark();
                ++fit;
            }
        }
    }

    void GCJSObject::mark_children(GCHeap &heap) {
        if (obj.prototype.type == JSValue::Type::OBJECT && obj.prototype.object_val) {
            auto *gc_obj = heap.lookup_object(obj.prototype.object_val);
            if (gc_obj && !gc_obj->is_marked()) {
                gc_obj->mark(heap);
            }
        }
        for (auto &[key, val] : obj.properties) {
            if (val.type == JSValue::Type::OBJECT && val.object_val) {
                auto *gc_obj = heap.lookup_object(val.object_val);
                if (gc_obj && !gc_obj->is_marked()) {
                    gc_obj->mark(heap);
                }
            }
            if (val.type == JSValue::Type::FUNCTION && val.function_val) {
                auto *gc_fn = heap.lookup_function(val.function_val);
                if (gc_fn && !gc_fn->is_marked()) {
                    gc_fn->mark(heap);
                }
            }
        }
        for (auto &el : obj.array_elements) {
            if (el.type == JSValue::Type::OBJECT && el.object_val) {
                auto *gc_obj = heap.lookup_object(el.object_val);
                if (gc_obj && !gc_obj->is_marked()) {
                    gc_obj->mark(heap);
                }
            }
            if (el.type == JSValue::Type::FUNCTION && el.function_val) {
                auto *gc_fn = heap.lookup_function(el.function_val);
                if (gc_fn && !gc_fn->is_marked()) {
                    gc_fn->mark(heap);
                }
            }
        }
    }

    void GCJSFunction::mark_children(GCHeap &heap) {
        if (fn.prototype_property.type == JSValue::Type::OBJECT && fn.prototype_property.object_val) {
            auto *gc_obj = heap.lookup_object(fn.prototype_property.object_val);
            if (gc_obj && !gc_obj->is_marked()) {
                gc_obj->mark(heap);
            }
        }
    }

}  // namespace browser::js
