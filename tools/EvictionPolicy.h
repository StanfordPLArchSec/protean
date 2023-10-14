#pragma once

#include <array>
#include <algorithm>

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
  class CompositeEvictionPolicy {
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


};
