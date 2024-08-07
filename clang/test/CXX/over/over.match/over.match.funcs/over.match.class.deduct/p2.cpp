// RUN: %clang_cc1 -verify -std=c++1z %s

namespace Explicit {
  // Each notional constructor is explicit if the function or function template
  // was generated from a constructor or deduction-guide that was declared explicit.
  template<typename T> struct A {
    A(T);
    A(T*);
	A(...);
  };
  template<typename T> A(T) -> A<T>;
  template<typename T> explicit A(T*) -> A<T**>; // expected-note {{explicit}}

  int *p;
  A a(p);
  A b = p;
  A c{p};
  A d = {p}; // expected-error {{selected an explicit deduction guide}}

  using X = A<int**>;
  using Y = A<int>;  // uses the implicit guide, being more specialized than the eligible user-defined deduction guides.

  using X = decltype(a);
  using Y = decltype(b);
  using X = decltype(c);
}


namespace std {
  template<typename T> struct initializer_list {
    const T *ptr;
    __SIZE_TYPE__ size;
    initializer_list();
  };
}

namespace p0702r1 {
  template<typename T> struct X { // expected-note {{candidate}} expected-note {{implicit deduction guide}}
    X(std::initializer_list<T>); // expected-note {{candidate template ignored: could not match 'std::initializer_list<T>' against 'Z'}} \
                                 // expected-note {{implicit deduction guide declared as 'template <typename T> X(std::initializer_list<T>) -> X<T>'}}
  };

  X xi = {0};
  X xxi = {xi};
  extern X<int> xi;
  // Prior to P0702R1, this is X<X<int>>.
  extern X<int> xxi;

  struct Y : X<int> {};
  Y y {{0}};
  X xy {y};
  extern X<int> xy;

  struct Z : X<int>, X<float> {};
  Z z = {{0}, {0.0f}};
  // This is not X<Z> even though that would work. Instead, it's ambiguous
  // between X<int> and X<float>.
  X xz = {z}; // expected-error {{no viable constructor or deduction guide}}
}
namespace pr34970 {
//https://bugs.llvm.org/show_bug.cgi?id=34970

template <typename X, typename Y> struct IsSame {
    static constexpr bool value = false;
};

template <typename Z> struct IsSame<Z, Z> {
    static constexpr bool value = true;
};

template <typename T> struct Optional {
    template <typename U> Optional(U&&) { }
};

template <typename A> Optional(A) -> Optional<A>;

int main() {
    Optional opt(1729);
    Optional dupe(opt);

    static_assert(IsSame<decltype(opt), Optional<int>>::value);
    static_assert(IsSame<decltype(dupe), Optional<int>>::value);
    static_assert(!IsSame<decltype(dupe), Optional<Optional<int>>>::value);
	return 0;
}


}

namespace deduceTemplatedConstructor {
template <typename X, typename Y> struct IsSame {
    static constexpr bool value = false;
};

template <typename Z> struct IsSame<Z, Z> {
    static constexpr bool value = true;
};
template <class T> struct A {
  using value_type = T;
  A(value_type);
  A(const A&);
  A(T, T, int);
  template<class U>
  A(int, T, U);
};

A x(1, 2, 3);       // no-error
static_assert(IsSame<decltype(x),A<int>>::value);

template <class T>
A(T) -> A<T>;

A a(42);
static_assert(IsSame<decltype(a),A<int>>::value);
A b = a;
static_assert(IsSame<decltype(b),A<int>>::value);

template <class T>
A(A<T>) -> A<A<T>>;

A b2 = a;
static_assert(IsSame<decltype(b2),A<A<int>>>::value);
}
