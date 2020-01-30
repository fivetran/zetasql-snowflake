//
// Copyright 2019 ZetaSQL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "zetasql/public/numeric_value.h"

#include <stdlib.h>

#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "zetasql/common/fixed_int.h"
#include "zetasql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "zetasql/base/endian.h"
#include "zetasql/base/canonical_errors.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"

namespace std {
std::ostream& operator<<(std::ostream& o, __int128 x) {
  return x < 0 ? (o << '-' << -absl::uint128(x)) : (o << absl::uint128(x));
}
}  // namespace std

namespace zetasql {

namespace {

constexpr absl::string_view kInvalidSerializedAggregators[] = {
    "",
    "A VERY LONG INVALID ENCODING",
};

using zetasql_base::testing::StatusIs;

constexpr absl::string_view kMinNumericValueStr =
    "-99999999999999999999999999999.999999999";
constexpr absl::string_view kMaxNumericValueStr =
    "99999999999999999999999999999.999999999";
constexpr __int128 kMinNumericValuePacked =
    NumericValue::MinValue().as_packed_int();
constexpr __int128 kMaxNumericValuePacked =
    NumericValue::MaxValue().as_packed_int();
constexpr __int128 k1e9 = 1000000000;
constexpr __int128 k1e10 = k1e9 * 10;
constexpr int64_t kint64min = std::numeric_limits<int64_t>::min();
constexpr int64_t kint64max = std::numeric_limits<int64_t>::max();
using uint128 = unsigned __int128;
constexpr uint128 kuint128max = ~static_cast<uint128>(0);
constexpr __int128 kint128max = kuint128max >> 1;
constexpr __int128 kint128min = ~kint128max;

class NumericValueTest : public testing::Test {
 protected:
  NumericValueTest() {}

  inline NumericValue MkNumeric(const std::string& str) {
    return NumericValue::FromStringStrict(str).ValueOrDie();
  }

  // Generates a random valid numeric value.
  NumericValue MakeRandomNumeric() {
    int32_t sign = absl::Uniform<int32_t>(random_, 0, 2);
    int32_t int_digits = absl::Uniform<int32_t>(random_, 0, 30);
    int32_t fract_digits = absl::Uniform<int32_t>(random_, 0, 10);

    std::string str(sign ? "-" : "");
    if (int_digits > 0) {
      for (int i = 0; i < int_digits; ++i) {
        str.push_back(static_cast<char>(absl::Uniform<int32_t>(random_, 0, 10)) +
                      '0');
      }
    } else {
      str.push_back('0');
    }

    if (fract_digits > 0) {
      str.push_back('.');
      for (int i = 0; i < fract_digits; ++i) {
        str.push_back(static_cast<char>(absl::Uniform<int32_t>(random_, 0, 10)) +
                      '0');
      }
    }

    return MkNumeric(str);
  }

  template <typename T>
  void TestRoundTripFromInteger() {
    for (int i = 0; i < 1000; ++i) {
      T from_integer = absl::Uniform<std::make_unsigned_t<T>>(random_);
      NumericValue numeric_value(from_integer);
      ZETASQL_ASSERT_OK_AND_ASSIGN(T to_integer, numeric_value.To<T>());
      EXPECT_EQ(from_integer, to_integer) << numeric_value.ToString();
    }
  }

  void VerifyVariance(absl::optional<double> expect_var,
                        absl::optional<double> actual_var) {
    ASSERT_EQ(expect_var.has_value(), actual_var.has_value());
    if (expect_var.has_value()) {
      EXPECT_DOUBLE_EQ(expect_var.value(), actual_var.value());
    }
  }

  void VerifyStandardDeviation(absl::optional<double> expect_var,
                               absl::optional<double> actual_stddev) {
    ASSERT_EQ(expect_var.has_value(), actual_stddev.has_value());
    if (expect_var.has_value()) {
      EXPECT_DOUBLE_EQ(std::sqrt(expect_var.value()), actual_stddev.value());
    }
  }

  void TestVarianceAggregator(const NumericValue::VarianceAggregator& agg,
                              absl::optional<double> expect_var_pop,
                              absl::optional<double> expect_var_samp,
                              uint64_t count) {
    VerifyVariance(expect_var_pop, agg.GetPopulationVariance(count));
    VerifyStandardDeviation(expect_var_pop, agg.GetPopulationStdDev(count));
    VerifyVariance(expect_var_samp, agg.GetSamplingVariance(count));
    VerifyStandardDeviation(expect_var_samp, agg.GetSamplingStdDev(count));
  }

  void TestCovariance(const NumericValue::CovarianceAggregator& agg,
                      absl::optional<double> expect_covar_pop,
                      absl::optional<double> expect_covar_samp,
                      uint64_t count) {
    VerifyVariance(expect_covar_pop, agg.GetPopulationCovariance(count));
    VerifyVariance(expect_covar_samp, agg.GetSamplingCovariance(count));
  }

  void TestCorrelation(const NumericValue::CorrelationAggregator& agg,
                       absl::optional<double> expect,
                       uint64_t count) {
    absl::optional<double> actual(agg.GetCorrelation(count));
    ASSERT_EQ(expect.has_value(), actual.has_value());
    if (expect.has_value()) {
      EXPECT_EQ(std::isnan(expect.value()), std::isnan(actual.value()));
      if (!std::isnan(expect.value())) {
        EXPECT_DOUBLE_EQ(expect.value(), actual.value());
      }
    }
  }

  void TestSerialize(NumericValue value) {
    std::string bytes = value.SerializeAsProtoBytes();
    NumericValue deserialized = NumericValue::DeserializeFromProtoBytes(
        bytes).ValueOrDie();
    EXPECT_EQ(value, deserialized);
  }

  absl::BitGen random_;
};


TEST_F(NumericValueTest, FromPackedInt) {
  static constexpr __int128 kValidValues[] = {0,
                                              -1,
                                              1,
                                              -10000,
                                              10000,
                                              kint64min,
                                              kint64max,
                                              kMinNumericValuePacked,
                                              kMaxNumericValuePacked};
  for (__int128 packed : kValidValues) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromPackedInt(packed));
    EXPECT_EQ(packed, value.as_packed_int());
  }

  constexpr unsigned __int128 kuint128max = ~static_cast<unsigned __int128>(0);
  constexpr __int128 kint128max = kuint128max >> 1;
  constexpr __int128 kint128min = ~kint128max;
  static constexpr __int128 kInvalidValues[] = {
      kMinNumericValuePacked - 1,
      kMaxNumericValuePacked + 1,
      kint128min,
      kint128max,
  };
  for (__int128 packed : kInvalidValues) {
    EXPECT_THAT(NumericValue::FromPackedInt(packed),
                StatusIs(zetasql_base::OUT_OF_RANGE,
                         "numeric overflow: result out of range"));
  }
}

TEST_F(NumericValueTest, FromHighAndLowBits) {
  constexpr uint64_t max_high = NumericValue::MaxValue().high_bits();
  constexpr uint64_t max_low = NumericValue::MaxValue().low_bits();
  constexpr uint64_t min_high = NumericValue::MinValue().high_bits();
  constexpr uint64_t min_low = NumericValue::MinValue().low_bits();
  constexpr uint64_t kuint64max = std::numeric_limits<uint64_t>::max();
  static constexpr std::pair<uint64_t, uint64_t> kValidValues[] = {
      {0, 0},
      {0, 1},
      {0, kuint64max},
      {1, kuint64max},
      {max_high - 1, kuint64max},
      {max_high, 0},
      {max_high, max_low - 1},
      {max_high, max_low},
      {min_high, min_low},
      {min_high, min_low + 1},
      {min_high, kuint64max},
      {min_high + 1, 0},
      {kuint64max, 0},
      {kuint64max, kuint64max},
  };
  for (std::pair<uint64_t, uint64_t> bits : kValidValues) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value, NumericValue::FromHighAndLowBits(
                                                 bits.first, bits.second));
    EXPECT_EQ(bits.first, value.high_bits());
    EXPECT_EQ(bits.second, value.low_bits());
  }

  static constexpr std::pair<uint64_t, uint64_t> kInvalidValues[] = {
      {max_high, max_low + 1}, {max_high, kuint64max},
      {max_high + 1, 0},       {max_high + 1, kuint64max},
      {min_high - 1, 0},       {min_high - 1, kuint64max},
      {min_high, 0},           {min_high, min_low - 1},
  };
  for (std::pair<uint64_t, uint64_t> bits : kInvalidValues) {
    EXPECT_THAT(NumericValue::FromHighAndLowBits(bits.first, bits.second),
                StatusIs(zetasql_base::OUT_OF_RANGE,
                         "numeric overflow: result out of range"));
  }
}

using NumericStringTestData = std::pair<__int128, absl::string_view>;
constexpr NumericStringTestData kToStringTestData[] = {
    {0, "0"},
    {kMaxNumericValuePacked, kMaxNumericValueStr},
    {kMinNumericValuePacked, kMinNumericValueStr},
    {1, "0.000000001"},
    {10, "0.00000001"},
    {123456789, "0.123456789"},
    {123000000, "0.123"},
    {1234567890LL, "1.23456789"},
    {12345678901LL, "12.345678901"},
    {-1, "-0.000000001"},
    {-10, "-0.00000001"},
    {-123456789, "-0.123456789"},
    {-123000000, "-0.123"},
    {-1234567890LL, "-1.23456789"},
    {-12345678901LL, "-12.345678901"},
    {k1e9, "1"},
    {10 * k1e9, "10"},
    {123 * k1e9, "123"},
    {-k1e9, "-1"},
    {-10 * k1e9, "-10"},
    {-123 * k1e9, "-123"},
    {std::numeric_limits<int64_t>::min() * k1e9, "-9223372036854775808"},
    {std::numeric_limits<int64_t>::max() * k1e9, "9223372036854775807"},
    {std::numeric_limits<uint64_t>::max() * k1e9, "18446744073709551615"},
    {9997 * k1e9, {"9997.", 4}},  // a regression test for b/77498186
};

constexpr NumericStringTestData kNumericFromStringTestData[] = {
    {0, "00"},
    {0, "0.0"},
    {0, "00.000"},
    {123 * k1e9, "+123"},
    {123 * k1e9, "123.0"},
    {123 * k1e9, "123."},
    {123 * k1e9, "+123.0"},
    {-123 * k1e9, "-123.0"},
    {1, "+0.000000001"},
    {123000000, ".123"},
    {123000000, "+.123"},
    {-123000000, "-.123"},

    // The white space is ignored.
    {0, " 0"},
    {0, "0 "},
    {0, " 0 "},

    // Non-essential zeroes are ignored.
    {0, "00000000000000000000000000000000000000000000000000"},
    {0, "-00000000000000000000000000000000000000000000000000"},
    {0, "+00000000000000000000000000000000000000000000000000"},
    {0, ".00000000000000000000000000000000000000000000000000"},
    {0, "-.00000000000000000000000000000000000000000000000000"},
    {0, "+.00000000000000000000000000000000000000000000000000"},
    {0, "00000000000000000000000000000000.0000000000000000000"},
    {0, "-00000000000000000000000000000000.0000000000000000000"},
    {0, "+00000000000000000000000000000000.0000000000000000000"},
    {12334 * k1e9 / 100,
     "0000000000000000000000000000000123.340000000000000000000000"},
    {12334 * k1e9 / 100,
     "+0000000000000000000000000000000123.340000000000000000000000"},
    {-12334 * k1e9 / 100,
     "-0000000000000000000000000000000123.340000000000000000000000"},

    // Exponent form.
    {123000 * k1e9, "123e3"},
    {123000 * k1e9, "123E3"},
    {123000 * k1e9, "123.e3"},
    {123000 * k1e9, "123e+3"},
    {123000 * k1e9, "123000e0"},
    {123000 * k1e9, "123000e00"},
    {123000 * k1e9, "123000E+00"},
    {123000 * k1e9, "1230000e-001"},
    {123000000, "123e-3"},
    {123000000, "123E-3"},
    {123000 * k1e9, "0000000000000000000000000123e3"},
    {123000 * k1e9, "0000000000000000000000000123.000000000000000e3"},
    {-123000 * k1e9, "-0000000000000000000000000123e3"},
    {-123000 * k1e9, "-0000000000000000000000000123.000000000000000e3"},
    {1230000000 * k1e9, ".123e10"},
    {11234567890 * k1e9, "1.1234567890E10"},
    {((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345678,
     "0.12345678901234567890123456789012345678e+29"},
    {((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345678,
     "-0.12345678901234567890123456789012345678e+29"},
    {((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345678,
     "12345678901234567890123456789012345678e-9"},
    {((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345678,
     "-12345678901234567890123456789012345678e-9"},
    {kMaxNumericValuePacked,
     "0.999999999999999999999999999999999999990000000000000000000000000"
     "e0000000000000000000000000000000000000000000000000000000000000029"},
    {kMinNumericValuePacked,
     "-0.999999999999999999999999999999999999990000000000000000000000000"
     "E0000000000000000000000000000000000000000000000000000000000000029"},
    {kMaxNumericValuePacked, "0.99999999999999999999999999999999999999e+29"},
    {kMinNumericValuePacked, "-0.99999999999999999999999999999999999999e+29"},
    {kMaxNumericValuePacked,
     "9999999999999999999999999999999999999900000000000000000000e-29"},
    {kMinNumericValuePacked,
     "-9999999999999999999999999999999999999900000000000000000000e-29"},
    // exponent below int64min
    {0, "0E-99999999999999999999999999999999999999999999999999999999999"},
    {0,
     "-0.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999"},
    {0,
     "+000000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999"},
    {0,
     "   -.00000000000000000000000000000000000000000000000000000000000"
     "E-99999999999999999999999999999999999999999999999999999999999   "},
};

constexpr NumericStringTestData kNumericFromStringRoundingTestData[] = {
    {123456789, "0.1234567891"},
    {123456789, "0.123456789123456789"},
    {123456789, "0.1234567894"},
    {123456789, "0.1234567885"},
    {123456789, "0.123456788555"},
    {123456789, "0.1234567889"},
    {-123456789, "-0.1234567891"},
    {-123456789, "-0.123456789123456789"},
    {-123456789, "-0.1234567894"},
    {-123456789, "-0.1234567885"},
    {-123456789, "-0.123456788555"},
    {-123456789, "-0.1234567889"},

    {kMaxNumericValuePacked, "99999999999999999999999999999.9999999991"},
    {kMinNumericValuePacked, "-99999999999999999999999999999.9999999991"},

    // More tests for the exponential form.
    {1111111111,
     "1111111111111111111111111111111111111111111111111111111111111111111e-66"},
    {0, ".123e-10"},
    {0, ".123e-9999"},
    {((1234567890 * k1e10 + 1234567890) * k1e9 + 123456789) * k1e9 + 12345679,
     "123456789012345678901234567890123456789e-10"},
    {((-1234567890 * k1e10 - 1234567890) * k1e9 - 123456789) * k1e9 - 12345679,
     "-123456789012345678901234567890123456789e-10"},
    // exponent below int64min
    {0, "1E-99999999999999999999999999999999999999999999999999999999999"},
    {0, "-1E-99999999999999999999999999999999999999999999999999999999999"},
    {0,
     "99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999"},
    {0,
     "-99999999999999999999999999999999999999999999999999999999999"
     "E-99999999999999999999999999999999999999999999999999999999999"},
};

constexpr absl::string_view kInvalidNumericStrings[] = {
    "999999999999999999999999999999",
    "-999999999999999999999999999999",
    "100000000000000000000000000000",
    "-100000000000000000000000000000",
    "266666666666666666666666666666",
    "-266666666666666666666666666666",
    "26666666666666666666666666666600",
    "-26666666666666666666666666666600",
    "",
    "abcd",
    "- 123",
    "123abc",
    "123..456",
    "123.4.56",
    ".",
    "  ",
    "+",
    "-",
    "++1",
    "--1",
    "+-1",
    "-+1",
    "123e5.6",
    "345e+ 4",
    "123e",
    "e",
    "123e-",
    "123e+",
    "123e-+1",
    "123e+-1",
    "123ee+1",
    "123ee-1",
    "123e +1",
    "123e -1",
    ".e",
    "123e-9999e",
    "123e9999e",
    "123e99999999",
    "170141183460469231731687303715884105727",     // kint128max
    "170141183460469231731687303715884105727.0",   // kint128max
    "-170141183460469231731687303715884105728",    // kint128min
    "-170141183460469231731687303715884105728.0",  // kint128min
    // Exponent overflows.
    "1e9223372036854775808",
    "1e9223372036854775800",  // overflows when adding 9 to the exponent
    "0e-9999999999999999999999999999999999999999999999999ABC",
};

TEST_F(NumericValueTest, ToString) {
  for (const NumericStringTestData& pair : kToStringTestData) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromPackedInt(pair.first));
    EXPECT_EQ(pair.second, value.ToString());
    static constexpr char kExistingValue[] = "existing_value_1234567890";
    std::string str = kExistingValue;
    value.AppendToString(&str);
    EXPECT_EQ(absl::StrCat(kExistingValue, pair.second), str);
  }
}

TEST_F(NumericValueTest, FromString) {
  using FromStringFunc =
      std::function<zetasql_base::StatusOr<NumericValue>(absl::string_view)>;
  const FromStringFunc functions[] = {&NumericValue::FromStringStrict,
                                      &NumericValue::FromString};
  for (const auto& from_string : functions) {
    // Common successful cases under strict and non-strict parsing.
    for (const NumericStringTestData& pair : kToStringTestData) {
      ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value, from_string(pair.second));
      EXPECT_EQ(pair.first, value.as_packed_int()) << pair.second;
    }
    for (const NumericStringTestData& pair : kNumericFromStringTestData) {
      ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value, from_string(pair.second));
      EXPECT_EQ(pair.first, value.as_packed_int()) << pair.second;
    }

    // Test common failures for the strict and non-strict parsing.
    for (const absl::string_view input : kInvalidNumericStrings) {
      EXPECT_THAT(from_string(input),
                  StatusIs(zetasql_base::OUT_OF_RANGE,
                           absl::StrCat("Invalid NUMERIC value: ", input)));
    }
  }

  for (const NumericStringTestData& pair : kNumericFromStringRoundingTestData) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value,
                         NumericValue::FromString(pair.second));
    EXPECT_EQ(pair.first, value.as_packed_int()) << pair.second;

    EXPECT_THAT(NumericValue::FromStringStrict(pair.second),
                StatusIs(zetasql_base::OUT_OF_RANGE,
                         absl::StrCat("Invalid NUMERIC value: ", pair.second)));
  }
}

// A lite version of Status that allows instantiation with constexpr.
struct Error : absl::string_view {
  constexpr explicit Error(absl::string_view message_prefix)
      : absl::string_view(message_prefix) {}
};

constexpr Error kNumericOverflow("numeric overflow: ");
constexpr Error kNumericOutOfRange("numeric out of range: ");
constexpr Error kDivisionByZero("division by zero: ");
constexpr Error kNumericIllegalNonFinite(
    "Illegal conversion of non-finite floating point number to numeric: ");

// A lite version of StatusOr that allows instantiation with constexpr.
template <typename T>
using ErrorOr = absl::variant<Error, T>;

struct NumericValueWrapper : absl::variant<Error, absl::string_view, int64_t> {
  using absl::variant<Error, absl::string_view, int64_t>::variant;
  bool negate = false;
};

template <typename T>
zetasql_base::StatusOr<T> GetNumericValue(const NumericValueWrapper& src) {
  T value;
  if (absl::holds_alternative<absl::string_view>(src)) {
    ZETASQL_ASSIGN_OR_RETURN(value, T::FromString(absl::get<absl::string_view>(src)));
  } else if (absl::holds_alternative<int64_t>(src)) {
    value = T(absl::get<int64_t>(src));
  } else {
    return MakeEvalError() << absl::get<Error>(src);
  }
  if (src.negate) {
    return T::UnaryMinus(value);
  }
  return value;
}

NumericValueWrapper operator-(const NumericValueWrapper& src) {
  NumericValueWrapper result = src;
  result.negate = !result.negate;
  return result;
}

template <typename T>
zetasql_base::StatusOr<T> GetValue(const T& src) {
  return src;
}

template <typename T>
zetasql_base::StatusOr<T> GetValue(const ErrorOr<T>& src) {
  if (absl::holds_alternative<T>(src)) {
    return absl::get<T>(src);
  }
  return MakeEvalError() << absl::get<Error>(src);
}

template <typename T>
zetasql_base::StatusOr<T> GetValue(const zetasql_base::StatusOr<T>& src) {
  return src;
}

zetasql_base::StatusOr<NumericValue> GetValue(const NumericValueWrapper& src) {
  return GetNumericValue<NumericValue>(src);
}

// Returns a value that can be used in absl::StrCat.
template <typename T>
const T& AlphaNum(const T& src) {
  return src;
}

std::string AlphaNum(const NumericValue& src) { return src.ToString(); }

template <typename Input = NumericValueWrapper,
          typename Output = NumericValueWrapper>
struct NumericUnaryOpTestData {
  Input input;
  Output expected_output;
};

template <typename Input2 = NumericValueWrapper>
struct NumericBinaryOpTestData {
  NumericValueWrapper input1;
  Input2 input2;
  NumericValueWrapper expected_output;
};

struct NumericAddOp {
  template <class T>
  zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Add(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 + $1";
};

struct NumericSubtractOp {
  template <class T>
  zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Subtract(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 - $1";
};

struct NumericMultiplyOp {
  template <class T>
  zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Multiply(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 * $1";
};

struct NumericDivideOp {
  template <class T>
  zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Divide(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 / $1";
};

struct NumericModOp {
  template <class T>
  zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Mod(y);
  }
  static constexpr absl::string_view kExpressionFormat = "Mod($0, $1)";
};

struct NumericIntegerDivideOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.IntegerDivide(y);
  }
  static constexpr absl::string_view kExpressionFormat = "$0 / $1";
};

struct NumericPowerOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x, const T& y) const {
    return x.Power(y);
  }
  static constexpr absl::string_view kExpressionFormat = "POW($0, $1)";
};

struct NumericTruncOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x, int64_t y) const {
    return x.Trunc(y);
  }
  static constexpr absl::string_view kExpressionFormat = "TRUNC($0, $1)";
};

struct NumericRoundOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x, int64_t y) const {
    return x.Round(y);
  }
  static constexpr absl::string_view kExpressionFormat = "ROUND($0, $1)";
};

struct NumericFloorOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x) const {
    return x.Floor();
  }
  static constexpr absl::string_view kExpressionFormat = "FLOOR($0)";
};

struct NumericCeilingOp {
  template <class T>
  inline zetasql_base::StatusOr<T> operator()(const T& x) const {
    return x.Ceiling();
  }
  static constexpr absl::string_view kExpressionFormat = "CEIL($0)";
};

template <typename T>
struct NumericFromDoubleOp {
  zetasql_base::StatusOr<T> operator()(double operand) const {
    return T::FromDouble(operand);
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

struct NumericToDoubleOp {
  template <typename T>
  double operator()(const T& operand) const {
    return operand.ToDouble();
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

template <typename Output>
struct NumericToIntegerOp {
  template <typename T>
  zetasql_base::StatusOr<Output> operator()(const T& operand) const {
    return operand.template To<Output>();
  }
  static constexpr absl::string_view kExpressionFormat = "$0";
};

template <typename Op, typename Input, typename Output>
void TestUnaryOp(Op op, const Input& input_wrapper,
                 const Output& expected_output) {
  ZETASQL_ASSERT_OK_AND_ASSIGN(auto input, GetValue(input_wrapper));
  auto status_or_result = GetValue(op(input));
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(auto actual_output, status_or_result);
    EXPECT_EQ(status_or_expected_output.ValueOrDie(), actual_output)
        << absl::Substitute(Op::kExpressionFormat, AlphaNum(input));
  } else {
    std::string expression =
        absl::Substitute(Op::kExpressionFormat, AlphaNum(input));
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(zetasql_base::OUT_OF_RANGE,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

template <typename Op, typename Input1, typename Input2, typename Output>
void TestBinaryOp(Op op, const Input1& input_wrapper1,
                  const Input2& input_wrapper2, const Output& expected_output) {
  ZETASQL_ASSERT_OK_AND_ASSIGN(auto input1, GetValue(input_wrapper1));
  ZETASQL_ASSERT_OK_AND_ASSIGN(auto input2, GetValue(input_wrapper2));
  auto status_or_result = GetValue(op(input1, input2));
  auto status_or_expected_output = GetValue(expected_output);
  if (status_or_expected_output.ok()) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(auto actual_output, status_or_result);
    EXPECT_EQ(status_or_expected_output.ValueOrDie(), actual_output)
        << absl::Substitute(Op::kExpressionFormat, AlphaNum(input1),
                            AlphaNum(input2));
  } else {
    std::string expression = absl::Substitute(
        Op::kExpressionFormat, AlphaNum(input1), AlphaNum(input2));
    EXPECT_THAT(
        status_or_result.status(),
        StatusIs(zetasql_base::OUT_OF_RANGE,
                 absl::StrCat(status_or_expected_output.status().message(),
                              expression)));
  }
}

TEST_F(NumericValueTest, Add) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      // The result is too large to fit into int128.
      {kMaxNumericValueStr, kMaxNumericValueStr, kNumericOverflow},
      // The result fits into int128 but exceeds the NUMERIC range.
      {kMaxNumericValueStr, 1, kNumericOverflow},

      {kMaxNumericValueStr, 0, kMaxNumericValueStr},
      {kMinNumericValueStr, kMaxNumericValueStr, 0},
      {0, 0, 0},
      {"0.000000001", "0.000000002", "0.000000003"},
      {"0.000000003", "-0.000000002", "0.000000001"},
      {1, 2, 3},
      {1, "0.000000001", "1.000000001"},
  };

  NumericAddOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Subtract) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      // The result is too large to fit into int128.
      {kMaxNumericValueStr, kMinNumericValueStr, kNumericOverflow},

      // The result fits into int128 but exceeds the NUMERIC range.
      {kMinNumericValueStr, 1, kNumericOverflow},

      {kMaxNumericValueStr, kMaxNumericValueStr, 0},
      {kMaxNumericValueStr, 0, kMaxNumericValueStr},
      {1, 1, 0},
      {3, 1, 2},
      {3, 5, -2},
      {3, -5, 8},
      {-1, 2, -3},
      {-1, -2, 1},
      {"0.000000001", "0.000000001", 0},
      {"0.000000003", "0.000000001", "0.000000002"},
      {"0.000000003", "0.000000005", "-0.000000002"},
      {"0.000000003", "-0.000000005", "0.000000008"},
      {"-0.000000001", "0.000000002", "-0.000000003"},
      {"-0.000000001", "-0.000000002", "0.000000001"},
  };

  NumericSubtractOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, -data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Multiply) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {0, 0, 0},
      {0, 2, 0},
      {0, kMaxNumericValueStr, 0},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {6, "0.5", 3},
      {4294967296, 4294967296, "18446744073709551616"},
      {"2.5", "0.999999999",
       "2.499999998"},  // round 2.4999999975 > 2.499999998
      {"2.7", "0.999999999",
       "2.699999997"},  // round 2.6999999973 -> 2.699999997
      {"99999999.98", "1000000000100000000010.000000001",
       "99999999989999999998999999999.9"},
      {"33333333333333333333333333333.333333333", 3, kMaxNumericValueStr},
      {kMaxNumericValueStr, kMaxNumericValueStr, kNumericOverflow},
      {kMaxNumericValueStr, "1.000000001", kNumericOverflow},
      // Overflow after rounding.
      {"99999999.99", "1000000000100000000010.000000001", kNumericOverflow},
      {"5e14", "2e14", kNumericOverflow},
  };

  NumericMultiplyOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, data.input2, data.input1, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, -data.input2, -data.input1, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, data.input2, -data.input1, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input2, data.input1, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Multiply_PowersOfTen) {
  std::string expected_str = "1";
  NumericValue positive(1);
  NumericValue negative(-1);
  for (int i = 0; i < 28; ++i) {
    expected_str.append("0");
    positive = positive.Multiply(NumericValue(10)).ValueOrDie();
    negative = negative.Multiply(NumericValue(10)).ValueOrDie();
    EXPECT_EQ(expected_str, positive.ToString());
    EXPECT_EQ(std::string("-") + expected_str, negative.ToString());
  }

  // Next multiplication will add 30th number and cause overflow.
  EXPECT_THAT(positive.Multiply(NumericValue(10)),
              StatusIs(zetasql_base::OUT_OF_RANGE,
                       "numeric overflow: 10000000000000000000000000000 * 10"));
  EXPECT_THAT(
      negative.Multiply(NumericValue(10)),
      StatusIs(zetasql_base::OUT_OF_RANGE,
               "numeric overflow: -10000000000000000000000000000 * 10"));

  positive = MkNumeric("0.1");
  negative = MkNumeric("-0.1");
  for (int i = 1; i < 9; ++i) {
    expected_str = std::string("0.") + std::string(i, '0') + "1";
    positive = positive.Multiply(MkNumeric("0.1")).ValueOrDie();
    negative = negative.Multiply(MkNumeric("0.1")).ValueOrDie();
    EXPECT_EQ(expected_str, positive.ToString());
    EXPECT_EQ(std::string("-") + expected_str, negative.ToString());
  }

  // With next multiplication the number becomes too small and gets rounded to
  // zero.
  EXPECT_EQ(NumericValue(), positive.Multiply(MkNumeric("0.1")).ValueOrDie());
  EXPECT_EQ(NumericValue(), negative.Multiply(MkNumeric("0.1")).ValueOrDie());
}

TEST_F(NumericValueTest, Multiply_AllPrecisionCombinations) {
  NumericValue n1 = MkNumeric("0.000000005");
  for (int i = -9; i < 28; ++i) {
    NumericValue n2 = MkNumeric("0.000000003");
    for (int j = -9; j < 28; ++j) {
      int res_prec = i + j;

      if (res_prec < -10) {
        // The result is too small to fit into 9 fractional digits, the result
        // is 0.
        EXPECT_EQ(NumericValue(), n1.Multiply(n2).ValueOrDie());
      } else if (res_prec == -10) {
        // The trailing "15" was rounded.
        EXPECT_EQ("0.000000002",
                  n1.Multiply(n2).ValueOrDie().ToString());
      } else if (res_prec > 27) {
        EXPECT_THAT(n1.Multiply(n2),
                    StatusIs(zetasql_base::OUT_OF_RANGE,
                             absl::StrCat("numeric overflow: ", n1.ToString(),
                                          " * ", n2.ToString())));
      } else {
        NumericValue res = n1.Multiply(n2).ValueOrDie();

        std::string expected_str = "15";
        if (res_prec < -1) {
          expected_str =
              absl::StrCat("0.", std::string(-res_prec - 2, '0'), "15");
        } else if (res_prec == -1) {
          expected_str = "1.5";
        } else if (res_prec > 0) {
          expected_str += std::string(res_prec, '0');
        }

        EXPECT_EQ(expected_str, res.ToString());
      }

      n2 = n2.Multiply(NumericValue(10)).ValueOrDie();
    }

    n1 = n1.Multiply(NumericValue(10)).ValueOrDie();
  }
}

TEST_F(NumericValueTest, UnaryMinus) {
  EXPECT_EQ(NumericValue(0), NumericValue::UnaryMinus(NumericValue(0)));
  EXPECT_EQ(NumericValue(-1), NumericValue::UnaryMinus(NumericValue(1)));
  EXPECT_EQ(NumericValue(2), NumericValue::UnaryMinus(NumericValue(-2)));
  EXPECT_EQ(NumericValue::MinValue(),
            NumericValue::UnaryMinus(NumericValue::MaxValue()));
  EXPECT_EQ(NumericValue::MaxValue(),
            NumericValue::UnaryMinus(NumericValue::MinValue()));
}

TEST_F(NumericValueTest, Abs) {
  EXPECT_EQ(NumericValue(0), NumericValue::Abs(NumericValue(0)));
  EXPECT_EQ(NumericValue(1), NumericValue::Abs(NumericValue(1)));
  EXPECT_EQ(NumericValue(2), NumericValue::Abs(NumericValue(-2)));
  EXPECT_EQ(NumericValue::MaxValue(),
            NumericValue::Abs(NumericValue::MaxValue()));
  EXPECT_EQ(NumericValue::MaxValue(),
            NumericValue::Abs(NumericValue::MinValue()));
}

TEST_F(NumericValueTest, Sign) {
  EXPECT_EQ(NumericValue(0), NumericValue::Sign(NumericValue(0)));
  EXPECT_EQ(NumericValue(1), NumericValue::Sign(NumericValue(1)));
  EXPECT_EQ(NumericValue(1), NumericValue::Sign(NumericValue(123)));
  EXPECT_EQ(NumericValue(1), NumericValue::Sign(NumericValue::MaxValue()));
  EXPECT_EQ(NumericValue(-1), NumericValue::Sign(NumericValue(-456)));
  EXPECT_EQ(NumericValue(-1), NumericValue::Sign(NumericValue::MinValue()));
}

TEST_F(NumericValueTest, UnalignedReadWrite) {
  std::unique_ptr<char[]> buffer(new char[100]);
  char* buffer_ptr = buffer.get() + 3;
  NumericValue* value = new (buffer_ptr) NumericValue;
  buffer_ptr += 20;
  NumericValue* max_value =
      new (buffer_ptr) NumericValue(NumericValue::MaxValue());

  EXPECT_EQ(NumericValue(static_cast<int64_t>(0)), *value);
  EXPECT_EQ(NumericValue(NumericValue::MaxValue()), *max_value);

  *value = NumericValue(static_cast<int64_t>(10));
  EXPECT_EQ(NumericValue(static_cast<int64_t>(10)), *value);
}

TEST_F(NumericValueTest, IsTriviallyDestructible) {
  // Verify that NumericValue has a trivial destructor and hence can have static
  // storage duration per (broken link).
  constexpr NumericValue kOne(static_cast<int64_t>(1));
  constexpr NumericValue kTwo(static_cast<uint64_t>(2));
  EXPECT_EQ(kOne, kOne);
  EXPECT_LT(kOne, kTwo);
}

TEST_F(NumericValueTest, SerializeSize) {
  std::string bytes = NumericValue(1).SerializeAsProtoBytes();
  EXPECT_EQ(4, bytes.size());
  bytes = NumericValue(-1).SerializeAsProtoBytes();
  EXPECT_EQ(4, bytes.size());
  bytes = NumericValue::MaxValue().SerializeAsProtoBytes();
  EXPECT_EQ(16, bytes.size());
  bytes = NumericValue::MinValue().SerializeAsProtoBytes();
  EXPECT_EQ(16, bytes.size());
  bytes = NumericValue().SerializeAsProtoBytes();
  EXPECT_EQ(1, bytes.size());
}

TEST_F(NumericValueTest, SerializeDeserializeProtoBytes) {
  static constexpr NumericValueWrapper kTestValues[] = {
      0, 1, "123.01", "0.000000001", "0.999999999", kMaxNumericValueStr};
  for (const NumericValueWrapper& value_wrapper : kTestValues) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(value_wrapper));
    TestSerialize(value);
    TestSerialize(NumericValue::UnaryMinus(value));
  }

  const int kTestIterations = 500;
  for (int i = 0; i < kTestIterations; ++i) {
    NumericValue value = MakeRandomNumeric();
    TestSerialize(value);
    TestSerialize(NumericValue::UnaryMinus(value));
  }
}

TEST_F(NumericValueTest, DeserializeProtoBytesFailures) {
  std::string bytes;

  EXPECT_THAT(NumericValue::DeserializeFromProtoBytes(bytes),
              StatusIs(zetasql_base::OUT_OF_RANGE,
                       "Invalid numeric encoding"));
  bytes.resize(17);
  EXPECT_THAT(NumericValue::DeserializeFromProtoBytes(bytes),
              StatusIs(zetasql_base::OUT_OF_RANGE,
                       "Invalid numeric encoding"));

  bytes.resize(16);
  bytes[15] = 0x7f;
  for (int i = 0; i < 15; ++i) {
    bytes[i] = 0xff;
  }
  EXPECT_THAT(NumericValue::DeserializeFromProtoBytes(bytes),
              StatusIs(zetasql_base::OUT_OF_RANGE,
                       ::testing::HasSubstr("numeric overflow: ")));
}

TEST_F(NumericValueTest, ToDouble) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, double>
      kTestData[] = {
          {0, 0},
          {"0.000000001", 0.000000001},
          {"0.999999999", 0.999999999},
          {"1.5", 1.5},
          {123, 123},
          {"123.5", 123.5},
          {"4503599627370496", 4503599627370496.0},
          {"8.12407667", 8.12407667},  // test round to even
          {"1974613819685343985664.0249533", 1974613819685343985664.0249533},
          {kMaxNumericValueStr, 1e29},
      };

  NumericToDoubleOp op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(NumericValueTest, ToDouble_RandomInputs) {
  for (int i = 0; i < 10000; ++i) {
    NumericValue v = MakeRandomNumeric();
    double expected;
    ASSERT_TRUE(absl::SimpleAtod(v.ToString(), &expected));
    // Use EXPECT_EQ instead of EXPECT_DOUBLE_EQ to ensure exactness.
    EXPECT_EQ(expected, v.ToDouble()) << v.ToString();
  }
}
static constexpr Error kInt32OutOfRange("int32 out of range: ");
template <typename T>
static constexpr NumericUnaryOpTestData<T, ErrorOr<int32_t>>
    kToInt32ValueTestData[] = {
        {0, 0},
        {123, 123},
        {-123, -123},
        {"123.56", 124},
        {"-123.56", -124},
        {"123.5", 124},
        {"-123.5", -124},
        {"123.46", 123},
        {"-123.46", -123},
        {2147483647, 2147483647},
        {-2147483648, -2147483648},
        {2147483648, kInt32OutOfRange},
        {-2147483649, kInt32OutOfRange},
        {"2147483647.499999999", 2147483647},
        {"-2147483648.499999999", -2147483648},
        {"2147483647.5", kInt32OutOfRange},
        {"-2147483648.5", kInt32OutOfRange},
        {kMaxNumericValueStr, kInt32OutOfRange},
        {kMinNumericValueStr, kInt32OutOfRange},
};

TEST_F(NumericValueTest, ToInt32) {
  NumericToIntegerOp<int32_t> op;
  for (const auto& data : kToInt32ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromInt32) {
  TestRoundTripFromInteger<int32_t>();
}

static constexpr Error kInt64OutOfRange("int64 out of range: ");
template <typename T>
static constexpr NumericUnaryOpTestData<T, ErrorOr<int64_t>>
    kToInt64ValueTestData[] = {
        {0, 0},
        {123, 123},
        {-123, -123},
        {"123.56", 124},
        {"-123.56", -124},
        {"123.5", 124},
        {"-123.5", -124},
        {"123.46", 123},
        {"-123.46", -123},
        {"9223372036854775807", 9223372036854775807LL},
        {"-9223372036854775808", -9223372036854775807LL - 1},
        {"9223372036854775807.499999999", 9223372036854775807LL},
        {"-9223372036854775808.499999999", -9223372036854775807LL - 1},
        {"9223372036854775807.5", kInt64OutOfRange},
        {"-9223372036854775808.5", kInt64OutOfRange},
        {"9223372036854775808", kInt64OutOfRange},
        {"-9223372036854775809", kInt64OutOfRange},
        {kMaxNumericValueStr, kInt64OutOfRange},
        {kMinNumericValueStr, kInt64OutOfRange},
};

TEST_F(NumericValueTest, ToInt64) {
  NumericToIntegerOp<int64_t> op;
  for (const auto& data : kToInt64ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromInt64) {
  TestRoundTripFromInteger<int64_t>();
}

static constexpr Error kUint32OutOfRange("uint32 out of range: ");
template <typename T>
static constexpr NumericUnaryOpTestData<T, ErrorOr<uint32_t>>
    kToUint32ValueTestData[] = {
        {0, 0},
        {123, 123},
        {"123.56", 124},
        {"123.5", 124},
        {"123.46", 123},
        {4294967295, 4294967295U},
        {"4294967295.499999999", 4294967295U},
        {"4294967295.5", kUint32OutOfRange},
        {4294967296, kUint32OutOfRange},
        {"-0.499999999", 0},
        {"-0.5", kUint32OutOfRange},
        {-1, kUint32OutOfRange},
        {kMaxNumericValueStr, kUint32OutOfRange},
        {kMinNumericValueStr, kUint32OutOfRange},
};

TEST_F(NumericValueTest, ToUint32) {
  NumericToIntegerOp<uint32_t> op;
  for (const auto& data : kToUint32ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromUint32) {
  TestRoundTripFromInteger<uint32_t>();
}

static constexpr Error kUint64OutOfRange("uint64 out of range: ");
template <typename T>
static constexpr NumericUnaryOpTestData<T, ErrorOr<uint64_t>>
    kToUint64ValueTestData[] = {
        {0, 0},
        {123, 123},
        {"123.56", 124},
        {"123.5", 124},
        {"123.46", 123},
        {"18446744073709551615", 18446744073709551615ull},
        {"18446744073709551615.499999999", 18446744073709551615ull},
        {"18446744073709551615.5", kUint64OutOfRange},
        {"18446744073709551616", kUint64OutOfRange},
        {"-0.499999999", 0},
        {"-0.5", kUint64OutOfRange},
        {-1, kUint64OutOfRange},
        {kMaxNumericValueStr, kUint64OutOfRange},
        {kMinNumericValueStr, kUint64OutOfRange},
};

TEST_F(NumericValueTest, ToUint64) {
  NumericToIntegerOp<uint64_t> op;
  for (const auto& data : kToUint64ValueTestData<NumericValueWrapper>) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, RoundTripFromUint64) {
  TestRoundTripFromInteger<uint64_t>();
}

TEST_F(NumericValueTest, FromDouble) {
  static constexpr NumericUnaryOpTestData<double, NumericValueWrapper>
      kTestData[] = {
          {0, 0},
          {0.000000001, "0.000000001"},
          {0.999999999, "0.999999999"},
          {1.5, "1.5"},
          {123, 123},
          {123.5, "123.5"},
          {4503599627370496.0, "4503599627370496"},
          {1.5e18, "1500000000000000000"},
          {99999999999999999999999999999.999999999,
           "99999999999999991433150857216"},

          // Check rounding
          {3.141592653, "3.141592653"},
          {3.141592653589, "3.141592654"},
          {3.1415926532, "3.141592653"},
          {3.1415926539, "3.141592654"},
          {0.5555555555, "0.555555556"},
          {0.0000000001, 0},

          {1.0000000001e29, kNumericOutOfRange},
          // 3e29 * kScalingFactor is between int128max and uint128max.
          {3e29, kNumericOutOfRange},
          {1e30, kNumericOutOfRange},
          {std::numeric_limits<double>::max(), kNumericOutOfRange},

          {std::numeric_limits<double>::quiet_NaN(), kNumericIllegalNonFinite},
          {std::numeric_limits<double>::signaling_NaN(),
           kNumericIllegalNonFinite},
          {std::numeric_limits<double>::infinity(), kNumericIllegalNonFinite},
      };

  NumericFromDoubleOp<NumericValue> op;
  for (const auto& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
    TestUnaryOp(op, -data.input, -data.expected_output);
  }
}

TEST_F(NumericValueTest, FromDouble_RandomInputs) {
  NumericFromDoubleOp<NumericValue> op;
  for (int i = 0; i < 10000; ++i) {
    uint64_t bits = absl::Uniform<uint64_t>(random_);
    double double_val = absl::bit_cast<double>(bits);
    if (!std::isfinite(double_val)) {
      TestUnaryOp(op, double_val,
                  NumericValueWrapper(kNumericIllegalNonFinite));
    } else {
      std::string str = absl::StrFormat("%.11f", double_val);
      if (absl::EndsWith(str, "50")) {
        // Strings with suffix 50 could be from doubles with suffix 49 and
        // get rounded up. In this case the test would fail while the result is
        // correct. Skipping those cases for now.
        continue;
      }
      auto expected = NumericValue::FromString(str);
      if (expected.ok()) {
        TestUnaryOp(op, double_val, expected);
      } else {
        TestUnaryOp(op, double_val, NumericValueWrapper(kNumericOutOfRange));
      }
    }
  }
}

TEST_F(NumericValueTest, Divide) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {1, 1, 1},
      {0, kMaxNumericValueStr, 0},
      {6, 2, 3},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {kMaxNumericValueStr, kMaxNumericValueStr, 1},
      {kMaxNumericValueStr, 3, "33333333333333333333333333333.333333333"},
      {kMaxNumericValueStr, "33333333333333333333333333333.333333333", 3},
      {5, 2, "2.5"},
      {5, "0.5", 10},
      {"18446744073709551616", 4294967296, 4294967296},

      // Rounding.
      {1, 3, "0.333333333"},
      {2, 3, "0.666666667"},
      {"1e20", "33333333333333333333333333333", "0.000000003"},

      // Specific test cases that exercise rarely executed branches in the
      // division algorithm.
      {"72532070012368178591038012.422501607", "30633582.37",
       "2367730588486448005.037486330"},
      {"75968009597863048104202226663", "56017.999",
       "1356135723410310462967487.051135118"},

      {kMaxNumericValueStr, "0.3", kNumericOverflow},
      {kMaxNumericValueStr, "0.999999999", kNumericOverflow},
      {"1e20", "1e-9", kNumericOverflow},

      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxNumericValueStr, 0, kDivisionByZero},
  };

  NumericDivideOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Power) {
  static constexpr Error kNegativeToFractionalError(
      "Negative NUMERIC value cannot be raised to a fractional power: ");
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {0, 0, 1},
      {kMaxNumericValueStr, 0, 1},
      {kMinNumericValueStr, 0, 1},
      {0, 10, 0},
      {3, 1, 3},
      {-3, 1, -3},
      {3, 2, 9},
      {-3, 2, 9},
      {2, 15, 32768},
      {-2, 15, -32768},
      {kMaxNumericValueStr, 1, kMaxNumericValueStr},
      {kMinNumericValueStr, 1, kMinNumericValueStr},
      {"0.1", 2, "0.01"},
      {"0.1", 3, "0.001"},
      {"0.1", 4, "0.0001"},
      {"-0.1", 2, "0.01"},
      {"-0.1", 3, "-0.001"},
      {"-0.1", 4, "0.0001"},
      {"1.00001", 10, "1.000100005"},
      {"-1.00001", 10, "1.000100005"},
      {"1.5", 11, "86.497558594"},
      {"-1.5", 11, "-86.497558594"},
      {"1.001", 10000, "21916.681339078"},
      {"-1.001", 10000, "21916.681339078"},

      // Negative exponent.
      {5, -1, "0.2"},
      {-5, -1, "-0.2"},
      {1, -10, 1},
      {-1, -10, 1},
      {1, -11, 1},
      {-1, -11, -1},
      {"0.1", -1, 10},
      {"-0.1", -1, -10},
      {"0.1", -10, "1e10"},
      {"-0.1", -10, "1e10"},
      {"0.1", -11, "1e11"},
      {"-0.1", -11, "-1e11"},
      {"0.1", -28, "1e28"},

      // Fractional exponent.
      {0, "0.5", 0},
      {4, "0.5", 2},
      {4, "-0.5", "0.5"},
      {4, "1.5", 8},
      {4, "-1.5", "0.125"},
      {4, "2.5", 32},
      {4, "-2.5", "0.03125"},
      {"9e20", "0.5", "3e10"},
      {kMaxNumericValueStr, "0.5", "316227766016837.9375"},
      {"152415787806720022193399025", "0.5", "12345678912345"},
      {"1e10", "-0.5", "1e-5"},
      {"2e9", "-0.5", "0.000022361"},
      {"2e9", "-1.01", 0},
      {"1024", "-2.2", "0.000000238"},
      {"2", -22, "0.000000238"},
      {"-2", -22, "0.000000238"},

      // Underflow.
      {kMaxNumericValueStr, -1, 0},
      {kMinNumericValueStr, -1, 0},
      {kMaxNumericValueStr, kMinNumericValueStr, 0},
      {"0.1", 10, 0},
      {"-0.1", 10, 0},
      {"1e5", -2, 0},
      {"-1e5", -2, 0},
      {"1e2", -5, 0},
      {"-1e2", -5, 0},
      {"5.123", -40, 0},
      {"-5.123", -40, 0},

      // Overflow.
      {kMaxNumericValueStr, 2, kNumericOverflow},
      {kMinNumericValueStr, 2, kNumericOverflow},
      {"5.123", 50, kNumericOverflow},
      {"-5.123", 50, kNumericOverflow},
      {"19106210.01032759", "4.23", kNumericOverflow},
      {"0.479371", "-96.45037198", kNumericOverflow},

      {0, -1, kDivisionByZero},
      {0, "-1.5", kDivisionByZero},
      {0, -2, kDivisionByZero},
      {0, kMinNumericValueStr, kDivisionByZero},

      {"-0.000000001", "0.000000001", kNegativeToFractionalError},
      {"-0.000000001", "-0.000000001", kNegativeToFractionalError},
      {-123, "2.1", kNegativeToFractionalError},
      {-123, "-2.1", kNegativeToFractionalError},
      {kMinNumericValueStr, kMaxNumericValueStr, kNegativeToFractionalError},
      {kMinNumericValueStr, kMinNumericValueStr, kNegativeToFractionalError},
  };

  NumericPowerOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
  }

  // POW(1.5, 140): a case with inexact result
  FixedInt<64, 4> expected_packed(int64_t{1000000000});
  const int kExp = 140;
  for (int i = 0; i < kExp; ++i) {
    expected_packed *= uint64_t{3};
  }
  // Divide expected_packed by pow(2, 140);
  // skip rounding (the resulting error is at most 1e-9).
  expected_packed >>= kExp;
  NumericValue expected =
      NumericValue::FromPackedInt(static_cast<__int128>(expected_packed))
          .ValueOrDie();
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue actual,
                       MkNumeric("1.5").Power(NumericValue(kExp)));
  NumericValue error =
      NumericValue::Abs(actual.Subtract(expected).ValueOrDie());
  EXPECT_LT(error, NumericValue(20));
}

TEST_F(NumericValueTest, Power_RandomCombinations) {
  for (int i = 0; i < 100000; ++i) {
    NumericValue x1 = NumericValue::Abs(MakeRandomNumeric());
    NumericValue x2 = MakeRandomNumeric();
    auto result = x1.Power(x2);
    double approx_expected = std::pow(x1.ToDouble(), x2.ToDouble());
    if (result.ok()) {
      EXPECT_NEAR(result.ValueOrDie().ToDouble(), approx_expected,
                  std::max(std::abs(approx_expected) * 1e-5, 1e-9))
          << "POW(" << x1 << ", " << x2 << ")";
    } else {
      EXPECT_TRUE(std::isnan(approx_expected) ||
                  std::abs(approx_expected) > 9.9999e28)
          << "POW(" << x1 << ", " << x2 << "): expected " << approx_expected
          << "\ngot " << result.status();
    }
  }
}

TEST_F(NumericValueTest, Power_PowersOfTwo) {
  uint64_t oracle = 1;
  for (int64_t exp = 0; exp < 63; ++exp, oracle <<= 1) {
    EXPECT_EQ(NumericValue(oracle),
              NumericValue(2).Power(NumericValue(exp)).ValueOrDie());
    NumericValue neg_oracle(oracle);
    if ((exp % 2) != 0) {
      neg_oracle = NumericValue::UnaryMinus(neg_oracle);
    }
    EXPECT_EQ(neg_oracle,
              NumericValue(-2).Power(NumericValue(exp)).ValueOrDie());
  }
}

template <typename T>
class NumericValueByTypeTest : public NumericValueTest {};

template <typename T>
void AddValuesToAggregator(T* aggregator,
                           const std::vector<NumericValue>& values) {
  for (auto value : values) {
    aggregator->Add(value);
  }
}

template <>
void AddValuesToAggregator<NumericValue::CovarianceAggregator>(
    NumericValue::CovarianceAggregator* aggregator,
    const std::vector<NumericValue>& values) {
  for (auto value : values) {
    aggregator->Add(value, value);
  }
}

template <>
void AddValuesToAggregator<NumericValue::CorrelationAggregator>(
    NumericValue::CorrelationAggregator* aggregator,
    const std::vector<NumericValue>& values) {
  for (auto value : values) {
    aggregator->Add(value, value);
  }
}

template <typename T>
class AggregatorSerializationByTypeTest : public NumericValueByTypeTest<T> {
 protected:
  void TestSerializeAggregator(const std::vector<NumericValue>& values) {
    T aggregator;
    AddValuesToAggregator(&aggregator, values);

    std::string bytes = aggregator.SerializeAsProtoBytes();
    ZETASQL_ASSERT_OK_AND_ASSIGN(T deserialized_aggregator,
                         T::DeserializeFromProtoBytes(bytes));
    EXPECT_EQ(aggregator, deserialized_aggregator);
  }
};

using SumAggregatorTypes =
    ::testing::Types<NumericValue::Aggregator, NumericValue::SumAggregator>;
using AllAggregatorTypes =
    ::testing::Types<NumericValue::Aggregator, NumericValue::SumAggregator,
                     NumericValue::VarianceAggregator,
                     NumericValue::CovarianceAggregator,
                     NumericValue::CorrelationAggregator>;

TYPED_TEST_SUITE(NumericValueByTypeTest, SumAggregatorTypes);

TYPED_TEST_SUITE(AggregatorSerializationByTypeTest, AllAggregatorTypes);

TYPED_TEST(NumericValueByTypeTest, AggregatorOneValue) {
  TypeParam a1;
  a1.Add(NumericValue(0));
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue sum, a1.GetSum());
  EXPECT_EQ(NumericValue(0), sum);

  TypeParam a2;
  a2.Add(NumericValue::MaxValue());
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a2.GetSum());
  EXPECT_EQ(NumericValue::MaxValue(), sum);

  TypeParam a3;
  a3.Add(NumericValue::MinValue());
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a3.GetSum());
  EXPECT_EQ(NumericValue::MinValue(), sum);
}

TYPED_TEST(NumericValueByTypeTest, AggregatorMultipleValues) {
  const int64_t kCount = 10000;

  TypeParam a1;
  TypeParam a2;
  TypeParam a3;

  for (int64_t i = 1; i <= kCount; i++) {
    a1.Add(NumericValue(i));
    a2.Add(NumericValue(-i));
    a3.Add(NumericValue(i % 2 ? i : -i));
  }

  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue sum, a1.GetSum());
  EXPECT_EQ(NumericValue((1 + kCount) * kCount / 2), sum);

  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a2.GetSum());
  EXPECT_EQ(NumericValue((-1LL - kCount) * kCount / 2), sum);
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a3.GetSum());
  EXPECT_EQ(NumericValue(-kCount / 2), sum);

  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue expected,
                       NumericValue::FromDouble((1 + kCount) / 2.0));
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue avg, a1.GetAverage(kCount));
  EXPECT_EQ(expected, avg);
  ZETASQL_ASSERT_OK_AND_ASSIGN(expected,
                       NumericValue::FromDouble((-1LL - kCount) / 2.0));
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a2.GetSum());
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(kCount));
  EXPECT_EQ(expected, avg);
  ZETASQL_ASSERT_OK_AND_ASSIGN(expected, NumericValue::FromDouble(-0.5));
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a3.GetSum());
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a3.GetAverage(kCount));
  EXPECT_EQ(expected, avg);
}

TYPED_TEST(NumericValueByTypeTest, AggregatorAverageRounding) {
  // 1/3 - rounding down.
  TypeParam a1;
  a1.Add(NumericValue(1));
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue avg, a1.GetAverage(3));
  EXPECT_EQ("0.333333333", avg.ToString());

  // 2/3 - rounding up.
  a1.Add(NumericValue(1));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a1.GetAverage(3));
  EXPECT_EQ("0.666666667", avg.ToString());

  // 5/11- rounding up.
  a1.Add(NumericValue(3));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a1.GetAverage(11));
  EXPECT_EQ("0.454545455", avg.ToString());

  // -1/3 - rounding down.
  TypeParam a2;
  a2.Add(NumericValue(-1));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(3));
  EXPECT_EQ("-0.333333333", avg.ToString());

  // -4/6 - rounding up.
  a2.Add(NumericValue(-1));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(3));
  EXPECT_EQ("-0.666666667", avg.ToString());

  // -5/11 - - rounding up.
  a2.Add(NumericValue(-3));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(11));
  EXPECT_EQ("-0.454545455", avg.ToString());
}

TYPED_TEST(NumericValueByTypeTest, AggregatorOverflow) {
  TypeParam a1;

  EXPECT_THAT(a1.GetAverage(0),
              StatusIs(zetasql_base::OUT_OF_RANGE, "division by zero: AVG"));

  a1.Add(NumericValue::MaxValue());
  a1.Add(NumericValue(1));
  EXPECT_THAT(a1.GetSum(),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: SUM"));
  EXPECT_THAT(a1.GetAverage(1),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: AVG"));
  a1.Add(NumericValue(-1));
  // The sum no longer overflows.
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue sum, a1.GetSum());
  EXPECT_EQ(NumericValue::MaxValue(), sum);
  ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue avg, a1.GetAverage(1));
  EXPECT_EQ(NumericValue::MaxValue(), avg);

  // Advance to 2 * MaxValue
  a1.Add(NumericValue::MaxValue());
  EXPECT_THAT(a1.GetSum(),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: SUM"));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a1.GetAverage(2));
  EXPECT_EQ(NumericValue::MaxValue(), avg);

  // Advance back to MaxValue
  a1.Add(NumericValue::MinValue());
  // The sum no longer overflows.
  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a1.GetSum());
  EXPECT_EQ(NumericValue::MaxValue(), sum);

  // Advance the sum to 2 * MinValue
  a1.Add(NumericValue::MinValue());
  a1.Add(NumericValue::MinValue());
  a1.Add(NumericValue::MinValue());
  // The sum should overflow again.
  EXPECT_THAT(a1.GetSum(),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: SUM"));
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a1.GetAverage(6));
  EXPECT_EQ("-33333333333333333333333333333.333333333", avg.ToString());

  // Advance to 2 * MinValue - 1 so dividing by 2 overflows
  a1.Add(NumericValue(-1));
  EXPECT_THAT(a1.GetAverage(2),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: AVG"));

  // Special case: adding 4 values which internal representation is exactly
  // -2^125. That makes the sum exactly -2^127 which is the minimum
  // possible __int128 value.
  NumericValue large_negative =
      TestFixture::MkNumeric("-42535295865117307932921825928.971026432");
  TypeParam a2;
  for (int i = 1; i <= 4; i++) {
    a2.Add(large_negative);
    ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(i));
    EXPECT_EQ(large_negative, avg);
  }

  EXPECT_THAT(a2.GetSum(),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: SUM"));

  // Add 4 more of large_negative values - the end result should be the same.
  for (int i = 1; i <= 4; i++) {
    a2.Add(large_negative);
    EXPECT_THAT(a2.GetSum(),
                StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: SUM"));
    ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a2.GetAverage(4 + i));
    EXPECT_EQ(large_negative, avg);
  }

  EXPECT_THAT(a2.GetAverage(2),
              StatusIs(zetasql_base::OUT_OF_RANGE, "numeric overflow: AVG"));

  // Add 10,000 max values, then 20,000 min values, then 10,000 max values.
  const int kCount = 10000;
  TypeParam a3;

  for (int i = 0; i < kCount; i++) {
    a3.Add(NumericValue::MaxValue());
  }
  for (int i = 0; i < kCount * 2; i++) {
    a3.Add(NumericValue::MinValue());
  }
  for (int i = 0; i < kCount; i++) {
    a3.Add(NumericValue::MaxValue());
  }

  ZETASQL_ASSERT_OK_AND_ASSIGN(sum, a3.GetSum());
  EXPECT_EQ(NumericValue(0), sum);
  ZETASQL_ASSERT_OK_AND_ASSIGN(avg, a3.GetAverage(kCount * 4));
  EXPECT_EQ(NumericValue(0), avg);
}

TYPED_TEST(NumericValueByTypeTest, AggregatorMergeWith) {
  std::vector<NumericValue> values = {
    NumericValue(0),
    NumericValue(1),
    NumericValue::MaxValue(),
    TestFixture::MkNumeric("-123.01"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    TestFixture::MkNumeric("56.999999999")
  };

  // Single aggregator used as a control for the MergeWith operation.
  TypeParam control;

  // Tests different total number of aggregated values.
  for (int num_values = 0; num_values <= values.size(); num_values++) {
    if (num_values > 0) {
      control.Add(values[num_values - 1]);
    }

    bool control_sum_overflow = !control.GetSum().ok();
    NumericValue control_sum = control_sum_overflow
        ? NumericValue() : control.GetSum().ValueOrDie();
    NumericValue control_average = num_values > 0
        ? control.GetAverage(num_values).ValueOrDie() : NumericValue();

    // Break input values between two aggregators and test that merging them
    // doesn't affect the sum or the average of values against the control
    // aggregator.
    for (int num_in_first_aggregator = 0; num_in_first_aggregator <= num_values;
         num_in_first_aggregator++) {
      TypeParam a1;
      TypeParam a2;
      for (int i = 0; i < num_in_first_aggregator; i++) {
        a1.Add(values[i]);
      }
      for (int i = num_in_first_aggregator; i < num_values; i++) {
        a2.Add(values[i]);
      }

      TypeParam test;
      test.MergeWith(a1);
      test.MergeWith(a2);

      if (control_sum_overflow) {
        EXPECT_THAT(test.GetSum(), StatusIs(zetasql_base::OUT_OF_RANGE,
                                            "numeric overflow: SUM"));
      } else {
        ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue sum, test.GetSum());
        EXPECT_EQ(sum, control_sum);
      }

      if (num_values > 0) {
        ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue avg, test.GetAverage(num_values));
        EXPECT_EQ(avg, control_average);
      }
    }
  }
}

TYPED_TEST(AggregatorSerializationByTypeTest, AggregatorSerialization) {
  TypeParam a1;
  std::string bytes = a1.SerializeAsProtoBytes();
  if (std::is_same_v<TypeParam, NumericValue::Aggregator>) {
    EXPECT_EQ(24, bytes.size());
  }

  this->TestSerializeAggregator({NumericValue()});
  this->TestSerializeAggregator({NumericValue(1)});
  this->TestSerializeAggregator({NumericValue(-1)});
  this->TestSerializeAggregator({this->MkNumeric("123.01")});
  this->TestSerializeAggregator({this->MkNumeric("-123.01")});
  this->TestSerializeAggregator({NumericValue::MinValue()});
  this->TestSerializeAggregator({NumericValue::MaxValue()});
}

TYPED_TEST(AggregatorSerializationByTypeTest,
           AggregatorLargeValueSerialization) {
  std::vector<NumericValue> min_values;
  std::vector<NumericValue> max_values;
  for (int i = 0; i < 100; i++) {
    min_values.push_back(NumericValue::MinValue());
    max_values.push_back(NumericValue::MaxValue());
    this->TestSerializeAggregator(min_values);
    this->TestSerializeAggregator(max_values);
  }
}

TYPED_TEST(AggregatorSerializationByTypeTest,
           AggregatorDeserializationFailures) {
  for (absl::string_view string_view : kInvalidSerializedAggregators) {
    EXPECT_THAT(TypeParam::DeserializeFromProtoBytes(string_view),
                StatusIs(zetasql_base::OUT_OF_RANGE,
                         testing::MatchesRegex(
                             "Invalid "
                             "NumericValue::(|Sum|Variance|Covariance|"
                             "Correlation)Aggregator encoding")));
  }
}

TEST_F(NumericValueTest, VarianceAggregatorNoValue) {
  NumericValue::VarianceAggregator agg;
  TestVarianceAggregator(agg, absl::nullopt, absl::nullopt, 0);
}

TEST_F(NumericValueTest, VarianceAggregatorSingleValue) {
  NumericValue::VarianceAggregator agg1;
  agg1.Add(NumericValue(0));
  TestVarianceAggregator(agg1, 0.0, absl::nullopt, 1);

  NumericValue::VarianceAggregator agg2;
  agg2.Add(NumericValue::MaxValue());
  TestVarianceAggregator(agg2, 0.0, absl::nullopt, 1);

  NumericValue::VarianceAggregator agg3;
  agg3.Add(NumericValue::MinValue());
  TestVarianceAggregator(agg3, 0.0, absl::nullopt, 1);
}

TEST_F(NumericValueTest, VarianceAggregatorMultipleValues) {
  const int64_t kMoreCount = 10000;
  const int64_t kLessCount = 5;

  NumericValue::VarianceAggregator agg1;
  NumericValue::VarianceAggregator agg2;
  NumericValue::VarianceAggregator agg3;
  NumericValue::VarianceAggregator agg4;
  NumericValue::VarianceAggregator agg5;
  NumericValue::VarianceAggregator agg6;

  for (int64_t i = 1; i <= kMoreCount; i++) {
    agg1.Add(NumericValue(i));
    agg2.Add(NumericValue(-i));
    agg3.Add(NumericValue(i % 2 ? i : -i));
    agg4.Add(NumericValue(kMoreCount));
  }

  agg5.Add(NumericValue(2));
  agg5.Add(NumericValue(2));
  agg5.Add(NumericValue(-3));
  agg5.Add(NumericValue(2));
  agg5.Add(NumericValue(2));

  agg6.Add(NumericValue(1));
  agg6.Add(NumericValue(-1));
  agg6.Add(NumericValue(1));
  agg6.Add(NumericValue(1));
  agg6.Add(NumericValue(0));

  double expect_pvar1 = 8333333.25;
  double expect_svar1 = 8334166.666666667;
  TestVarianceAggregator(agg1, expect_pvar1, expect_svar1, kMoreCount);
  double expect_pvar2 = 8333333.25;
  double expect_svar2 = 8334166.666666667;
  TestVarianceAggregator(agg2, expect_pvar2, expect_svar2, kMoreCount);
  double expect_pvar3 = 33338333.25;
  double expect_svar3 = 33341667.41674167;
  TestVarianceAggregator(agg3, expect_pvar3, expect_svar3, kMoreCount);
  double expect_pvar4 = 0;
  double expect_svar4 = 0;
  TestVarianceAggregator(agg4, expect_pvar4, expect_svar4, kMoreCount);
  double expect_pvar5 = 4;
  double expect_svar5 = 5;
  TestVarianceAggregator(agg5, expect_pvar5, expect_svar5, kLessCount);
  double expect_pvar6 = 0.64;
  double expect_svar6 = 0.8;
  TestVarianceAggregator(agg6, expect_pvar6, expect_svar6, kLessCount);
}

TEST_F(NumericValueTest, VarianceAggregatorSmallValues) {
  NumericValue::VarianceAggregator agg1;

  agg1.Add(MkNumeric("0"));
  agg1.Add(MkNumeric("0.000000001"));

  double expect_pvar1 = 2.5e-19;
  double expect_svar1 = 5e-19;
  TestVarianceAggregator(agg1, expect_pvar1, expect_svar1, 2);
}

TEST_F(NumericValueTest, VarianceAggregatorSubtractedValues) {
  const int64_t kLessCount = 5;

  NumericValue::VarianceAggregator agg1;
  NumericValue::VarianceAggregator agg2;

  agg1.Add(NumericValue(2));
  agg1.Add(NumericValue(4));
  agg1.Add(NumericValue(2));
  agg1.Add(NumericValue(5));
  agg1.Add(NumericValue(-3));
  agg1.Add(NumericValue(2));
  agg1.Add(NumericValue(2));
  agg1.Subtract(NumericValue(5));
  agg1.Subtract(NumericValue(4));

  agg2.Add(NumericValue(1));
  agg2.Add(NumericValue(-100));
  agg2.Add(NumericValue(-1));
  agg2.Add(NumericValue(200));
  agg2.Add(NumericValue(1));
  agg2.Subtract(NumericValue(-100));
  agg2.Add(NumericValue(1));
  agg2.Subtract(NumericValue(200));
  agg2.Add(NumericValue(0));

  double expect_pvar1 = 4;
  double expect_svar1 = 5;
  TestVarianceAggregator(agg1, expect_pvar1, expect_svar1, kLessCount);
  double expect_pvar2 = 0.64;
  double expect_svar2 = 0.8;
  TestVarianceAggregator(agg2, expect_pvar2, expect_svar2, kLessCount);
}

TEST_F(NumericValueTest, VarianceAggregatorMergeWith) {
  std::vector<NumericValue> values = {
    NumericValue(0),
    NumericValue(1),
    NumericValue(-1),
    NumericValue::MaxValue(),
    MkNumeric("-123.01"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    MkNumeric("56.999999999")
  };

  // Single aggregator used as a control for the MergeWith operation.
  NumericValue::VarianceAggregator control;

  // Tests different total number of aggregated values.
  for (int num_values = 0; num_values <= values.size(); ++num_values) {
    if (num_values > 0) {
      control.Add(values[num_values - 1]);
    }

    absl::optional<double> control_population_variance =
        control.GetPopulationVariance(num_values);
    absl::optional<double> control_sampling_variance =
        control.GetSamplingVariance(num_values);

    for (int num_in_first_aggregator = 0; num_in_first_aggregator <= num_values;
         num_in_first_aggregator++) {
      NumericValue::VarianceAggregator a1;
      NumericValue::VarianceAggregator a2;
      for (int i = 0; i < num_in_first_aggregator; i++) {
        a1.Add(values[i]);
      }
      for (int i = num_in_first_aggregator; i < num_values; i++) {
        a2.Add(values[i]);
      }

      NumericValue::VarianceAggregator test;
      test.MergeWith(a2);
      test.MergeWith(a1);

      EXPECT_EQ(control_population_variance,
                test.GetPopulationVariance(num_values));
      EXPECT_EQ(control_sampling_variance,
                test.GetSamplingVariance(num_values));
    }
  }
}

TEST_F(NumericValueTest, CovarianceAggregatorNoValue) {
  NumericValue::CovarianceAggregator agg;
  TestCovariance(agg, absl::nullopt, absl::nullopt, 0);
}

TEST_F(NumericValueTest, CovarianceAggregatorSinglePair) {
  NumericValue::CovarianceAggregator agg1;
  NumericValue::CovarianceAggregator agg2;
  NumericValue::CovarianceAggregator agg3;

  agg1.Add(MkNumeric("1"), MkNumeric("1"));
  TestCovariance(agg1, 0, absl::nullopt, 1);

  agg2.Add(NumericValue::MaxValue(), NumericValue::MaxValue());
  TestCovariance(agg2, 0, absl::nullopt, 1);

  agg3.Add(NumericValue::MinValue(), NumericValue::MinValue());
  TestCovariance(agg3, 0, absl::nullopt, 1);
}

TEST_F(NumericValueTest, CovarianceAggregatorMultiplePairs) {
  NumericValue::CovarianceAggregator agg1;
  NumericValue::CovarianceAggregator agg2;

  agg1.Add(MkNumeric("1.2"), MkNumeric("5"));
  agg1.Add(MkNumeric("-2.4"), MkNumeric("15"));
  agg1.Add(MkNumeric("3.6"), MkNumeric("-20"));
  agg1.Add(MkNumeric("4.8"), MkNumeric("30"));
  agg1.Add(MkNumeric("6"), MkNumeric("35"));
  TestCovariance(agg1, 16.08, 20.1, 5);

  agg2.Add(MkNumeric("100"), MkNumeric("3"));
  agg2.Add(MkNumeric("200"), MkNumeric("7"));
  agg2.Add(MkNumeric("300"), MkNumeric("11"));
  agg2.Add(MkNumeric("400"), MkNumeric("13"));
  agg2.Add(MkNumeric("600"), MkNumeric("17"));
  TestCovariance(agg2, 816, 1020, 5);
}

TEST_F(NumericValueTest, CovarianceAggregatorSmallValues) {
  NumericValue::CovarianceAggregator agg1;

  agg1.Add(MkNumeric("0"), MkNumeric("0.000000001"));
  agg1.Add(MkNumeric("0.000000001"), MkNumeric("0"));

  double expect_covar_pop = -2.5e-19;
  double expect_covar_samp = -5e-19;
  TestCovariance(agg1, expect_covar_pop, expect_covar_samp, 2);
}

TEST_F(NumericValueTest, CovarianceAggregatorSubtractPairs) {
  NumericValue::CovarianceAggregator agg1;

  agg1.Add(MkNumeric("1.2"), MkNumeric("5"));
  agg1.Add(MkNumeric("-2.4"), MkNumeric("15"));
  agg1.Add(MkNumeric("17"), MkNumeric("-3.5"));
  agg1.Add(MkNumeric("3.6"), MkNumeric("-20"));
  agg1.Add(MkNumeric("2.4"), MkNumeric("8"));
  agg1.Add(MkNumeric("4.8"), MkNumeric("30"));
  agg1.Subtract(MkNumeric("2.4"), MkNumeric("8"));
  agg1.Add(MkNumeric("6"), MkNumeric("35"));
  agg1.Subtract(MkNumeric("17"), MkNumeric("-3.5"));
  TestCovariance(agg1, 16.08, 20.1, 5);
}

TEST_F(NumericValueTest, CovarianceAggregatorMergeWith) {
  std::vector<NumericValue> x_values = {
    NumericValue(0),
    NumericValue(1),
    NumericValue(-1),
    NumericValue::MaxValue(),
    MkNumeric("-123.01"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    MkNumeric("56.999999999")
  };
  std::vector<NumericValue> y_values = {
    NumericValue(8),
    NumericValue(7),
    NumericValue(2),
    MkNumeric("-53.8"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    MkNumeric("32.999999999"),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue()
  };

  // Single aggregator used as a control for the MergeWith operation.
  NumericValue::CovarianceAggregator control;

  // Tests different total number of aggregated values.
  for (int num_values = 0; num_values <= x_values.size(); ++num_values) {
    if (num_values > 0) {
      control.Add(x_values[num_values - 1], y_values[num_values - 1]);
    }

    absl::optional<double> control_population_covariance =
        control.GetPopulationCovariance(num_values);
    absl::optional<double> control_sampling_covariance =
        control.GetSamplingCovariance(num_values);

    for (int num_in_first_aggregator = 0; num_in_first_aggregator <= num_values;
         num_in_first_aggregator++) {
      NumericValue::CovarianceAggregator a1;
      NumericValue::CovarianceAggregator a2;
      for (int i = 0; i < num_in_first_aggregator; i++) {
        a1.Add(x_values[i], y_values[i]);
      }
      for (int i = num_in_first_aggregator; i < num_values; i++) {
        a2.Add(x_values[i], y_values[i]);
      }

      NumericValue::CovarianceAggregator test;
      test.MergeWith(a2);
      test.MergeWith(a1);

      EXPECT_EQ(control_population_covariance,
                test.GetPopulationCovariance(num_values));
      EXPECT_EQ(control_sampling_covariance,
                test.GetSamplingCovariance(num_values));
    }
  }
}

TEST_F(NumericValueTest, CorrelationAggregatorNoValue) {
  NumericValue::CorrelationAggregator agg;
  TestCorrelation(agg, absl::nullopt, 0);
}

TEST_F(NumericValueTest, CorrelationAggregatorSinglePair) {
  NumericValue::CorrelationAggregator agg1;
  NumericValue::CorrelationAggregator agg2;
  NumericValue::CorrelationAggregator agg3;

  agg1.Add(MkNumeric("1"), MkNumeric("1"));
  TestCorrelation(agg1, absl::nullopt, 1);

  agg2.Add(NumericValue::MaxValue(), NumericValue::MaxValue());
  TestCorrelation(agg2, absl::nullopt, 1);

  agg3.Add(NumericValue::MinValue(), NumericValue::MinValue());
  TestCorrelation(agg3, absl::nullopt, 1);
}

TEST_F(NumericValueTest, CorrelationAggregatorMultiplePairsCorr) {
  NumericValue::CorrelationAggregator agg1;
  NumericValue::CorrelationAggregator agg2;
  NumericValue::CorrelationAggregator agg3;

  agg1.Add(MkNumeric("1"), MkNumeric("1"));
  agg1.Add(NumericValue::MaxValue(), NumericValue::MaxValue());
  agg1.Add(NumericValue::MinValue(), NumericValue::MinValue());
  TestCorrelation(agg1, 1, 3);

  agg2.Add(MkNumeric("1"), MkNumeric("5"));
  agg2.Add(MkNumeric("1.5"), MkNumeric("15"));
  agg2.Add(MkNumeric("2"), MkNumeric("20"));
  agg2.Add(MkNumeric("2.5"), MkNumeric("25"));
  agg2.Add(MkNumeric("3"), MkNumeric("35"));
  TestCorrelation(agg2, std::sqrt(0.98), 5);

  agg3.Add(MkNumeric("1"), MkNumeric("3"));
  agg3.Add(MkNumeric("2"), MkNumeric("3"));
  agg3.Add(MkNumeric("3"), MkNumeric("3"));
  agg3.Add(MkNumeric("4"), MkNumeric("3"));
  agg3.Add(MkNumeric("5"), MkNumeric("3"));
  TestCorrelation(agg3, std::numeric_limits<double>::quiet_NaN(), 5);
}

TEST_F(NumericValueTest, CorrelationAggregatorMultiplePairsNegativeCorr) {
  NumericValue::CorrelationAggregator agg1;
  NumericValue::CorrelationAggregator agg2;

  agg1.Add(MkNumeric("1"), MkNumeric("-1"));
  agg1.Add(NumericValue::MaxValue(), NumericValue::MinValue());
  agg1.Add(NumericValue::MinValue(), NumericValue::MaxValue());
  TestCorrelation(agg1, -1, 3);

  agg2.Add(MkNumeric("1"), MkNumeric("-5"));
  agg2.Add(MkNumeric("1.5"), MkNumeric("-15"));
  agg2.Add(MkNumeric("2"), MkNumeric("-20"));
  agg2.Add(MkNumeric("2.5"), MkNumeric("-25"));
  agg2.Add(MkNumeric("3"), MkNumeric("-35"));
  TestCorrelation(agg2, -std::sqrt(0.98), 5);
}

TEST_F(NumericValueTest, CorrelationAggregatorSubtractPairs) {
  NumericValue::CorrelationAggregator agg1;

  agg1.Add(MkNumeric("8"), MkNumeric("-2"));
  agg1.Add(MkNumeric("1"), MkNumeric("5"));
  agg1.Add(MkNumeric("3"), MkNumeric("35"));
  agg1.Add(MkNumeric("1.5"), MkNumeric("15"));
  agg1.Add(MkNumeric("2"), MkNumeric("20"));
  agg1.Add(MkNumeric("2.5"), MkNumeric("25"));
  agg1.Subtract(MkNumeric("8"), MkNumeric("-2"));
  agg1.Subtract(MkNumeric("3"), MkNumeric("35"));
  agg1.Add(MkNumeric("3"), MkNumeric("35"));
  TestCorrelation(agg1, std::sqrt(0.98), 5);
}

TEST_F(NumericValueTest, CorrelationAggregatorMergeWith) {
  std::vector<NumericValue> x_values = {
    NumericValue(0),
    NumericValue(1),
    NumericValue(-1),
    NumericValue::MaxValue(),
    MkNumeric("-123.01"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    MkNumeric("56.999999999")
  };
  std::vector<NumericValue> y_values = {
    NumericValue(8),
    NumericValue(7),
    NumericValue(2),
    MkNumeric("-53.8"),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MinValue(),
    NumericValue::MaxValue(),
    MkNumeric("32.999999999"),
    NumericValue::MaxValue(),
    NumericValue::MaxValue(),
    NumericValue::MaxValue()
  };

  // Single aggregator used as a control for the MergeWith operation.
  NumericValue::CorrelationAggregator control;

  // Tests different total number of aggregated values.
  for (int num_values = 0; num_values <= x_values.size(); ++num_values) {
    if (num_values > 0) {
      control.Add(x_values[num_values - 1], y_values[num_values - 1]);
    }

    absl::optional<double> control_correlation =
        control.GetCorrelation(num_values);

    for (int num_in_first_aggregator = 0; num_in_first_aggregator <= num_values;
         num_in_first_aggregator++) {
      NumericValue::CorrelationAggregator a1;
      NumericValue::CorrelationAggregator a2;
      for (int i = 0; i < num_in_first_aggregator; i++) {
        a1.Add(x_values[i], y_values[i]);
      }
      for (int i = num_in_first_aggregator; i < num_values; i++) {
        a2.Add(x_values[i], y_values[i]);
      }

      NumericValue::CorrelationAggregator test;
      test.MergeWith(a2);
      test.MergeWith(a1);

      EXPECT_EQ(control_correlation,
                test.GetCorrelation(num_values));
    }
  }
}

TEST_F(NumericValueTest, HasFractionalPart) {
  static constexpr NumericUnaryOpTestData<NumericValueWrapper, bool>
      kTestData[] = {
          {"0.1", true},
          {"0.01", true},
          {"0.001", true},
          {"0.000000001", true},
          {"0.987654321", true},
          {"0.999999999", true},
          {"1.000000001", true},
          {"1", false},
          {"10", false},
          {"9999", false},
          {"99999999999999999999999999999", false},
          {"99999999999999999999999999999.000000001", true},
          {kMaxNumericValueStr, true},
      };

  for (const auto& data : kTestData) {
    ZETASQL_ASSERT_OK_AND_ASSIGN(NumericValue value, GetValue(data.input));
    EXPECT_EQ(data.expected_output, value.has_fractional_part());
    EXPECT_EQ(data.expected_output,
              NumericValue::UnaryMinus(value).has_fractional_part());
  }
}

TEST_F(NumericValueTest, Trunc) {
  static constexpr NumericBinaryOpTestData<int64_t> kTestData[] = {
      {0, kint64min, 0},
      {0, -30, 0},
      {0, -29, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 9, 0},
      {0, 10, 0},
      {0, kint64max, 0},

      {"0.987654321", kint64min, 0},
      {"0.987654321", -30, 0},
      {"0.987654321", -1, 0},
      {"0.987654321", 0, 0},
      {"0.987654321", 1, "0.9"},
      {"0.987654321", 2, "0.98"},
      {"0.987654321", 4, "0.9876"},
      {"0.987654321", 5, "0.98765"},
      {"0.987654321", 8, "0.98765432"},
      {"0.987654321", 9, "0.987654321"},
      {"0.987654321", 10, "0.987654321"},
      {"0.987654321", kint64max, "0.987654321"},

      {"1234567899876543210.123456789", kint64min, 0},
      {"1234567899876543210.123456789", -30, 0},
      {"1234567899876543210.123456789", -19, 0},
      {"1234567899876543210.123456789", -18, 1000000000000000000LL},
      {"1234567899876543210.123456789", -17, 1200000000000000000LL},
      {"1234567899876543210.123456789", -16, 1230000000000000000LL},
      {"1234567899876543210.123456789", -15, 1234000000000000000LL},
      {"1234567899876543210.123456789", -14, 1234500000000000000LL},
      {"1234567899876543210.123456789", -9, 1234567899000000000LL},
      {"1234567899876543210.123456789", -8, 1234567899800000000LL},
      {"1234567899876543210.123456789", -2, 1234567899876543200LL},
      {"1234567899876543210.123456789", 0, 1234567899876543210LL},
      {"1234567899876543210.123456789", 1, "1234567899876543210.1"},
      {"1234567899876543210.123456789", 3, "1234567899876543210.123"},
      {"1234567899876543210.123456789", 4, "1234567899876543210.1234"},
      {"1234567899876543210.123456789", 8, "1234567899876543210.12345678"},
      {"1234567899876543210.123456789", 9, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", 10, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", kint64max,
       "1234567899876543210.123456789"},

      {kMaxNumericValueStr, kint64min, 0},
      {kMaxNumericValueStr, -30, 0},
      {kMaxNumericValueStr, -29, 0},
      {kMaxNumericValueStr, -28, "90000000000000000000000000000"},
      {kMaxNumericValueStr, -27, "99000000000000000000000000000"},
      {kMaxNumericValueStr, -1, "99999999999999999999999999990"},
      {kMaxNumericValueStr, 0, "99999999999999999999999999999"},
      {kMaxNumericValueStr, 1, "99999999999999999999999999999.9"},
      {kMaxNumericValueStr, 8, "99999999999999999999999999999.99999999"},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 10, kMaxNumericValueStr},
      {kMaxNumericValueStr, kint64max, kMaxNumericValueStr},
  };

  NumericTruncOp op;
  for (const NumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, Round) {
  static constexpr NumericBinaryOpTestData<int64_t> kTestData[] = {
      {0, kint64min, 0},
      {0, -30, 0},
      {0, -29, 0},
      {0, -1, 0},
      {0, 0, 0},
      {0, 1, 0},
      {0, 9, 0},
      {0, 10, 0},
      {0, kint64max, 0},

      {"0.987654321", kint64min, 0},
      {"0.987654321", -30, 0},
      {"0.987654321", -1, 0},
      {"0.987654321", 0, 1},
      {"0.987654321", 1, 1},
      {"0.987654321", 2, "0.99"},
      {"0.987654321", 4, "0.9877"},
      {"0.987654321", 5, "0.98765"},
      {"0.987654321", 8, "0.98765432"},
      {"0.987654321", 9, "0.987654321"},
      {"0.987654321", 10, "0.987654321"},
      {"0.987654321", kint64max, "0.987654321"},

      {"1234567899876543210.123456789", kint64min, 0},
      {"1234567899876543210.123456789", -30, 0},
      {"1234567899876543210.123456789", -19, 0},
      {"1234567899876543210.123456789", -18, 1000000000000000000LL},
      {"1234567899876543210.123456789", -17, 1200000000000000000LL},
      {"1234567899876543210.123456789", -16, 1230000000000000000LL},
      {"1234567899876543210.123456789", -15, 1235000000000000000LL},
      {"1234567899876543210.123456789", -14, 1234600000000000000LL},
      {"1234567899876543210.123456789", -9, 1234567900000000000LL},
      {"1234567899876543210.123456789", -8, 1234567899900000000LL},
      {"1234567899876543210.123456789", -2, 1234567899876543200LL},
      {"1234567899876543210.123456789", 0, 1234567899876543210LL},
      {"1234567899876543210.123456789", 1, "1234567899876543210.1"},
      {"1234567899876543210.123456789", 3, "1234567899876543210.123"},
      {"1234567899876543210.123456789", 4, "1234567899876543210.1235"},
      {"1234567899876543210.123456789", 8, "1234567899876543210.12345679"},
      {"1234567899876543210.123456789", 9, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", 10, "1234567899876543210.123456789"},
      {"1234567899876543210.123456789", kint64max,
       "1234567899876543210.123456789"},

      {"12341234123412341234123412341.234123412", kint64min, 0},
      {"12341234123412341234123412341.234123412", -30, 0},
      {"12341234123412341234123412341.234123412", -29, 0},
      {"12341234123412341234123412341.234123412", -28,
       "10000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -27,
       "12000000000000000000000000000"},
      {"12341234123412341234123412341.234123412", -1,
       "12341234123412341234123412340"},
      {"12341234123412341234123412341.234123412", 0,
       "12341234123412341234123412341"},
      {"12341234123412341234123412341.234123412", 1,
       "12341234123412341234123412341.2"},
      {"12341234123412341234123412341.234123412", 8,
       "12341234123412341234123412341.23412341"},
      {"12341234123412341234123412341.234123412", 9,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", 10,
       "12341234123412341234123412341.234123412"},
      {"12341234123412341234123412341.234123412", kint64max,
       "12341234123412341234123412341.234123412"},

      {"56785678567856785678567856785.567856785", kint64min, 0},
      {"56785678567856785678567856785.567856785", -30, 0},
      {"56785678567856785678567856785.567856785", -29, kNumericOverflow},
      {"56785678567856785678567856785.567856785", -28,
       "60000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -27,
       "57000000000000000000000000000"},
      {"56785678567856785678567856785.567856785", -1,
       "56785678567856785678567856790"},
      {"56785678567856785678567856785.567856785", 0,
       "56785678567856785678567856786"},
      {"56785678567856785678567856785.567856785", 1,
       "56785678567856785678567856785.6"},
      {"56785678567856785678567856785.567856785", 8,
       "56785678567856785678567856785.56785679"},
      {"56785678567856785678567856785.567856785", 9,
       "56785678567856785678567856785.567856785"},
      {"56785678567856785678567856785.567856785", 10,
       "56785678567856785678567856785.567856785"},
      {"56785678567856785678567856785.567856785", kint64max,
       "56785678567856785678567856785.567856785"},

      {kMaxNumericValueStr, kint64min, 0},
      {kMaxNumericValueStr, -30, 0},
      {kMaxNumericValueStr, -29, kNumericOverflow},
      {kMaxNumericValueStr, -28, kNumericOverflow},
      {kMaxNumericValueStr, -1, kNumericOverflow},
      {kMaxNumericValueStr, 0, kNumericOverflow},
      {kMaxNumericValueStr, 1, kNumericOverflow},
      {kMaxNumericValueStr, 8, kNumericOverflow},
      {kMaxNumericValueStr, 9, kMaxNumericValueStr},
      {kMaxNumericValueStr, 10, kMaxNumericValueStr},
      {kMaxNumericValueStr, kint64max, kMaxNumericValueStr},
  };

  NumericRoundOp op;
  for (const NumericBinaryOpTestData<int64_t>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
  }
}

TEST_F(NumericValueTest, IntegerDivide) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {6, 2, 3},
      {1, 3, 0},
      {kMaxNumericValueStr, 3, "33333333333333333333333333333"},
      {kMaxNumericValueStr, 2, "49999999999999999999999999999"},
      {kMaxNumericValueStr, 1, "99999999999999999999999999999"},
      {5, "2.3", 2},
      {"5.2", 2, 2},
      {kMaxNumericValueStr, "0.3", kNumericOverflow},
      {"1e20", "1e-9", kNumericOverflow},

      {0, 0, kDivisionByZero},
      {"0.1", 0, kDivisionByZero},
      {1, 0, kDivisionByZero},
      {kMaxNumericValueStr, 0, kDivisionByZero},
  };

  NumericIntegerDivideOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, -data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, data.expected_output);
  }
}

TEST_F(NumericValueTest, Mod) {
  static constexpr NumericBinaryOpTestData<> kTestData[] = {
      {5, 2, 1},
      {5, "0.001", 0},
      {kMaxNumericValueStr, 3, "0.999999999"},
      {kMaxNumericValueStr, 2, "1.999999999"},
      {kMaxNumericValueStr, 1, "0.999999999"},
      {5, "2.3", "0.4"},
      {5, "0.3", "0.2"},
      {"5.2", 2, "1.2"},
  };

  NumericModOp op;
  for (const NumericBinaryOpTestData<>& data : kTestData) {
    TestBinaryOp(op, data.input1, data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, data.input2, -data.expected_output);
    TestBinaryOp(op, data.input1, -data.input2, data.expected_output);
    TestBinaryOp(op, -data.input1, -data.input2, -data.expected_output);
  }

  EXPECT_THAT(NumericValue(1).Mod(NumericValue()),
              StatusIs(zetasql_base::OUT_OF_RANGE, "division by zero: 1 / 0"));
}

TEST_F(NumericValueTest, Ceiling) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 1000},
      {"999.999999999", 1000},
      {-999, -999},
      {"-999.000000001", -999},
      {"-999.999999999", -999},
      {"0.999999999", 1},
      {"-0.999999999", 0},
      {kMinNumericValueStr, "-99999999999999999999999999999"},
      {kMaxNumericValueStr, kNumericOverflow},
      {"99999999999999999999999999999.000000001", kNumericOverflow},
  };

  NumericCeilingOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}

TEST_F(NumericValueTest, Floor) {
  static constexpr NumericUnaryOpTestData<> kTestData[] = {
      {0, 0},
      {999, 999},
      {"999.000000001", 999},
      {"999.999999999", 999},
      {-999, -999},
      {"-999.000000001", -1000},
      {"-999.999999999", -1000},
      {"0.999999999", 0},
      {"-0.999999999", -1},
      {kMaxNumericValueStr, "99999999999999999999999999999"},
      {kMinNumericValueStr, kNumericOverflow},
      {"-99999999999999999999999999999.000000001", kNumericOverflow},
  };

  NumericFloorOp op;
  for (const NumericUnaryOpTestData<>& data : kTestData) {
    TestUnaryOp(op, data.input, data.expected_output);
  }
}
}  // namespace
}  // namespace zetasql
