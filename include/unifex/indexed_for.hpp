/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <functional>
#include <type_traits>

namespace unifex {

template <typename Predecessor, typename Range, typename Policy, typename Func>
struct indexed_for_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Range range_;
  UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <template <typename...> class Tuple>
  struct indexed_for_result {
   public:
    template <typename... Args>
    using apply = Tuple<Args...>;
  };

  template <typename... Args>
  using is_overload_noexcept = std::bool_constant<noexcept(
      std::invoke(std::declval<Func>(), std::declval<Args>()...))>;

  template <template <typename...> class Variant>
  struct calculate_errors {
   public:
    template <typename... Errors>
    using apply = std::conditional_t<
        Predecessor::
            template value_types<std::conjunction, is_overload_noexcept>::value,
        Variant<Errors...>,
        deduplicate_t<Variant<Errors..., std::exception_ptr>>>;
  };

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = deduplicate_t<typename Predecessor::template value_types<
      Variant,
      indexed_for_result<Tuple>::template apply>>;

  template <template <typename...> class Variant>
  using error_types = typename Predecessor::template error_types<
      calculate_errors<Variant>::template apply>;

  friend constexpr auto tag_invoke(
      tag_t<blocking>,
      const indexed_for_sender& sender) {
    return blocking(sender.pred_);
  }

  template <typename Receiver>
  struct indexed_for_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    // TODO: Use range and policy
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      if constexpr (noexcept(std::invoke(
                        (Func &&) func_, values...))) {
        std::invoke((Func &&) func_, values...);
        unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
      } else {
        try {
          std::invoke((Func &&) func_, values...);
          unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
        } catch (...) {
          unifex::set_error((Receiver &&) receiver_, std::current_exception());
        }
      }
    }

    template <typename Error>
    void set_error(Error&& error) && noexcept {
      unifex::set_error((Receiver &&) receiver_, (Error &&) error);
    }

    void set_done() && noexcept {
      unifex::set_done((Receiver &&) receiver_);
    }

    template <
        typename CPO,
        std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const indexed_for_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Visit>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const indexed_for_receiver& r,
        Visit&& visit) {
      std::invoke(visit, r.receiver_);
    }
  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return unifex::connect(
        std::forward<Predecessor>(pred_),
        indexed_for_receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Func>(func_), std::forward<Receiver>(receiver)});
  }
};

template <typename Sender, typename Range, typename Policy, typename Func>
auto indexed_for(Sender&& predecessor, Range&& range, Policy&& policy, Func&& func) {
  return indexed_for_sender<std::remove_cvref_t<Sender>, std::decay_t<Range>, std::decay_t<Policy>, std::decay_t<Func>>{
      (Sender &&) predecessor, (Range&& ) range, (Policy&&) policy, (Func &&) func};
}

} // namespace unifex
