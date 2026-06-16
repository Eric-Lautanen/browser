#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/value.hpp"
#include "../js/gc.hpp"
#include "../js/vm.hpp"
#include "../js/parser.hpp"
#include "../js/compiler.hpp"

using namespace browser;
using namespace browser::js;

TEST(gc_basic, {
    GCHeap heap;
    ASSERT_EQ(heap.object_count(), 0);
    auto* obj1 = heap.alloc_object();
    auto* obj2 = heap.alloc_object();
    (void)obj1; (void)obj2;
    ASSERT_EQ(heap.object_count(), 2);
    ASSERT(heap.allocated_bytes() >= sizeof(GCJSObject) * 2);
    heap.collect({});
    ASSERT_EQ(heap.object_count(), 0);
})

TEST(gc_mark, {
    GCHeap heap;
    auto* parent = heap.alloc_object();
    auto* child = heap.alloc_object();
    parent->obj.set("child", JSValue::object(&child->obj));
    JSValue root = JSValue::object(&parent->obj);
    std::vector<JSValue*> roots = {&root};
    heap.collect(roots);
    ASSERT_EQ(heap.object_count(), 2);
    JSValue val = parent->obj.get("child");
    ASSERT(val.type == JSValue::Type::OBJECT);
    ASSERT_EQ(val.object_val, &child->obj);
})

TEST(gc_unreachable, {
    GCHeap heap;
    auto* obj = heap.alloc_object();
    JSValue root = JSValue::object(&obj->obj);
    heap.collect({&root});
    ASSERT_EQ(heap.object_count(), 1);
    heap.collect({});
    ASSERT_EQ(heap.object_count(), 0);
})

TEST(gc_side_map, {
    GCHeap heap;
    auto* gc_obj = heap.alloc_object();
    JSObject* obj_ptr = &gc_obj->obj;
    auto* looked_up = heap.lookup_object(obj_ptr);
    ASSERT_EQ(looked_up, gc_obj);
})

TEST(gc_side_map_function, {
    GCHeap heap;
    auto* gc_fn = heap.alloc_function();
    JSFunction* fn_ptr = &gc_fn->fn;
    auto* looked_up = heap.lookup_function(fn_ptr);
    ASSERT_EQ(looked_up, gc_fn);
})

TEST(gc_sweep_unmarked, {
    GCHeap heap;
    auto* obj = heap.alloc_object();
    auto* fn = heap.alloc_function();
    (void)obj; (void)fn;
    ASSERT_EQ(heap.object_count(), 1);
    ASSERT_EQ(heap.function_count(), 1);
    heap.collect({});
    ASSERT_EQ(heap.object_count(), 0);
    ASSERT_EQ(heap.function_count(), 0);
})

TEST(gc_cycle, {
    GCHeap heap;
    auto* a = heap.alloc_object();
    auto* b = heap.alloc_object();
    a->obj.set("ref", JSValue::object(&b->obj));
    b->obj.set("ref", JSValue::object(&a->obj));
    JSValue root = JSValue::object(&a->obj);
    heap.collect({&root});
    ASSERT_EQ(heap.object_count(), 2);
    heap.collect({});
    ASSERT_EQ(heap.object_count(), 0);
})

TEST(gc_allocated_bytes, {
    GCHeap heap;
    u32 before = heap.allocated_bytes();
    heap.alloc_object();
    u32 after = heap.allocated_bytes();
    ASSERT_EQ(after - before, sizeof(GCJSObject));
})

TEST(gc_vm_global_alive, {
    VM vm;
    vm.register_builtins();
    auto* obj = vm.heap()->alloc_object();
    vm.global_object()->set("test", JSValue::object(&obj->obj));
    auto roots = vm.gc_roots();
    vm.heap()->collect(roots);
    auto val = vm.global_object()->get("test");
    ASSERT(val.type == JSValue::Type::OBJECT);
})

TEST(gc_vm_execute_doesnt_crash, {
    VM vm;
    vm.register_builtins();
    auto bc = Compiler{}.compile(*Parser("var x = {a:1,b:2}; x.a;").parse_program());
    auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 1);
})

TEST(gc_mark_function, {
    GCHeap heap;
    auto* parent = heap.alloc_object();
    auto* fn = heap.alloc_function();
    parent->obj.set("method", JSValue::function(&fn->fn));
    JSValue root = JSValue::object(&parent->obj);
    heap.collect({&root});
    ASSERT_EQ(heap.object_count(), 1);
    ASSERT_EQ(heap.function_count(), 1);
})

TEST(gc_lookup_null_after_sweep, {
    GCHeap heap;
    auto* obj = heap.alloc_object();
    JSObject* ptr = &obj->obj;
    ASSERT(heap.lookup_object(ptr) != nullptr);
    heap.collect({});
    ASSERT_EQ(heap.lookup_object(ptr), nullptr);
})

TEST(gc_lookup_function_null_after_sweep, {
    GCHeap heap;
    auto* fn = heap.alloc_function();
    JSFunction* ptr = &fn->fn;
    ASSERT(heap.lookup_function(ptr) != nullptr);
    heap.collect({});
    ASSERT_EQ(heap.lookup_function(ptr), nullptr);
})

TEST(gc_threshold_getter, {
    GCHeap heap;
    ASSERT_EQ(heap.threshold(), 1024 * 1024);
})
