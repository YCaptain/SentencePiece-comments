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

#ifndef MODEL_INTERFACE_H_
#define MODEL_INTERFACE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common.h"
#include "normalizer.h"
#include "sentencepiece_model.pb.h"
#include "sentencepiece_processor.h"
#include "third_party/absl/strings/string_view.h"
#include "third_party/darts_clone/darts.h"
#include "util.h"

namespace sentencepiece {

// DOC:
// 将给定的文本按词分割。
// 参数:
//      add_ws_as_suffix -- true将空格视为后缀，false则视为前缀
//
// 返回:
//      分割后的文本。
// "_this_is_a_pen" => ["_this", "_is", "_a", "_pen"]
std::vector<absl::string_view> SplitIntoWords(absl::string_view text,
                                              bool add_ws_as_suffix = false);


// DOC:
// 编码结果类型
// 一组最优编码结果类型
using EncodeResult = std::vector<std::pair<absl::string_view, int>>;
using NBestEncodeResult = std::vector<std::pair<EncodeResult, float>>;

// 模型原型
class ModelProto;

// DOC:
// 底层的模型接口。
//      给定一个规范化的字符串，返回一个用id标识的句子分词序列。
// Underlying model interface.
// Given a normalized string, returns a sequence of sentence pieces with ids.
class ModelInterface {
 public:
  using PieceToIdMap =
      std::unordered_map<absl::string_view, int, string_util::string_view_hash>;

  // unk: unknown token -- 未知标记
  // bos: end of sentence -- 句子末尾
  // eos: begin of sentence -- 句子开头
  // pad: 用于填充片段的部分
  absl::string_view unk_piece() const;
  absl::string_view bos_piece() const;
  absl::string_view eos_piece() const;
  absl::string_view pad_piece() const;

  // 在销毁ModelInterface前不应删除`model_proto`
  // `model_proto` should not be deleted until ModelInterface is destroyed.
  explicit ModelInterface(const ModelProto &model_proto);
  ModelInterface() {}

  virtual ~ModelInterface();

  // DOC:
  // 返回status，表示编码/解码(Encode/Decode)函数是否可用
  //
  // 返回:
  //        status
  // Returns Status.
  // Encode/Decode functions are valid only when status is OK.
  virtual util::Status status() const { return status_; }

  // DOC:
  // 返回模型原型地址。
  //
  // 返回:
  //        model_proto_地址
  virtual const ModelProto &model_proto() const { return *model_proto_; }

  // DOC:
  // 返回前缀匹配器地址。
  //
  // 返回:
  //        prefix_matcher地址
  virtual const normalizer::PrefixMatcher *prefix_matcher() const {
    return matcher_.get();
  }

  // DOC:
  // [纯虚函数]输入一条正规化后的字符串，返回一组带id的句子片段。
  //
  // 参数:
  //        normalized -- 正规化后的字符串
  //
  // 返回:
  //        一组带id的句子片段。
  // Given a normalized string, returns a sequence of sentence pieces with ids.
  // The concatenation of pieces must be the same as `normalized`.
  virtual EncodeResult Encode(absl::string_view normalized) const = 0;

  // DOC:
  // 给定一个规范化的字符串，返回一个用id标识的句子分词序列和分数。
  //
  // 参数:
  //        normalized -- 正规化后的字符串
  //        nbest_size -- 最优句子片段组个数
  //
  // 返回:
  //        id标识的句子分词序列和分数。
  // The same as above, but returns nbest result with score.
  virtual NBestEncodeResult NBestEncode(absl::string_view normalized,
                                        int nbest_size) const {
    LOG(ERROR) << "Not implemented.";
    return NBestEncodeResult();
  }

  // DOC:
  // 样例编码API，返回句子片段。
  virtual EncodeResult SampleEncode(absl::string_view normalized,
                                    float alpha) const {
    LOG(ERROR) << "Not implemented.";
    return EncodeResult();
  }

  // DOC:
  // 获取词对应的id并返回，如果词未定义则返回UNK(0)
  // 参数:
  //        piece -- 句子片段
  //
  // 返回:
  //        获取词对应的id并返回，如果词未定义则返回UNK(0)
  // Returns the vocab id of `piece`.
  // Returns UNK(0) if `piece` is unknown
  virtual int PieceToId(absl::string_view piece) const;

  // DOC:
  // 返回id对应的文本表示。
  // id必须在[0, GetPieceSize())区间内。
  // 参数:
  //        id -- 分词id
  //
  // 返回:
  //        id对应的文本表示
  // Returns the string representation of vocab with `id`.
  // id must be 0 <= id < GetPieceSize().
  virtual const std::string &IdToPiece(int id) const {
    return model_proto_->pieces(id).piece();
  }

  // DOC:
  // 返回句子片段大小。
  //
  // 返回:
  //        句子片段大小。
  // Returns the size of sentence pieces, which is the same
  // as the size of vocabulary for NMT.
  virtual int GetPieceSize() const { return model_proto_->pieces_size(); }

  // DOC:
  // 返回id对应的词的分数。
  //
  // 参数:
  //        id -- 需要返回分数的id
  //
  // 返回:
  //        id对应的词的分数。
  // Returns the score of `id`.
  // Score represents a log probability of the piece.
  // We can roughly estimate the unigram frequency of the piece.
  virtual float GetScore(int id) const {
    return model_proto_->pieces(id).score();
  }

  // DOC:
  // 判断id是否为未知(unknown)标记
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为未知标记返回true，否则返回false。
  // Returns true if `id` is unknown symbol.
  virtual bool IsUnknown(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::UNKNOWN);
  }

  // DOC:
  // 判断id是否为控制(control)标记
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为控制标记返回true，否则返回false。
  // Returns true if `id` is control symbol.
  virtual bool IsControl(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::CONTROL);
  }

  // DOC:
  // 判断id是否为控制(control)标记
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为控制标记返回true，否则返回false。
  // Returns true if `id` is unused symbol.
  virtual bool IsUnused(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::UNUSED);
  }

  // DOC:
  // 判断id是否为自定义(user defined)标记
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为自定义标记返回true，否则返回false。
  // Returns true if `id` is user defined symbol.
  virtual bool IsUserDefined(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::USER_DEFINED);
  }

 protected:
  void InitializePieces();

  // DOC:
  // 返回id分数(为提高性能的内联实现)
  //
  // 参数:
  //        id -- 需要返回分数的id
  //
  // 返回:
  //        id分数。
  // Non-virtual (inlined) implementation for faster execution.
  inline float GetScoreInlined(int id) const {
    return model_proto_->pieces(id).score();
  }

  // DOC:
  // 判断id是否为未知(unknown)标记(为提高性能的内联实现)
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为未知标记返回true，否则返回false。
  inline bool IsUnknownInlined(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::UNKNOWN);
  }

  // DOC:
  // 判断id是否为控制(control)标记(为提高性能的内联实现)
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为控制标记返回true，否则返回false。
  inline bool IsControlInlined(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::CONTROL);
  }

  // DOC:
  // 判断id是否为自定义(user defined)标记(为提高性能的内联实现)
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为自定义标记返回true，否则返回false。
  inline bool IsUnusedInlined(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::UNUSED);
  }

  // DOC:
  // 判断id是否为自定义(user defined)标记(为提高性能的内联实现)
  //
  // 参数:
  //        id -- 需要被判断的id
  //
  // 返回:
  //        id为自定义标记返回true，否则返回false。
  // Returns true if `id` is user defined symbol.
  inline bool IsUserDefinedInlined(int id) const {
    return (model_proto_->pieces(id).type() ==
            ModelProto::SentencePiece::USER_DEFINED);
  }

  const ModelProto *model_proto_ = nullptr;

  // 用于匹配用户定义标记的前缀匹配器
  // PrefixMatcher for user defined symbols.
  // 对用户定义符号的前缀匹配器。
  std::unique_ptr<normalizer::PrefixMatcher> matcher_;


  // 对于常规的词的piece->id的键值表。
  // piece -> id map for normal pieces
  PieceToIdMap pieces_;

  // 对于控制符和未知字的piece->id的键值表。

  // piece -> id map for control and unknown
  PieceToIdMap reserved_id_map_;

  // 未知标记id
  // unknown id.
  int unk_id_ = 0;

  // status.
  util::Status status_;
};
}  // namespace sentencepiece
#endif  // MODEL_INTERFACE_H_
