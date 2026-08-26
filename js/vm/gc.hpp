#pragma once
#include "../../tests/utility.hpp"
#include "../value.hpp"

#include <unordered_map>
#include <vector>

namespace browser::js {

    class GCHeap;

    class GCObject {
    public:
        virtual ~GCObject() = default;
        virtual void mark_children(GCHeap &heap) = 0;
        bool is_marked() const { return marked_; }
        // X-C2: marking is iterative — mark() flags the object and queues it
        // on the heap's worklist; the collector drains the list, calling
        // mark_children once per object. No recursion over heap graphs.
        void mark(GCHeap &heap);
        void unmark() { marked_ = false; }

    private:
        friend class GCHeap;
        bool marked_ = false;
    };

    class GCJSObject : public GCObject {
    public:
        JSObject obj;
        void mark_children(GCHeap &heap) override;
    };

    class GCJSFunction : public GCObject {
    public:
        JSFunction fn;
        void mark_children(GCHeap &heap) override;
    };

    class GCHeap {
    public:
        GCHeap();
        ~GCHeap();
        GCJSObject *alloc_object();
        GCJSFunction *alloc_function();
        void collect(const std::vector<JSValue *> &roots);
        u32 allocated_bytes() const { return allocated_; }
        u32 threshold() const { return threshold_; }
        u32 object_count() const { return (u32)objects_.size(); }
        u32 function_count() const { return (u32)functions_.size(); }
        u32 total_collected() const { return total_collected_; }
        u32 last_collected() const { return last_collected_; }
        f32 last_pause_ms() const { return last_pause_ms_; }
        u32 cycle_count() const { return cycle_count_; }
        GCJSObject *lookup_object(JSObject *obj) {
            auto it = obj_map_.find(obj);
            return it != obj_map_.end() ? it->second : nullptr;
        }
        GCJSFunction *lookup_function(JSFunction *fn) {
            auto it = fn_map_.find(fn);
            return it != fn_map_.end() ? it->second : nullptr;
        }

    private:
        std::vector<GCJSObject *> objects_;
        std::vector<GCJSFunction *> functions_;
        u32 allocated_ = 0;
        u32 threshold_ = 1024 * 1024;
        u32 total_collected_ = 0;
        u32 last_collected_ = 0;
        f32 last_pause_ms_ = 0;
        u32 cycle_count_ = 0;
        // X-C2: iterative marking worklist.
        std::vector<GCObject *> mark_stack_;

    public:
        void defer_mark(GCObject *obj) { mark_stack_.push_back(obj); }

    private:
        void mark_roots(const std::vector<JSValue *> &roots);
        void sweep();
        std::unordered_map<JSObject *, GCJSObject *> obj_map_;
        std::unordered_map<JSFunction *, GCJSFunction *> fn_map_;
    };

}  // namespace browser::js
