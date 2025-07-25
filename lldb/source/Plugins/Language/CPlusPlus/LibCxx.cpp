//===-- LibCxx.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/VectorIterator.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObject.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "Plugins/Language/CPlusPlus/CxxStringTypes.h"
#include "Plugins/LanguageRuntime/CPlusPlus/CPPLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include <optional>
#include <tuple>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static void consumeInlineNamespace(llvm::StringRef &name) {
  // Delete past an inline namespace, if any: __[a-zA-Z0-9_]+::
  auto scratch = name;
  if (scratch.consume_front("__") && std::isalnum(scratch[0])) {
    scratch = scratch.drop_while([](char c) { return std::isalnum(c); });
    if (scratch.consume_front("::")) {
      // Successfully consumed a namespace.
      name = scratch;
    }
  }
}

bool lldb_private::formatters::isOldCompressedPairLayout(
    ValueObject &pair_obj) {
  return isStdTemplate(pair_obj.GetTypeName(), "__compressed_pair");
}

bool lldb_private::formatters::isStdTemplate(ConstString type_name,
                                             llvm::StringRef type) {
  llvm::StringRef name = type_name.GetStringRef();
  // The type name may be prefixed with `std::__<inline-namespace>::`.
  if (name.consume_front("std::"))
    consumeInlineNamespace(name);
  return name.consume_front(type) && name.starts_with("<");
}

lldb::ValueObjectSP lldb_private::formatters::GetChildMemberWithName(
    ValueObject &obj, llvm::ArrayRef<ConstString> alternative_names) {
  for (ConstString name : alternative_names) {
    lldb::ValueObjectSP child_sp = obj.GetChildMemberWithName(name);

    if (child_sp)
      return child_sp;
  }
  return {};
}

lldb::ValueObjectSP
lldb_private::formatters::GetFirstValueOfLibCXXCompressedPair(
    ValueObject &pair) {
  ValueObjectSP value;
  ValueObjectSP first_child = pair.GetChildAtIndex(0);
  if (first_child)
    value = first_child->GetChildMemberWithName("__value_");
  if (!value) {
    // pre-c88580c member name
    value = pair.GetChildMemberWithName("__first_");
  }
  return value;
}

lldb::ValueObjectSP
lldb_private::formatters::GetSecondValueOfLibCXXCompressedPair(
    ValueObject &pair) {
  ValueObjectSP value;
  if (pair.GetNumChildrenIgnoringErrors() > 1) {
    ValueObjectSP second_child = pair.GetChildAtIndex(1);
    if (second_child) {
      value = second_child->GetChildMemberWithName("__value_");
    }
  }
  if (!value) {
    // pre-c88580c member name
    value = pair.GetChildMemberWithName("__second_");
  }
  return value;
}

bool lldb_private::formatters::LibcxxFunctionSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {

  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());

  if (!valobj_sp)
    return false;

  ExecutionContext exe_ctx(valobj_sp->GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();

  if (process == nullptr)
    return false;

  CPPLanguageRuntime *cpp_runtime = CPPLanguageRuntime::Get(*process);

  if (!cpp_runtime)
    return false;

  CPPLanguageRuntime::LibCppStdFunctionCallableInfo callable_info =
      cpp_runtime->FindLibCppStdFunctionCallableInfo(valobj_sp);

  switch (callable_info.callable_case) {
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::Invalid:
    stream.Printf(" __f_ = %" PRIu64, callable_info.member_f_pointer_value);
    return false;
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::Lambda:
    stream.Printf(
        " Lambda in File %s at Line %u",
        callable_info.callable_line_entry.GetFile().GetFilename().GetCString(),
        callable_info.callable_line_entry.line);
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::CallableObject:
    stream.Printf(
        " Function in File %s at Line %u",
        callable_info.callable_line_entry.GetFile().GetFilename().GetCString(),
        callable_info.callable_line_entry.line);
    break;
  case CPPLanguageRuntime::LibCppStdFunctionCallableCase::FreeOrMemberFunction:
    stream.Printf(" Function = %s ",
                  callable_info.callable_symbol.GetName().GetCString());
    break;
  }

  return true;
}

bool lldb_private::formatters::LibcxxSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP ptr_sp(valobj_sp->GetChildMemberWithName("__ptr_"));
  ValueObjectSP ctrl_sp(valobj_sp->GetChildMemberWithName("__cntrl_"));
  if (!ctrl_sp || !ptr_sp)
    return false;

  DumpCxxSmartPtrPointerSummary(stream, *ptr_sp, options);

  bool success;
  uint64_t ctrl_addr = ctrl_sp->GetValueAsUnsigned(0, &success);
  // Empty control field. We're done.
  if (!success || ctrl_addr == 0)
    return true;

  if (auto count_sp = ctrl_sp->GetChildMemberWithName("__shared_owners_")) {
    bool success;
    uint64_t count = count_sp->GetValueAsUnsigned(0, &success);
    if (!success)
      return false;

    // std::shared_ptr releases the underlying resource when the
    // __shared_owners_ count hits -1. So `__shared_owners_ == 0` indicates 1
    // owner. Hence add +1 here.
    stream.Printf(" strong=%" PRIu64, count + 1);
  }

  if (auto weak_count_sp =
          ctrl_sp->GetChildMemberWithName("__shared_weak_owners_")) {
    bool success;
    uint64_t count = weak_count_sp->GetValueAsUnsigned(0, &success);
    if (!success)
      return false;

    // Unlike __shared_owners_, __shared_weak_owners_ indicates the exact
    // std::weak_ptr reference count.
    stream.Printf(" weak=%" PRIu64, count);
  }

  return true;
}

bool lldb_private::formatters::LibcxxUniquePointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP ptr_sp(valobj_sp->GetChildMemberWithName("__ptr_"));
  if (!ptr_sp)
    return false;

  if (isOldCompressedPairLayout(*ptr_sp))
    ptr_sp = GetFirstValueOfLibCXXCompressedPair(*ptr_sp);

  if (!ptr_sp)
    return false;

  DumpCxxSmartPtrPointerSummary(stream, *ptr_sp, options);

  return true;
}

/*
 (lldb) fr var ibeg --raw --ptr-depth 1 -T
 (std::__1::__wrap_iter<int *>) ibeg = {
 (std::__1::__wrap_iter<int *>::iterator_type) __i = 0x00000001001037a0 {
 (int) *__i = 1
 }
 }
*/

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibCxxVectorIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new VectorIteratorSyntheticFrontEnd(
                          valobj_sp, {ConstString("__i_"), ConstString("__i")})
                    : nullptr);
}

lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    LibcxxSharedPtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_cntrl(nullptr),
      m_ptr_obj(nullptr) {
  if (valobj_sp)
    Update();
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxSharedPtrSyntheticFrontEnd::CalculateNumChildren() {
  return (m_cntrl ? 1 : 0);
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_cntrl || !m_ptr_obj)
    return lldb::ValueObjectSP();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ValueObjectSP();

  if (idx == 0)
    return m_ptr_obj->GetSP();

  if (idx == 1) {
    Status status;
    auto value_type_sp = valobj_sp->GetCompilerType()
                             .GetTypeTemplateArgument(0)
                             .GetPointerType();
    ValueObjectSP cast_ptr_sp = m_ptr_obj->Cast(value_type_sp);
    ValueObjectSP value_sp = cast_ptr_sp->Dereference(status);
    if (status.Success())
      return value_sp;
  }

  return lldb::ValueObjectSP();
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::Update() {
  m_cntrl = nullptr;
  m_ptr_obj = nullptr;

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  TargetSP target_sp(valobj_sp->GetTargetSP());
  if (!target_sp)
    return lldb::ChildCacheState::eRefetch;

  auto ptr_obj_sp = valobj_sp->GetChildMemberWithName("__ptr_");
  if (!ptr_obj_sp)
    return lldb::ChildCacheState::eRefetch;

  m_ptr_obj = ptr_obj_sp->Clone(ConstString("pointer")).get();

  lldb::ValueObjectSP cntrl_sp(valobj_sp->GetChildMemberWithName("__cntrl_"));

  m_cntrl = cntrl_sp.get(); // need to store the raw pointer to avoid a circular
                            // dependency
  return lldb::ChildCacheState::eRefetch;
}

llvm::Expected<size_t>
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (name == "__ptr_" || name == "pointer")
    return 0;

  if (name == "object" || name == "$$dereference$$")
    return 1;

  return llvm::createStringError("Type has no child named '%s'",
                                 name.AsCString());
}

lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEnd::
    ~LibcxxSharedPtrSyntheticFrontEnd() = default;

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxSharedPtrSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEnd::
    LibcxxUniquePtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEnd::
    ~LibcxxUniquePtrSyntheticFrontEnd() = default;

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxUniquePtrSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxUniquePtrSyntheticFrontEnd::CalculateNumChildren() {
  if (m_value_ptr_sp)
    return m_deleter_sp ? 2 : 1;
  return 0;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_value_ptr_sp)
    return lldb::ValueObjectSP();

  if (idx == 0)
    return m_value_ptr_sp;

  if (idx == 1)
    return m_deleter_sp;

  if (idx == 2) {
    Status status;
    auto value_sp = m_value_ptr_sp->Dereference(status);
    if (status.Success()) {
      return value_sp;
    }
  }

  return lldb::ValueObjectSP();
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEnd::Update() {
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP ptr_sp(valobj_sp->GetChildMemberWithName("__ptr_"));
  if (!ptr_sp)
    return lldb::ChildCacheState::eRefetch;

  // Retrieve the actual pointer and the deleter, and clone them to give them
  // user-friendly names.
  if (isOldCompressedPairLayout(*ptr_sp)) {
    if (ValueObjectSP value_pointer_sp =
            GetFirstValueOfLibCXXCompressedPair(*ptr_sp))
      m_value_ptr_sp = value_pointer_sp->Clone(ConstString("pointer"));

    if (ValueObjectSP deleter_sp =
            GetSecondValueOfLibCXXCompressedPair(*ptr_sp))
      m_deleter_sp = deleter_sp->Clone(ConstString("deleter"));
  } else {
    m_value_ptr_sp = ptr_sp->Clone(ConstString("pointer"));

    if (ValueObjectSP deleter_sp =
            valobj_sp->GetChildMemberWithName("__deleter_"))
      if (deleter_sp->GetNumChildrenIgnoringErrors() > 0)
        m_deleter_sp = deleter_sp->Clone(ConstString("deleter"));
  }

  return lldb::ChildCacheState::eRefetch;
}

llvm::Expected<size_t>
lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (name == "pointer")
    return 0;
  if (name == "deleter")
    return 1;
  if (name == "obj" || name == "object" || name == "$$dereference$$")
    return 2;
  return llvm::createStringError("Type has no child named '%s'",
                                 name.AsCString());
}

/// The field layout in a libc++ string (cap, side, data or data, size, cap).
namespace {
enum class StringLayout { CSD, DSC };
}

static ValueObjectSP ExtractLibCxxStringData(ValueObject &valobj) {
  if (auto rep_sp = valobj.GetChildMemberWithName("__rep_"))
    return rep_sp;

  ValueObjectSP valobj_r_sp = valobj.GetChildMemberWithName("__r_");
  if (!valobj_r_sp || !valobj_r_sp->GetError().Success())
    return nullptr;

  if (!isOldCompressedPairLayout(*valobj_r_sp))
    return nullptr;

  return GetFirstValueOfLibCXXCompressedPair(*valobj_r_sp);
}

/// Determine the size in bytes of \p valobj (a libc++ std::string object) and
/// extract its data payload. Return the size + payload pair.
// TODO: Support big-endian architectures.
static std::optional<std::pair<uint64_t, ValueObjectSP>>
ExtractLibcxxStringInfo(ValueObject &valobj) {
  ValueObjectSP valobj_rep_sp = ExtractLibCxxStringData(valobj);
  if (!valobj_rep_sp || !valobj_rep_sp->GetError().Success())
    return {};

  ValueObjectSP l = valobj_rep_sp->GetChildMemberWithName("__l");
  if (!l)
    return {};

  auto index_or_err = l->GetIndexOfChildWithName("__data_");
  if (!index_or_err) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::DataFormatters), index_or_err.takeError(),
                   "{0}");
    return {};
  }

  StringLayout layout =
      *index_or_err == 0 ? StringLayout::DSC : StringLayout::CSD;

  bool short_mode = false; // this means the string is in short-mode and the
                           // data is stored inline
  bool using_bitmasks = true; // Whether the class uses bitmasks for the mode
                              // flag (pre-D123580).
  uint64_t size;
  uint64_t size_mode_value = 0;

  ValueObjectSP short_sp = valobj_rep_sp->GetChildMemberWithName("__s");
  if (!short_sp)
    return {};

  ValueObjectSP is_long = short_sp->GetChildMemberWithName("__is_long_");
  ValueObjectSP size_sp = short_sp->GetChildMemberWithName("__size_");
  if (!size_sp)
    return {};

  if (is_long) {
    using_bitmasks = false;
    short_mode = !is_long->GetValueAsUnsigned(/*fail_value=*/0);
    size = size_sp->GetValueAsUnsigned(/*fail_value=*/0);
  } else {
    // The string mode is encoded in the size field.
    size_mode_value = size_sp->GetValueAsUnsigned(0);
    uint8_t mode_mask = layout == StringLayout::DSC ? 0x80 : 1;
    short_mode = (size_mode_value & mode_mask) == 0;
  }

  if (short_mode) {
    ValueObjectSP location_sp = short_sp->GetChildMemberWithName("__data_");
    if (using_bitmasks)
      size = (layout == StringLayout::DSC) ? size_mode_value
                                           : ((size_mode_value >> 1) % 256);

    if (!location_sp)
      return {};

    // When the small-string optimization takes place, the data must fit in the
    // inline string buffer (23 bytes on x86_64/Darwin). If it doesn't, it's
    // likely that the string isn't initialized and we're reading garbage.
    ExecutionContext exe_ctx(location_sp->GetExecutionContextRef());
    const std::optional<uint64_t> max_bytes =
        llvm::expectedToOptional(location_sp->GetCompilerType().GetByteSize(
            exe_ctx.GetBestExecutionContextScope()));
    if (!max_bytes || size > *max_bytes)
      return {};

    return std::make_pair(size, location_sp);
  }

  // we can use the layout_decider object as the data pointer
  ValueObjectSP location_sp = l->GetChildMemberWithName("__data_");
  ValueObjectSP size_vo = l->GetChildMemberWithName("__size_");
  ValueObjectSP capacity_vo = l->GetChildMemberWithName("__cap_");
  if (!size_vo || !location_sp || !capacity_vo)
    return {};
  size = size_vo->GetValueAsUnsigned(LLDB_INVALID_OFFSET);
  uint64_t capacity = capacity_vo->GetValueAsUnsigned(LLDB_INVALID_OFFSET);
  if (!using_bitmasks && layout == StringLayout::CSD)
    capacity *= 2;
  if (size == LLDB_INVALID_OFFSET || capacity == LLDB_INVALID_OFFSET ||
      capacity < size)
    return {};
  return std::make_pair(size, location_sp);
}

bool lldb_private::formatters::LibcxxWStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  auto string_info = ExtractLibcxxStringInfo(valobj);
  if (!string_info)
    return false;
  uint64_t size;
  ValueObjectSP location_sp;
  std::tie(size, location_sp) = *string_info;

  auto wchar_t_size = GetWCharByteSize(valobj);
  if (!wchar_t_size)
    return false;

  switch (*wchar_t_size) {
  case 1:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF8>(
        stream, summary_options, location_sp, size, "L");
  case 2:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF16>(
        stream, summary_options, location_sp, size, "L");
  case 4:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF32>(
        stream, summary_options, location_sp, size, "L");
  }
  return false;
}

template <StringPrinter::StringElementType element_type>
static bool
LibcxxStringSummaryProvider(ValueObject &valobj, Stream &stream,
                            const TypeSummaryOptions &summary_options,
                            std::string prefix_token) {
  auto string_info = ExtractLibcxxStringInfo(valobj);
  if (!string_info)
    return false;
  uint64_t size;
  ValueObjectSP location_sp;
  std::tie(size, location_sp) = *string_info;

  return StringBufferSummaryProvider<element_type>(
      stream, summary_options, location_sp, size, prefix_token);
}
template <StringPrinter::StringElementType element_type>
static bool formatStringImpl(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &summary_options,
                             std::string prefix_token) {
  StreamString scratch_stream;
  const bool success = LibcxxStringSummaryProvider<element_type>(
      valobj, scratch_stream, summary_options, prefix_token);
  if (success)
    stream << scratch_stream.GetData();
  else
    stream << "Summary Unavailable";
  return true;
}

bool lldb_private::formatters::LibcxxStringSummaryProviderASCII(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringImpl<StringPrinter::StringElementType::ASCII>(
      valobj, stream, summary_options, "");
}

bool lldb_private::formatters::LibcxxStringSummaryProviderUTF16(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringImpl<StringPrinter::StringElementType::UTF16>(
      valobj, stream, summary_options, "u");
}

bool lldb_private::formatters::LibcxxStringSummaryProviderUTF32(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringImpl<StringPrinter::StringElementType::UTF32>(
      valobj, stream, summary_options, "U");
}

static std::tuple<bool, ValueObjectSP, size_t>
LibcxxExtractStringViewData(ValueObject& valobj) {
  auto dataobj = GetChildMemberWithName(
      valobj, {ConstString("__data_"), ConstString("__data")});
  auto sizeobj = GetChildMemberWithName(
      valobj, {ConstString("__size_"), ConstString("__size")});
  if (!dataobj || !sizeobj)
    return std::make_tuple<bool,ValueObjectSP,size_t>(false, {}, {});

  if (!dataobj->GetError().Success() || !sizeobj->GetError().Success())
    return std::make_tuple<bool,ValueObjectSP,size_t>(false, {}, {});

  bool success{false};
  uint64_t size = sizeobj->GetValueAsUnsigned(0, &success);
  if (!success)
    return std::make_tuple<bool,ValueObjectSP,size_t>(false, {}, {});

  return std::make_tuple(true,dataobj,size);
}

template <StringPrinter::StringElementType element_type>
static bool formatStringViewImpl(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &summary_options,
                                 std::string prefix_token) {

  bool success;
  ValueObjectSP dataobj;
  size_t size;
  std::tie(success, dataobj, size) = LibcxxExtractStringViewData(valobj);

  if (!success) {
    stream << "Summary Unavailable";
    return true;
  }

  return StringBufferSummaryProvider<element_type>(stream, summary_options,
                                                   dataobj, size, prefix_token);
}

bool lldb_private::formatters::LibcxxStringViewSummaryProviderASCII(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringViewImpl<StringPrinter::StringElementType::ASCII>(
      valobj, stream, summary_options, "");
}

bool lldb_private::formatters::LibcxxStringViewSummaryProviderUTF16(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringViewImpl<StringPrinter::StringElementType::UTF16>(
      valobj, stream, summary_options, "u");
}

bool lldb_private::formatters::LibcxxStringViewSummaryProviderUTF32(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  return formatStringViewImpl<StringPrinter::StringElementType::UTF32>(
      valobj, stream, summary_options, "U");
}

bool lldb_private::formatters::LibcxxWStringViewSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {

  bool success;
  ValueObjectSP dataobj;
  size_t size;
  std::tie(success, dataobj, size) = LibcxxExtractStringViewData(valobj);

  if (!success) {
    stream << "Summary Unavailable";
    return true;
  }

  auto wchar_t_size = GetWCharByteSize(valobj);
  if (!wchar_t_size)
    return false;

  switch (*wchar_t_size) {
  case 1:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF8>(
        stream, summary_options, dataobj, size, "L");
  case 2:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF16>(
        stream, summary_options, dataobj, size, "L");
  case 4:
    return StringBufferSummaryProvider<StringPrinter::StringElementType::UTF32>(
        stream, summary_options, dataobj, size, "L");
  }
  return false;
}

static bool
LibcxxChronoTimePointSecondsSummaryProvider(ValueObject &valobj, Stream &stream,
                                            const TypeSummaryOptions &options,
                                            const char *fmt) {
  ValueObjectSP ptr_sp = valobj.GetChildMemberWithName("__d_");
  if (!ptr_sp)
    return false;
  ptr_sp = ptr_sp->GetChildMemberWithName("__rep_");
  if (!ptr_sp)
    return false;

#ifndef _WIN32
  // The date time in the chrono library is valid in the range
  // [-32767-01-01T00:00:00Z, 32767-12-31T23:59:59Z]. A 64-bit time_t has a
  // larger range, the function strftime is not able to format the entire range
  // of time_t. The exact point has not been investigated; it's limited to
  // chrono's range.
  const std::time_t chrono_timestamp_min =
      -1'096'193'779'200; // -32767-01-01T00:00:00Z
  const std::time_t chrono_timestamp_max =
      971'890'963'199; // 32767-12-31T23:59:59Z
#else
  const std::time_t chrono_timestamp_min = -43'200; // 1969-12-31T12:00:00Z
  const std::time_t chrono_timestamp_max =
      32'536'850'399; // 3001-01-19T21:59:59
#endif

  const std::time_t seconds = ptr_sp->GetValueAsSigned(0);
  if (seconds < chrono_timestamp_min || seconds > chrono_timestamp_max)
    stream.Printf("timestamp=%" PRId64 " s", static_cast<int64_t>(seconds));
  else {
    std::array<char, 128> str;
    std::size_t size =
        std::strftime(str.data(), str.size(), fmt, gmtime(&seconds));
    if (size == 0)
      return false;

    stream.Printf("date/time=%s timestamp=%" PRId64 " s", str.data(),
                  static_cast<int64_t>(seconds));
  }

  return true;
}

bool lldb_private::formatters::LibcxxChronoSysSecondsSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  return LibcxxChronoTimePointSecondsSummaryProvider(valobj, stream, options,
                                                     "%FT%H:%M:%SZ");
}

bool lldb_private::formatters::LibcxxChronoLocalSecondsSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  return LibcxxChronoTimePointSecondsSummaryProvider(valobj, stream, options,
                                                     "%FT%H:%M:%S");
}

static bool
LibcxxChronoTimepointDaysSummaryProvider(ValueObject &valobj, Stream &stream,
                                         const TypeSummaryOptions &options,
                                         const char *fmt) {
  ValueObjectSP ptr_sp = valobj.GetChildMemberWithName("__d_");
  if (!ptr_sp)
    return false;
  ptr_sp = ptr_sp->GetChildMemberWithName("__rep_");
  if (!ptr_sp)
    return false;

#ifndef _WIN32
  // The date time in the chrono library is valid in the range
  // [-32767-01-01Z, 32767-12-31Z]. A 32-bit time_t has a larger range, the
  // function strftime is not able to format the entire range of time_t. The
  // exact point has not been investigated; it's limited to chrono's range.
  const int chrono_timestamp_min = -12'687'428; // -32767-01-01Z
  const int chrono_timestamp_max = 11'248'737;  // 32767-12-31Z
#else
  const int chrono_timestamp_min = 0;       // 1970-01-01Z
  const int chrono_timestamp_max = 376'583; // 3001-01-19Z
#endif

  const int days = ptr_sp->GetValueAsSigned(0);
  if (days < chrono_timestamp_min || days > chrono_timestamp_max)
    stream.Printf("timestamp=%d days", days);

  else {
    const std::time_t seconds = std::time_t(86400) * days;

    std::array<char, 128> str;
    std::size_t size =
        std::strftime(str.data(), str.size(), fmt, gmtime(&seconds));
    if (size == 0)
      return false;

    stream.Printf("date=%s timestamp=%d days", str.data(), days);
  }

  return true;
}

bool lldb_private::formatters::LibcxxChronoSysDaysSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  return LibcxxChronoTimepointDaysSummaryProvider(valobj, stream, options,
                                                  "%FZ");
}

bool lldb_private::formatters::LibcxxChronoLocalDaysSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  return LibcxxChronoTimepointDaysSummaryProvider(valobj, stream, options,
                                                  "%F");
}

bool lldb_private::formatters::LibcxxChronoMonthSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  // FIXME: These are the names used in the C++20 ostream operator. Since LLVM
  // uses C++17 it's not possible to use the ostream operator directly.
  static const std::array<std::string_view, 12> months = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December"};

  ValueObjectSP ptr_sp = valobj.GetChildMemberWithName("__m_");
  if (!ptr_sp)
    return false;

  const unsigned month = ptr_sp->GetValueAsUnsigned(0);
  if (month >= 1 && month <= 12)
    stream << "month=" << months[month - 1];
  else
    stream.Printf("month=%u", month);

  return true;
}

bool lldb_private::formatters::LibcxxChronoWeekdaySummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  // FIXME: These are the names used in the C++20 ostream operator. Since LLVM
  // uses C++17 it's not possible to use the ostream operator directly.
  static const std::array<std::string_view, 7> weekdays = {
      "Sunday",   "Monday", "Tuesday", "Wednesday",
      "Thursday", "Friday", "Saturday"};

  ValueObjectSP ptr_sp = valobj.GetChildMemberWithName("__wd_");
  if (!ptr_sp)
    return false;

  const unsigned weekday = ptr_sp->GetValueAsUnsigned(0);
  if (weekday < 7)
    stream << "weekday=" << weekdays[weekday];
  else
    stream.Printf("weekday=%u", weekday);

  return true;
}

bool lldb_private::formatters::LibcxxChronoYearMonthDaySummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP ptr_sp = valobj.GetChildMemberWithName("__y_");
  if (!ptr_sp)
    return false;
  ptr_sp = ptr_sp->GetChildMemberWithName("__y_");
  if (!ptr_sp)
    return false;
  int year = ptr_sp->GetValueAsSigned(0);

  ptr_sp = valobj.GetChildMemberWithName("__m_");
  if (!ptr_sp)
    return false;
  ptr_sp = ptr_sp->GetChildMemberWithName("__m_");
  if (!ptr_sp)
    return false;
  const unsigned month = ptr_sp->GetValueAsUnsigned(0);

  ptr_sp = valobj.GetChildMemberWithName("__d_");
  if (!ptr_sp)
    return false;
  ptr_sp = ptr_sp->GetChildMemberWithName("__d_");
  if (!ptr_sp)
    return false;
  const unsigned day = ptr_sp->GetValueAsUnsigned(0);

  stream << "date=";
  if (year < 0) {
    stream << '-';
    year = -year;
  }
  stream.Printf("%04d-%02u-%02u", year, month, day);

  return true;
}
