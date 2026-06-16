#pragma once
#include <vector>
#include <unordered_map>
#include "../tests/utility.hpp"
#include "value.hpp"

namespace browser::js {

class GCHeap;

class GCObject {
public:
    virtual ~GCObject() = default;
    virtual void mark_children(GCHeap& heap) = 0;
    bool is_marked() const { return marked_; }
    void mark(GCHeap& heap) {
        if (marked_) return;
        marked_ = true;
        mark_children(heap);
    }
    void unmark() { marked_ = false; }
private:
    bool marked_ = false;
};

class GCJSObject : public GCObject {
public:
    JSObject obj;
    void mark_children(GCHeap& heap) override;
};

class GCJSFunction : public GCObject {
public:
    JSFunction fn;
    void mark_children(GCHeap& heap) override;
};

class GCHeap {
public:
    GCHeap();
    ~GCHeap();
    GCJSObject* alloc_object();
    GCJSFunction* alloc_function();
    void collect(const std::vector<JSValue*>& roots);
    u32 allocated_bytes() const { return allocated_; }
    u32 threshold() const { return threshold_; }
    u32 object_count() const { return (u32)objects_.size(); }
    u32 function_count() const { return (u32)functions_.size(); }
    GCJSObject* lookup_object(JSObject* obj) {
        auto it = obj_map_.find(obj);
        return it != obj_map_.end() ? it->second : nullptr;
    }
    GCJSFunction* lookup_function(JSFunction* fn) {
        auto it = fn_map_.find(fn);
        return it != fn_map_.end() ? it->second : nullptr;
    }
private:
    std::vector<GCJSObject*> objects_;
    std::vector<GCJSFunction*> functions_;
    u32 allocated_ = 0;
    u32 threshold_ = 1024 * 1024;
    void mark_roots(const std::vector<JSValue*>& roots);
    void sweep();
    std::unordered_map<JSObject*, GCJSObject*> obj_map_;
    std::unordered_map<JSFunction*, GCJSFunction*> fn_map_;
};

}
