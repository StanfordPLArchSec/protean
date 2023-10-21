#pragma once

#include <array>
#include <algorithm>
#include <limits>

#include "Util.h"

template <unsigned N, typename BV>
struct EvictionPolicies {

  class EvictionPolicy {
  public:
    virtual void allocated(unsigned idx, const BV& bv) {}
    virtual void checkDeclassifiedMiss(unsigned idx, const BV& bv) {}
    virtual void checkDeclassifiedHit(unsigned idx, const BV& bv) {}
    virtual void setDeclassifiedPre(unsigned idx, const BV& bv) {}
    virtual void setDeclassifiedPost(unsigned idx, const BV& bv) {}
    virtual void setClassifiedPre(unsigned idx, const BV& bv) {}
    virtual void setClassifiedPost(unsigned idx, const BV& bv) {}
    virtual int score(unsigned idx, const BV& bv) = 0;
  };


  class LRUEvictionPolicy final : public EvictionPolicy {
  public:
    LRUEvictionPolicy() {
      std::fill(lru.begin(), lru.end(), 0);
    }

    using Tick = unsigned long;

  private:
    void markUsed(unsigned idx) {
      ++tick;
      lru[idx] = tick;
    }

  public:
    void allocated(unsigned idx, const BV& bv) override {
      markUsed(idx);
    }
  
    void checkDeclassifiedHit(unsigned idx, const BV& bv) override {
      markUsed(idx);
    }

    int score(unsigned idx, const BV& bv) override {
      return lru[idx];
    }

  private:
    Tick tick = 0;
    std::array<unsigned, N> lru;
  };


  class PopCntEvictionPolicy final : public EvictionPolicy {
  public:
    int score(unsigned idx, const BV& bv) override {
      return std::count(bv.begin(), bv.end(), true);
    }
  };


  class NRUEvictionPolicy final : public EvictionPolicy {
  public:
    NRUEvictionPolicy() = default;
    NRUEvictionPolicy(unsigned start_value): startValue(start_value) {
      std::fill(nru.begin(), nru.end(), 0);
    }

  private:
    void markUsed(unsigned idx) {
      for (unsigned i = 0; i < N; ++i) {
	if (i == idx) { 
	  nru[i] = startValue;
	} else {
	  nru[i] = std::max(nru[i], 1U) - 1;
	}
      }
    }

  public:
    void allocated(unsigned idx, const BV& bv) override {
      markUsed(idx);
    }

    void checkDeclassifiedHit(unsigned idx, const BV& bv) override {
      markUsed(idx);
    }

    int score(unsigned idx, const BV&) override {
      return nru[idx];
    }
    
  private:
    unsigned startValue;
    std::array<unsigned, N> nru;
  };


  class LRVC final : public EvictionPolicy {
  private:
    using Tick = unsigned;

    static unsigned popcnt(const BV& bv) {
      return std::count(bv.begin(), bv.end(), true);
    }

    void markUpdated(unsigned idx) {
      ++tick;
      updated[idx] = tick;
    }

  public:
    LRVC() {
      std::fill(updated.begin(), updated.end(), tick);
    }
    
    void setDeclassifiedPre(unsigned idx, const BV& bv) override {
      popcntTmp = popcnt(bv);
    }

    void setDeclassifiedPost(unsigned idx, const BV& bv) override {
      if (popcntTmp != popcnt(bv))
	markUpdated(idx);
    }

    void setClassifiedPre(unsigned idx, const BV& bv) override {
      popcntTmp = popcnt(bv);
    }

    void setClassifiedPost(unsigned idx, const BV& bv) override {
      if (popcntTmp != popcnt(bv))
	markUpdated(idx);
    }

    int score(unsigned idx, const BV&) override {
      return updated[idx];
    }
    
  private:
    Tick tick = 0;
    std::array<Tick, N> updated;
    unsigned popcntTmp;
  };


  template <typename ScoreFunc, class... Policies>
  class CompositeEvictionPolicy : public EvictionPolicy {
  public:
    CompositeEvictionPolicy(ScoreFunc score_func, const Policies&... policies): scoreFunc(score_func), policies(std::make_tuple(policies...)) {}
    CompositeEvictionPolicy() = default;

  private:
    template <typename Func, size_t I = 0>
    void for_each_policy(Func func) {
      func(I, std::get<I>(policies));
      if constexpr (I+1 < sizeof...(Policies)) {
	for_each_policy<Func, I+1>(func);
      }
    }

  public:
    void allocated(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.allocated(idx, bv);
      });
    }

    void checkDeclassifiedMiss(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.checkDeclassifiedMiss(idx, bv);
      });
    }

    void checkDeclassifiedHit(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.checkDeclassifiedHit(idx, bv);
      });
    }

    void setDeclassifiedPre(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.setDeclassifiedPre(idx, bv);
      });
    }

    void setDeclassifiedPost(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.setDeclassifiedPost(idx, bv);
      });
    }

    void setClassifiedPre(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.setClassifiedPre(idx, bv);
      });
    }

    void setClassifiedPost(unsigned idx, const BV& bv) {
      for_each_policy([&] (size_t, auto& policy) {
	policy.setClassifiedPost(idx, bv);
      });
    }
  
    int score(unsigned idx, const BV& bv) {
      const int score = std::apply([&] (auto&... policies) {
	return scoreFunc(policies.score(idx, bv)...);
      }, policies);
      return score;
    }
  
  private:
    ScoreFunc scoreFunc;
    std::tuple<Policies...> policies;
  };


  class PatternEvictionPolicy final : public EvictionPolicy {
  private:
    unsigned max_pat_len;
  public:
    PatternEvictionPolicy() = default;
    PatternEvictionPolicy(unsigned max_pat_len): max_pat_len(max_pat_len) {}
    
    int score(unsigned idx, const BV& bv) override {
      std::vector<bool> pattern;
      if (hasPattern(bv.begin(), bv.end(), std::back_inserter(pattern), max_pat_len)) {
	return std::numeric_limits<int>::min();
      } else {
	return std::numeric_limits<int>::max();
      }
    }
  };

  class AllSetEvictionPolicy final : public EvictionPolicy {
  public:
    int score(unsigned idx, const BV& bv) override {
      if (std::reduce(bv.begin(), bv.end(), true, std::logical_and<bool>())) {
	return std::numeric_limits<int>::min();
      } else {
	return std::numeric_limits<int>::max();
      }
    }
  };

  template <class Policy1, class Policy2>
  class MinEvictionPolicy final : public CompositeEvictionPolicy<int (*) (int, int), Policy1, Policy2> {
  private:
    static int handle(int score1, int score2) {
      return std::min(score1, score2);
    }
  public:
    MinEvictionPolicy() = default;
    MinEvictionPolicy(const Policy1& p1, const Policy2& p2): CompositeEvictionPolicy<int (*) (int, int), Policy1, Policy2>(&handle, p1, p2) {}
  };

  // Functors
  struct LRU_PopCnt_Functor {
    int lru_scale;
    int popcnt_scale;

    LRU_PopCnt_Functor() = default;
    LRU_PopCnt_Functor(int lru_scale, int popcnt_scale): lru_scale(lru_scale), popcnt_scale(popcnt_scale) {}
    
    int operator()(int lru, int popcnt) const {
      return lru * lru_scale + popcnt * popcnt_scale;
    }
  };


  class LFU final : public EvictionPolicy {
  public:
    using Counter = unsigned;

    LFU() = default;
    LFU(Counter max): max(max) {}
    
    void allocated(unsigned idx, const BV& bv) override {
      lfu[idx] = N;
    }

    void checkDeclassifiedHit(unsigned idx, const BV& bv) override {
      for (unsigned i = 0; i < N; ++i) {
	if (i == idx) {
	  lfu[i] += N;
	  if (lfu[i] > max)
	    lfu[i] = max;
	} else {
	  if (lfu[i] > 0) {
	    lfu[i] -= 1;
	  }
	}
      }
    }

    int score(unsigned idx, const BV& bv) override {
      return lfu[idx];
    }

  private:
    Counter max;
    std::array<Counter, N> lfu;
  };




  class RRIP final : public EvictionPolicy {
  public:
    RRIP() = default;
    RRIP(size_t interval): interval(interval) {}
    
    void allocated(unsigned idx, const BV& bv) override {
      values[idx] = popcnt(bv) * interval;
    }

    void setDeclassifiedPost(unsigned idx, const BV& bv) override {
      values[idx] = std::min(maxValue(), values[idx] + 1);
    }

    void checkDeclassifiedMiss(unsigned idx, const BV& bv) override {
      values[idx] = std::min(maxValue(), values[idx] + interval);
    }

    void checkDeclassifiedHit(unsigned idx, const BV& bv) override {
      values[idx] = std::min(maxValue(), values[idx] + 1);
    }

    void setClassifiedPost(unsigned idx, const BV& bv) override {
      values[idx] = std::max(values[idx], interval) - interval;
    }

    int score(unsigned idx, const BV& bv) override {
      const auto min_it = std::min_element(values.begin(), values.end());
      const auto min_val = *min_it;
      if (min_val > 0) {
	for (size_t& value : values) {
	  value -= min_val;
	}
      }
      return values[idx];
    }

    
  private:
    size_t interval;
    std::array<size_t, N> values;

    size_t maxValue() const {
      return interval * 64;
    }
  };
  
};
