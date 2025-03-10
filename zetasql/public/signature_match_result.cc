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

#include "zetasql/public/signature_match_result.h"

#include <string>
#include <utility>
#include <vector>

#include "zetasql/public/type.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace zetasql {

bool SignatureMatchResult::IsCloserMatchThan(
    const SignatureMatchResult& other_result) const {
  if (non_literals_coerced_ != other_result.non_literals_coerced_) {
    return non_literals_coerced_ < other_result.non_literals_coerced_;
  }
  if (non_literals_distance_ != other_result.non_literals_distance_) {
    return non_literals_distance_ < other_result.non_literals_distance_;
  }
  if (literals_coerced_ != other_result.literals_coerced_) {
    return literals_coerced_ < other_result.literals_coerced_;
  }
  return literals_distance_ < other_result.literals_distance_;
}


void SignatureMatchResult::UpdateFromResult(
    const SignatureMatchResult& other_result) {
  non_matched_arguments_ += other_result.non_matched_arguments_;
  non_literals_coerced_ += other_result.non_literals_coerced_;
  non_literals_distance_ += other_result.non_literals_distance_;
  literals_coerced_ += other_result.literals_coerced_;
  literals_distance_ += other_result.literals_distance_;
  mismatch_message_ = other_result.mismatch_message();
  tvf_mismatch_message_ = other_result.tvf_mismatch_message();
  tvf_bad_argument_index_ = other_result.tvf_bad_argument_index();
  tvf_relation_coercion_map_ = other_result.tvf_relation_coercion_map_;
}

std::string SignatureMatchResult::DebugString() const {
  std::string result =
      absl::StrCat("non-matched arguments: ", non_matched_arguments_,
                   ", non-literals coerced: ", non_literals_coerced_,
                   ", non-literals distance: ", non_literals_distance_,
                   ", literals coerced: ", literals_coerced_,
                   ", literals distance: ", literals_distance_);
  if (!mismatch_message_.empty()) {
    absl::StrAppend(&result, ", mismatch message: \"", mismatch_message_, "\"");
  }
  if (!tvf_relation_coercion_map_.empty()) {
    std::vector<std::string> entries;
    for (const auto& [key, value] : tvf_relation_coercion_map_) {
      entries.push_back(absl::StrCat("(arg: ", key.argument_index,
                                     ", col: ", key.column_index, ")->",
                                     value->DebugString(true)));
    }
    absl::StrAppend(&result, "\", tvf relation coercion map: [",
                    absl::StrJoin(entries, ", "), "]");
  }
  return result;
}

}  // namespace zetasql
