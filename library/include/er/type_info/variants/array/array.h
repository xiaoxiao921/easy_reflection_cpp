#pragma once

#include <memory>

#include "c_array.h"
#include "std_array.h"

namespace er {

struct Array final : public IArray {
  Array() = delete;

  template <typename T, size_t size_v>
  Array(T (*array)[size_v], bool is_const) : _array(std::make_shared<CArray<T, size_v>>(array, is_const)) {
  }

  template <typename T, size_t size_v>
  Array(std::array<T, size_v>* array, bool is_const) : _array(std::make_shared<StdArray<T, size_v>>(array, is_const)) {
  }

  Expected<None> assign(Var var) override {
    return _array->assign(var);
  }

  void unsafe_assign(void* ptr) override {
    _array->unsafe_assign(ptr);
  }

  Var own_var() const override {
    return _array->own_var();
  }

  TypeId nested_type() const override {
    return _array->nested_type();
  }

  Expected<Var> at(size_t idx) override {
    return _array->at(idx);
  }

  Expected<Var> operator[](size_t idx) override {
    return _array->operator[](idx);
  }

  Expected<Var> front() override {
    return _array->front();
  };

  Expected<Var> back() override {
    return _array->back();
  };

  Expected<None> fill(Var filler) override {
    return _array->fill(filler);
  }

  void for_each(std::function<void(Var)> callback) const override {
    _array->for_each(callback);
  }

  void for_each(std::function<void(Var)> callback) override {
    _array->for_each(callback);
  }

  void unsafe_for_each(std::function<void(void*)> callback) const override {
    _array->unsafe_for_each(callback);
  }

  size_t size() const override {
    return _array->size();
  }

 private:
  std::shared_ptr<IArray> _array;
};

}  // namespace er
