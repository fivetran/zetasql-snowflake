//
// Copyright 2019 Google LLC
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

#include "zetasql/compliance/runtime_expected_errors.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "zetasql/compliance/matchers.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/substitute.h"

namespace zetasql {

std::unique_ptr<MatcherCollection<absl::Status>> RuntimeExpectedErrorMatcher(
    std::string matcher_name) {
  // This is a list of runtime errors that will be ignored if they occur during
  // randomized testing.
  //
  // The test framework generates random data and random queries and, in so
  // doing, it is inevitable that such runtime errors will occur.  A simple
  // example is
  //   SELECT t.a/t.b FROM myTable AS T;
  // Such a query will fail with a division by 0 error if there is a row for
  // which b=0.
  //
  // Preventing such errors in the randomized test framework is possible in
  // principle but a complex challenge. Ingoring these runtime errors means we
  // could miss genuine bugs whereby runtime errors incorrectly occur.
  // However, that is not a common bug pattern compared to e.g. ZETASQL_RET_CHECKS and
  // incorrect results, and this is the balance we have currently struck.
  std::vector<std::unique_ptr<MatcherBase<absl::Status>>> error_matchers;
  // Runtime errors
  //
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bitwise binary operator for BYTES requires equal length of the inputs"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Output of REPEAT exceeds max allowed output size of 1MB"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Output of LPAD/RPAD exceeds max allowed output size of 1MB"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Output of TO_JSON_STRING exceeds max allowed output size of 1MB"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Output of TRANSLATE exceeds max allowed output size of 1MB"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Array index (-?[\\d]+) is out of bounds"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Corrupted protocol buffer"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot encode a null value(.*) in (required|repeated) protocol message "
      "field"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Elements in input array to RANGE_BUCKET must be in ascending order"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Elements in input array to RANGE_BUCKET"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid return_position_after_match"));

  // Out of range errors
  //
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "division by zero"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "(uint64_t|int64|int32_t|uint32|float|numeric|BIGNUMERIC) out of range: "));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Illegal conversion of non-finite floating point number to an integer"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Sequence step cannot be 0"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Truncating date (.+) to WEEK resulted in "
      "an out of range date value: (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Negative NUMERIC value cannot be raised to "
      "a fractional power"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Negative BIGNUMERIC value cannot be raised to "
      "a fractional power"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Input calculates to invalid (date|time)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid Proto3 (Timestamp|TimeOfDay) input"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Input is outside of Proto3 (TimeOfDay|Date) range"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "ErrorFunction_RQG"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Second argument in (LEFT|RIGHT)\\(\\) cannot be negative"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Occurrence must be positive"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Position must be non-zero"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Duplicate character (.*) in TRANSLATE source characters"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Duplicate byte 0x(.{2}) in TRANSLATE source bytes"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid encoded timestamp: (.*) with format: TIMESTAMP_(.*)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid non-int32_t date:"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid DATE_DECIMAL:"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Cannot set field of NULL"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot store a NULL element in repeated proto field (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Position must be positive"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid return_position_after_match"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot write NULL to key or value of map field"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Key not found in map"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "MODIFY_MAP: All key arguments must be non-NULL"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "MODIFY_MAP: Only one instance of each key is allowed"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid input to PARSE_NUMERIC"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid input to PARSE_BIGNUMERIC"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid input to PARSE_JSON"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "ARRAY_FIRST cannot get the first element of an empty array"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "ARRAY_LAST cannot get the last element of an empty array"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Occurrence in STRPOS cannot be less than 1"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "The n argument to ARRAY_(|REMOVE_)(FIRST|LAST)_N must not be negative"));

  // REPLACE_FIELDS() specific
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "REPLACE_FIELDS(.*) cannot be used to modify the fields of "
      "(a NULL valued|an unset) proto"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Cannot set field of NULL"));

  // Overflow errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "(uint32_t|int32|uint64_t|int64|integer|double|float|numeric|BIGNUMERIC) "
      "overflow"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Floating point overflow in function: (ACOS|ACOSH|ASIN|COSH|SINH)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Floating point error in function: "
      "(ACOS|ACOSH|ASIN|ATANH|COT|COTH|CSC|CSCH|EXP|LN|ABSL_LOG|LOG10|POW|SEC)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Argument to SQRT cannot be negative"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "(Adding|Subtracting) (-?\\d+) (\\w+) (to|from) "
      "(date|timestamp|datetime|time) (.+) causes overflow"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "LN is undefined for zero or negative value"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "LOG10 is undefined for zero or negative value"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "LOG is undefined for zero or negative value, or when base equals 1"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "SQRT is undefined for negative value"));

  // CASTing errors for un-castable values.
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bad (UINT32|INT32|UINT64|INT64|FLOAT|DOUBLE) value"));
  // TODO: Remove these lowercase options once engines have all moved
  // to the formal uppercase name
  // b/235365564: copybara currently adds the "_t" suffix to int32_t, int64_t, ...
  // etc. Unfortunately these messages use type->DebugString() instead of the
  // (capitalized) type name, and changing these messages is currently
  // infeasible due to the bug above. It is challenging as well to have copybara
  // parse C++. As a workaround, we're obfuscating the literal string to hide
  // matches from copybara (We have a special copybara rule for just passing
  // them enclosed directly in quotes)
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      absl::Substitute("Bad (u$032|$032|u$064|$064|float|double) value",
                       "int")));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid NUMERIC value"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid BIGNUMERIC value"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid INTERVAL value"));

  // Regex/Like Errors
  //
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Cannot parse regular expression:"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid REGEXP_REPLACE pattern"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "pattern too large"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Regular expressions passed into extraction functions must not have more "
      "than 1 capturing group"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "LIKE pattern ends with a backslash"));

  // TODO: known issue
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "PARSE_TIMESTAMP mismatch between format character '(.+)' and "
      "timestamp string character '(.+)'"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Regular expression passed to REGEXP_EXTRACT_ALL must not have "
      "more than 1 capturing group"));

  // Net function errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "NET\\.(FORMAT_IP|PARSE_IP|PARSE_PACKED_IP|FORMAT_PACKED_IP|IP_IN_NET|"
      "MAKE_NET|IP_FROM_STRING|IP_TO_STRING|IPV4_FROM_INT64|IPV4_TO_INT64)\\("
      "\\) encountered a(n?) (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "NET.MAKE_NET() is trying to expand a subnet"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "The (first|second) argument of NET\\.(IP_NET_MASK|IP_TRUNC)\\(\\) must "
      "(have|be) (.+)"));

  // Date/Time Value Errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid (time zone|timestamp|date|datetime|time)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid empty time zone"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Cannot truncate a TIMESTAMP_(.+) value to (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "DATE_FROM_UNIX_DATE range is -719162 to 2932896 but saw"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "TIMESTAMP_DIFF at (\\w+) precision between "
      "values of (-?\\d+) and (-?\\d+) causes overflow"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "TIMESTAMP_(ADD|SUB) interval value  (-?\\d+) at (\\w+) precision "
      "causes overflow"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Converting timestamp interval (-?\\d+) at TIMESTAMP_(\\w+) scale to "
      "TIMESTAMP_(\\w+) scale causes overflow"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid format: %(.*) is not allowed for "
      "the (DATE|DATETIME|TIME) type"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Timezone is not allowed in (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bucket width INTERVAL with nanoseconds precision is not allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Zero bucket width INTERVAL is not allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Negative bucket width INTERVAL is not allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bucket for .* is outside of (timestamp|datetime) range"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bucket width INTERVAL with non-zero MONTH part is not allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bucket width INTERVAL with mixed DAY and MICROSECOND parts is not "
      "allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Bucket width INTERVAL with mixed DAY and NANOSECOND parts is not "
      "allowed"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Exactly one non-zero INTERVAL part in bucket width is required"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Only MONTH and DAY parts are allowed in bucket width INTERVAL"));

  // Interval Errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Interval field .* is out of range"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid interval"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "TIMESTAMP +/- INTERVAL is not supported for intervals with non-zero"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Interval overflow during multiplication"));

  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Illegal non-space trailing data"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Failed to parse input string"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Format string cannot end with a single '%'"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Mismatch between format character"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid ASCII value"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid codepoint"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Argument to ASCII is not a structurally valid ASCII string"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Failed to decode invalid hexadecimal string"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Input value (-?\\d+) cannot be converted into a TIMESTAMP, "
      "because it would be out of the allowed range between "
      "(-\\d+) to (\\d+) \\(microseconds\\)"));

  // PercentileDisc function.
  // TODO: Support array args in percentile_disc
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kUnimplemented,
      "Unsupported argument type for percentile_disc."));

  // Parsing and analysis errors.
  //
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Stack overflow while trying to parse"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Could not cast (.+) to type (UINT32|INT32|UINT64|INT64|FLOAT|DOUBLE|"
      "TIMESTAMP|DATE|DATETIME|TIME|STRING|BOOL|NUMERIC|BIGNUMERIC|JSON|"
      "INTERVAL)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Bad BOOL value:"));
  // TODO: Remove these lowercase options once engines have all moved
  // to the formal uppercase name
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Bad bool value:"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Invalid cast of bytes to UTF8 string"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid cast of bytes to UTF8 string"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Third argument (pad pattern) for LPAD/RPAD cannot be empty"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Update item (.+) overlaps with (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "(.+) does not support (.+) date part (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "A valid date part name is required(.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Found invalid date part argument syntax (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Found invalid date part argument function call syntax for (.+)"));
  // TODO: array<proto> literals don't round trip in the SQL Builder
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Argument 3 to (LAG|LEAD) must be a literal or query parameter"));
  // TODO: Remove the following two once the bug is resolved.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "ARRAY_IS_DISTINCT cannot be used on argument of type .* because the "
      "array's element type does not support grouping"));

  // TODO: Remove after the bug is fixed.
  // Due to the above expected errors, rqg could generate invalid expressions.
  // An invalid lambda body manifests as "No matching signature".
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "No matching signature for function "
      "(ARRAY_FILTER|ARRAY_TRANSFORM|ARRAY_INCLUDES|ARRAY_FIND|ARRAY_FIND_ALL|"
      "ARRAY_OFFSET|ARRAY_OFFSETS) .*"));

  // HLL sketch format errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid or incompatible sketch in HLL_COUNT\\.(.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid sketch in HLL_COUNT\\.(.+)"));

  // D3A sketch format errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid sketch in D3A_COUNT\\.(.+)"));

  // D3A weight overflow errors
  //
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument, "Grid counters overflowed.*"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument, "Invalid weight value.*"));

  // GEOGRAPHY related errors
  //
  // GEOGRAPHY constructors errors (not converted from S2Error).
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Input geographies for "));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "cannot include an empty geography"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "Failed to find geography format for the input."));
  // All GEOGRAPHY STLIB errors converted from S2Error.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "[stlib]"));
  // Unimplemented functions
  // TODO: implement
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kUnimplemented, "Precision argument of ST_AsGeoJSON "));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "(ST_BUFFER|ST_BUFFERWITHTOLERANCE) does not yet implement "
      "use_spheroid=true"));

  // PROTO_NULL_IF_UNSET() analysis errors from Protobuf fields without defaults
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "The field accessed by PROTO_DEFAULT_IF_NULL must have a usable default "
      "value; Field (.+) is annotated to ignore proto defaults"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "The PROTO_DEFAULT_IF_NULL input expression cannot access a field with "
      "type message; Field (.+) is of message type"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "The field accessed by PROTO_DEFAULT_IF_NULL input expression cannot "
      "access a required field; Field (.+) is required"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "The PROTO_DEFAULT_IF_NULL function does not accept expressions that "
      "result in a 'has_' virtual field access"));
  // REPLACE_FIELDS() specific
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "REPLACE_FIELDS(.+) field path (.+) overlaps with field path (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Modifying multiple fields from the same OneOf is unsupported by "
      "REPLACE_FIELDS()"));

  // COLLATION related errors
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "(ResolvedFunctionCallBase::collation_list not accessed and has "
      "non-default value)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "The second argument of COLLATE\\(\\) must be a string literal"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation for non-String type is not supported"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation is not supported for function: (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation conflict: \"(.+)\" vs. \"(.+)\". Collation on argument (.+) "
      "(.+) in function (.+) is not compatible with other arguments"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation is not allowed on input array to FLATTEN (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation conflict: \"(.+)\" vs. \"(.+)\". Collation for IN operator is "
      "different on input expr (.+) and subquery column (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation is not allowed on argument (.+) (.+)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation is not supported for aggregate function (.+) without "
      "DISTINCT"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation conflict: \"(.+)\" vs. \"(.+)\"; in column (.+), item (.+) of "
      "set operation scan"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "Collation is not supported in recursive queries"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Collation is not supported in a PIVOT clause yet"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Collation (.+) is not supported on argument (.+) of aggregate function "
      "in a PIVOT clause"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Order by item ((.|\\n)+) with collation (.+) in function (.+) is not "
      "supported"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kUnimplemented,
      "Analytic function (.+) with collation (.+) is not supported"));
  // TODO Remove this exemption once we have figured out why it
  //                   happens.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInternal,
      "group_boundary.end_tuple_id < static_cast<int>"));

  // LIKE with collation does not support '_' in the pattern.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "LIKE pattern has '_' which is not allowed when its operands have "
      "collation:(.+)"));

  // TODO Remove this after code is updated to kOutOfRange
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kInvalidArgument,
      "syntax error while parsing (value|array|object|object key|object "
      "separator)"));

  // JSON related errors
  // TO_JSON will return OUT_OF_RANGE error if the input type is
  // numeric/bignumeric and FEATURE_JSON_STRICT_NUMBER_PARSING is enabled when
  // there is precision loss.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Failed to convert type (NUMERIC|BIGNUMERIC) to JSON"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "The provided JSON input is not (a string|a boolean|an integer|a "
      "number)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "JSON number: (-?\\d+) cannot be converted to DOUBLE without loss of "
      "precision"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "The provided JSON number: .+ cannot be converted to an integer"));
  // TODO PARSE_JSON sometimes is generated with invalid string
  // inputs.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "syntax error while parsing (value|array|object|object key|object "
      "separator)"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid `wide_number_mode` specified"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid input to JSON_(REMOVE|SET)"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange, "Invalid input to JSON_STRIP_NULLS"));
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "Invalid input to JSON_ARRAY_(INSERT|APPEND)"));

  return std::make_unique<MatcherCollection<absl::Status>>(
      matcher_name, std::move(error_matchers));
}

std::unique_ptr<MatcherCollection<absl::Status>> RuntimeDMLExpectedErrorMatcher(
    std::string matcher_name) {
  // Legitimate errors that are difficult to prevent when generating queries.
  std::vector<std::unique_ptr<MatcherBase<absl::Status>>> error_matchers;

  // OR X errors when there is no primary key.
  // We want the RQG to generate these statements to ensure engines
  // return the same errors as reference impl.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "INSERT OR IGNORE is not allowed because the table does not have a "
      "primary key"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "INSERT OR UPDATE is not allowed because the table does not have a "
      "primary key"));
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "INSERT OR REPLACE is not allowed because the table does not have a "
      "primary key"));
  // This error can happen if multiple of the same key is added
  // to the same insert.  Even though we attempt to avoid duplicates as best
  // we can, there is still a chance different AST trees can resolve
  // to the same value.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "was already created in this transaction."));
  // ALREADY_EXISTS can happen if our duplicate checking during DML insert
  // fails or if a row already exists in the table for an inserted row. We do
  // our best to avoid duplicates, but since the insert is random, errors will
  // happen. Note that the message only contains the name of the table which
  // is not deterministic.
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kAlreadyExists, "^(\\w+)\\(.+\\)"));

  // Error when ASSERT ROWS MODIFIED failed count
  error_matchers.emplace_back(std::make_unique<StatusRegexMatcher>(
      absl::StatusCode::kOutOfRange,
      "ASSERT_ROWS_MODIFIED expected (.*) rows modified, but found (.*)"));

  // This error can happen on inserts on random schemas where the insert
  // is performed on a child table with a key that does not exist in the
  // parent table.
  // TODO: b/109660988 DML RQG: account for constraints
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kNotFound, "Parent row is missing"));

  // TODO: Not yet implemented/supported.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kUnimplemented,
      "does not yet implement use_spheroid=true"));

  // TODO Remove this after code is updated to kOutOfRange
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kInvalidArgument,
      "syntax error while parsing (value|array|object|object key|object "
      "separator) - "));
  // TODO PARSE_JSON sometimes is generated with invalid string
  // inputs.
  error_matchers.emplace_back(std::make_unique<StatusSubstringMatcher>(
      absl::StatusCode::kOutOfRange,
      "syntax error while parsing (value|array|object|object key|object "
      "separator) - "));

  error_matchers.emplace_back(RuntimeExpectedErrorMatcher("RuntimeErrors"));
  return std::make_unique<MatcherCollection<absl::Status>>(
      matcher_name, std::move(error_matchers));
}

}  // namespace zetasql
