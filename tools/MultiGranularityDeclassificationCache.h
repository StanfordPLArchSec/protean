#pragma once

#define LOG_EVICTIONS 0

template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned Scale_>
class MultiGranularityDeclassificationCache {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static_assert((TableRows & (TableRows - 1)) == 0, "");
