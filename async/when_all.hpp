#pragma once
// DELETED: prior when_all was UAF-by-design (destroyed wrapper frame mid-flight) and used nonexistent
// task<T>::result_type. Parallel fetch now uses counter+event join (BR-P5). This header remains as a stub
// to avoid breaking includes; any use will fail to link.
namespace browser::async {
template<typename... Ts> auto when_all(Ts&&...) = delete;
}

