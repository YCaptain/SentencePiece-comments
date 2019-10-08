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

#include "unicode_script.h"
#include <unordered_map>
#include "unicode_script_map.h"
#include "util.h"

// DOC: �����ռ� sentencepiece
namespace sentencepiece {
// DOC: �����ռ� sentencepiece::unicode_script
namespace unicode_script {
namespace {
// DOC:
// ���������ַ��������
class GetScriptInternal {
 public:
// DOC:
// GetScriptInternal �๹�캯�� ��ʼ���ַ�����
// 
// ����:
//      smap_ -- �ַ�����洢����������
  GetScriptInternal() { InitTable(&smap_); }

  // DOC:
  // ��ȡ�ַ������ַ�������
  // 
  // ����:
  //      c -- ���жϵ��ַ�
  ScriptType GetScript(char32 c) const {
	// DOC:
	// ������� STL map ��ͨ�ò��ҷ���
    return port::FindWithDefault(smap_, c, ScriptType::U_Common);
  }

 private:
  // ���Ա�
  std::unordered_map<char32, ScriptType> smap_;
};
}  // namespace

// DOC:
// ��ȡ�ַ������ַ�������
// 
// ����:
//      c -- ���жϵ��ַ�
ScriptType GetScript(char32 c) {
  static GetScriptInternal sc;
  return sc.GetScript(c);
}
}  // namespace unicode_script
}  // namespace sentencepiece
