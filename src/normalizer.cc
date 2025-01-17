// Copyright 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.!

#include "normalizer.h"

#include <utility>
#include <vector>
#include "common.h"
#include "third_party/absl/strings/string_view.h"
#include "third_party/darts_clone/darts.h"
#include "util.h"

namespace sentencepiece {
namespace normalizer {

constexpr int Normalizer::kMaxTrieResultsSize;

// DOC:
// Normalizer(规范化器)类的构造函数，用于初始化 Normalizer 类对象。
// 参数：
//     const NormalizerSpec &spec -- 用于初始化 Normalizer类对象中的 spec_ 成员变量。
//     const TrainerSpec &trainer_spec -- 用于初始化 Normalizer 类对象中的 treat_whitespace_as_suffix_ 成员变量。
Normalizer::Normalizer(const NormalizerSpec &spec,
                       const TrainerSpec &trainer_spec)
    : spec_(&spec),
      treat_whitespace_as_suffix_(trainer_spec.treat_whitespace_as_suffix()),
      status_(util::OkStatus()) {
  Init();
}

// DOC:
// Normalizer(规范化器)类的构造函数，用于初始化 Normalizer 类对象。
// 参数：
//     const NormalizerSpec &spec -- 用于初始化 Normalizer 类对象中的 spec_ 成员变量。
Normalizer::Normalizer(const NormalizerSpec &spec)
    : spec_(&spec), status_(util::OkStatus()) {
  Init();
}

// DOC:
// Normalizer(规范化器)类的析构造函数。
// 参数：
//      无
Normalizer::~Normalizer() {}

// DOC:
// 对Normalizer(规范化器)类的status_、trie_和normalized_成员变量进行初始化。
// 参数：
//      无
void Normalizer::Init() {
  absl::string_view index = spec_->precompiled_charsmap();
  if (index.empty()) {
    LOG(INFO) << "precompiled_charsmap is empty. use identity normalization.";
  } else {
    absl::string_view trie_blob, normalized;
    status_ = DecodePrecompiledCharsMap(index, &trie_blob, &normalized);
    if (!status_.ok()) return;

    // Reads the body of double array.
    trie_ = port::MakeUnique<Darts::DoubleArray>();

    // The second arg of set_array is not the size of blob,
    // but the number of double array units.
    trie_->set_array(const_cast<char *>(trie_blob.data()),
                     trie_blob.size() / trie_->unit_size());

    normalized_ = normalized.data();
  }
}

// DOC:
// 对Normalizer(规范化器)类的status_、trie_和normalized_成员变量进行初始化。
// 参数：
//      absl::string_view input -- 待规范的字符串样本
//      std::string *normalized -- 可供规范化操作的字符串指针
//      std::vector<size_t> *norm_to_orig -- 存储字符串字节对齐 size_t 数据的 vector
util::Status Normalizer::Normalize(absl::string_view input,
                                   std::string *normalized,
                                   std::vector<size_t> *norm_to_orig) const {
  norm_to_orig->clear();
  normalized->clear();

  if (input.empty()) {
    return util::OkStatus();
  }

  RETURN_IF_ERROR(status());

  int consumed = 0;

  // Ignores heading space.
  if (spec_->remove_extra_whitespaces()) {
    while (!input.empty()) {
      const auto p = NormalizePrefix(input);    // p 为 std::pair<absl::string_view, int>
      if (p.first != " ") {
        break;
      }
      input.remove_prefix(p.second);            // remove_prefix：将 input 的开头向前移动 p.second 个字符。
      consumed += p.second;                     // 记录下被移除的空格数量
    }
  }

  // all chars are whitespace.
  if (input.empty()) {
    return util::OkStatus();
  }

  // Reserves the output buffer to avoid re-allocations.
  // 保留输出缓冲区以避免重新分配。
  const size_t kReservedSize = input.size() * 3;
  normalized->reserve(kReservedSize);   // string::reserve:要求字符串容量适应计划的大小更改，最大长度为 kReservedSize 个字符。
                                        // 如果 kReservedSize 大于当前字符串容量，该函数将使容器的容量增加到 kReservedSize 个字符（或更大）。
  norm_to_orig->reserve(kReservedSize);

  // Replaces white space with U+2581 (LOWER ONE EIGHT BLOCK)
  // if escape_whitespaces() is set (default = true).
  const absl::string_view kSpaceSymbol = "\xe2\x96\x81";

  // adds kSpaceSymbol to the current context.
  // 将 kSpaceSymbol 加入到当前文章
  auto add_ws = [this, &consumed, &normalized, &norm_to_orig, &kSpaceSymbol]() {
    if (spec_->escape_whitespaces()) {
      normalized->append(kSpaceSymbol.data(), kSpaceSymbol.size());
      // 由 C++ 字符串得到对应的 C_string 的方法是使用 data()
      // data() 以字符数组的形式返回字符串内容，但并不添加'\0'.
      // 此处 append 应使用的是 append( const char *str, size_type num );
      // 意为在字符串的末尾添加 str 中的 num 个字符
      for (size_t n = 0; n < kSpaceSymbol.size(); ++n) {
        norm_to_orig->push_back(consumed);
        // push_back：新的元素加到 vector 的最后面，位置为当前最后一个元素的下一个元素，新的元素的值是 val 的拷贝（或者是移动拷贝）
      }
    } else {
      normalized->append(" ");
      norm_to_orig->push_back(consumed);
    }
  };

  // Adds a space symbol as a prefix (default is true)
  // With this prefix, "world" and "hello world" are converted into
  // "_world" and "_hello_world", which help the trainer to extract
  // "_world" as one symbol.
  if (!treat_whitespace_as_suffix_ && spec_->add_dummy_prefix()) add_ws();

  bool is_prev_space = spec_->remove_extra_whitespaces();
  while (!input.empty()) {
    auto p = NormalizePrefix(input);
    absl::string_view sp = p.first;

    // Removes heading spaces in sentence piece,
    // if the previous sentence piece ends with whitespace.
    while (is_prev_space && string_util::ConsumePrefix(&sp, " ")) {
    }

    if (!sp.empty()) {
      const char *data = sp.data();
      for (size_t n = 0; n < sp.size(); ++n) {
        if (spec_->escape_whitespaces() && data[n] == ' ') {
          // replace ' ' with kSpaceSymbol.
          normalized->append(kSpaceSymbol.data(), kSpaceSymbol.size());
          for (size_t m = 0; m < kSpaceSymbol.size(); ++m) {
            norm_to_orig->push_back(consumed);
          }
        } else {
          *normalized += data[n];
          norm_to_orig->push_back(consumed);
        }
      }
      // Checks whether the last character of sp is whitespace.
      is_prev_space = string_util::EndsWith(sp, " ");
    }

    consumed += p.second;
    input.remove_prefix(p.second);
    if (!spec_->remove_extra_whitespaces()) {
      is_prev_space = false;
    }
  }

  // Ignores tailing space.
  if (spec_->remove_extra_whitespaces()) {
    const absl::string_view space =
        spec_->escape_whitespaces() ? kSpaceSymbol : " ";
    while (string_util::EndsWith(*normalized, space)) {
      const int length = normalized->size() - space.size();
      CHECK_GE_OR_RETURN(length, 0);
      consumed = (*norm_to_orig)[length];
      normalized->resize(length);
      norm_to_orig->resize(length);
    }
  }

  // Adds a space symbol as a suffix (default is false)
  if (treat_whitespace_as_suffix_ && spec_->add_dummy_prefix()) add_ws();

  norm_to_orig->push_back(consumed);

  CHECK_EQ_OR_RETURN(norm_to_orig->size(), normalized->size() + 1);

  return util::OkStatus();
}

// DOC：
// 返回未经对齐的初始化字符串，适用于 sentencepiece 训练
// 参数：
//      absl::string_view input -- 需要被规范化前缀的输入的字符串。
std::string Normalizer::Normalize(absl::string_view input) const {
  std::vector<size_t> norm_to_orig;
  std::string normalized;
  Normalize(input, &normalized, &norm_to_orig);
  return normalized;
}

// DOC：
// 规范化输入字符串的前缀，并返回一个 std::pair<absl::string_view, int> 类型对象
// 包含规范化的前缀和前缀的长度
// 参数：
//      absl::string_view input -- 需要被规范化前缀的输入的字符串。
// 返回值：
//      std::pair<absl::string_view, int> -- 包含规范化的前缀和前缀的长度的 std::pair 对象
std::pair<absl::string_view, int> Normalizer::NormalizePrefix(
    absl::string_view input) const {
  std::pair<absl::string_view, int> result;

  // DOC：
  // 若 input 为空直接返回空的 result
  if (input.empty()) return result;

  if (matcher_ != nullptr) {
    bool found = false;
    const int mblen = matcher_->PrefixMatch(input, &found);
    if (found) return std::make_pair(input.substr(0, mblen), mblen);
  }

  size_t longest_length = 0;
  int longest_value = 0;

  if (trie_ != nullptr) {
    // Allocates trie_results in stack, which makes the encoding speed 36%
    // faster. (38k sentences/sec => 60k sentences/sec). Builder checks that the
    // result size never exceeds kMaxTrieResultsSize. This array consumes
    // 0.5kByte in stack, which is less than default stack frames (16kByte).
    // 在堆栈中分配 trie_results，使编码速度提高 36%。（每秒 38K 句 => 每秒 60K 句）。
    // 生成器检查结果大小是否从不超过 KmaxtrieResultsSize。此数组在堆栈中消耗 0.5kbyte，这小于默认堆栈帧（16kbyte）。
    Darts::DoubleArray::result_pair_type
        trie_results[Normalizer::kMaxTrieResultsSize];

    const size_t num_nodes = trie_->commonPrefixSearch(
        input.data(), trie_results, Normalizer::kMaxTrieResultsSize,
        input.size());

    // Finds the longest rule.
    for (size_t k = 0; k < num_nodes; ++k) {
      if (longest_length == 0 || trie_results[k].length > longest_length) {
        longest_length = trie_results[k].length;  // length of prefix
        longest_value = trie_results[k].value;    // pointer to |normalized_|.
      }
    }
  }

  if (longest_length == 0) {
    size_t length = 0;
    if (!string_util::IsValidDecodeUTF8(input, &length)) {
      // Found a malformed utf8.
      // The rune is set to be 0xFFFD (REPLACEMENT CHARACTER),
      // which is a valid Unicode of three bytes in utf8,
      // but here we only consume one byte.
      // 找到一个格式不正确的 utf8。
      // rune 被设置为 0xfffd（替换字符），
      // 这是一个有效的 unicode，在 utf8 中为三个字节，
      // 但这里我们只使用一个字节。

      //DOC:
      //若找到一个格式不正确的 utf8,将其设置为"\xEF\xBF\xBD"（0xfffd），并将 result 中它的长度设置为1字节。
      result.second = 1;
      static const char kReplacementChar[] = "\xEF\xBF\xBD";
      result.first = absl::string_view(kReplacementChar);
    } else {
      result.second = length;
      result.first = absl::string_view(input.data(), result.second);
    }
  } else {
    result.second = longest_length;
    // No need to pass the size of normalized sentence,
    // since |normalized| is delimitered by "\0".
    result.first = absl::string_view(&normalized_[longest_value]);
  }

  return result;
}

// static
// DOC:
// 将 trie_blob 与规范化字符串进行编码，返回编码后的字符串
std::string Normalizer::EncodePrecompiledCharsMap(
    absl::string_view trie_blob, absl::string_view normalized) {
  // <trie size(4byte)><double array trie><normalized string>
  std::string blob;
  blob.append(string_util::EncodePOD<uint32>(trie_blob.size()));
  blob.append(trie_blob.data(), trie_blob.size());
  blob.append(normalized.data(), normalized.size());
  return blob;
}

// static
// DOC:
// 将编码后的字符串进行解码，返回 trie_blob 与规范化字符串
util::Status Normalizer::DecodePrecompiledCharsMap(
    absl::string_view blob, absl::string_view *trie_blob,
    absl::string_view *normalized) {
  uint32 trie_blob_size = 0;
  if (blob.size() <= sizeof(trie_blob_size) ||
      !string_util::DecodePOD<uint32>(
          absl::string_view(blob.data(), sizeof(trie_blob_size)),
          &trie_blob_size) ||
      trie_blob_size >= blob.size()) {
    return util::InternalError("Blob for normalization rule is broken.");
  }

  blob.remove_prefix(sizeof(trie_blob_size));
  *trie_blob = absl::string_view(blob.data(), trie_blob_size);

  blob.remove_prefix(trie_blob_size);
  *normalized = absl::string_view(blob.data(), blob.size());

  return util::OkStatus();
}

// DOC:
// PrefixMatcher(前缀匹配器)类的构造函数，用于初始化前缀匹配器。
// 参数：
//      const std::set<absl::string_view> &dic  -- 含有用于初始化前缀匹配器类对象已排序的 absl::string_view 类型字符串的关联容器。
PrefixMatcher::PrefixMatcher(const std::set<absl::string_view> &dic) {
  if (dic.empty()) return;
  std::vector<const char *> key;
  key.reserve(dic.size());
  for (const auto &it : dic) key.push_back(it.data());
  trie_ = port::MakeUnique<Darts::DoubleArray>();
  CHECK_EQ(0, trie_->build(key.size(), const_cast<char **>(&key[0]), nullptr,
                           nullptr));
}

// DOC:
// 用于寻找 trie_ 指针所指向的 DoubleArray 内的字符串中最长，且有指定的要查找的前缀的的字符串。
// 如果没有找到匹配的字符串则将 found 设置为 false，同时返回一个 Unicode 字符的长度。
// 如果找到则将 found 设置为 true，同时返回寻找到的匹配的字符串的 UTF8 编码长度
// 参数：
//      bool *found -- 用于表示寻找的结果，没有找到匹配的字符串则将 found 设置为 false，找到则将 found 设置为 true。
//      absl::string_view w -- 指定的要查找的前缀。
// 返回值：
//      int -- 寻找到的匹配的字符串的UTF8编码长度。
int PrefixMatcher::PrefixMatch(absl::string_view w, bool *found) const {
  if (trie_ == nullptr) {
    if (found) *found = false;
    return std::min<int>(w.size(), string_util::OneCharLen(w.data()));
  }

  constexpr int kResultSize = 64;
  Darts::DoubleArray::result_pair_type trie_results[kResultSize];
  const int num_nodes =
      trie_->commonPrefixSearch(w.data(), trie_results, kResultSize, w.size());

  if (found) *found = (num_nodes > 0);
  if (num_nodes == 0) {
    return std::min<int>(w.size(), string_util::OneCharLen(w.data()));
  }

  int mblen = 0;
  for (int i = 0; i < num_nodes; ++i) {
    mblen = std::max<int>(trie_results[i].length, mblen);
  }

  return mblen;
}

// DOC:
// 将“w”中的条目替换为“out”
// 参数：
//      absl::string_view w -- 被替换的字符串。
//      absl::string_view out -- 用于替换的字符串。
std::string PrefixMatcher::GlobalReplace(absl::string_view w,
                                         absl::string_view out) const {
  std::string result;
  while (!w.empty()) {
    bool found = false;
    const int mblen = PrefixMatch(w, &found);
    if (found) {
      result.append(out.data(), out.size());
    } else {
      result.append(w.data(), mblen);
    }
    w.remove_prefix(mblen);
  }
  return result;
}

}  // namespace normalizer
}  // namespace sentencepiece
