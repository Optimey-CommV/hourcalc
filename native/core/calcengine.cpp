// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Port of oracle\CalcEngine.cs: the hours calculator state machine (tab 2). The member names
// follow the reference (_acc -> acc_, _op -> pendingOp_, ...).

#include "hc_internal.h"

namespace hc {

const wchar_t* CalcEngine::DivideByZeroText = L"Cannot divide by zero";
const wchar_t* CalcEngine::OverflowText = L"Overflow";

namespace {

std::wstring Canon(const std::wstring& key)
{
    if (key == L".") return L",";
    if (key == L"u" || key == L"U" || key == L"h" || key == L"H") return L":";
    if (key == L"Back" || key == L"BACKSPACE") return L"BACK";
    if (key == L"ce") return L"CE";
    if (key == L"c") return L"C";
    return key;
}

bool OutOfRange(const Dec& minutes)
{
    return minutes.Abs() >= timemath::LimitMinutes();
}

std::wstring OpText(wchar_t o)
{
    return std::wstring(1, o);
}

// Durations show as HH:mm, factors (second operand of * and /) as decimals.
std::wstring OperandText(wchar_t o, const Dec& minutes)
{
    return (o == L'*' || o == L'/') ? timemath::FormatFactor(minutes) : timemath::FormatHm(minutes);
}

} // namespace

CalcEngine::CalcEngine()
    : places_(timemath::DefaultDecimals), entryActive_(false), hasAcc_(false), pendingOp_(0), lastOp_(0),
      hasLast_(false), justEq_(false), error_(nullptr)
{
}

CalcEngine::CalcEngine(int decimalPlaces)
    : places_(timemath::ClampDecimals(decimalPlaces)), entryActive_(false), hasAcc_(false), pendingOp_(0),
      lastOp_(0), hasLast_(false), justEq_(false), error_(nullptr)
{
}

int CalcEngine::DecimalPlaces() const
{
    return places_;
}

void CalcEngine::SetDecimalPlaces(int places)
{
    places_ = timemath::ClampDecimals(places);
}

// ------------------------------------------------------------ display

std::wstring CalcEngine::Expression() const
{
    std::wstring s;
    for (size_t i = 0; i < expr_.size(); ++i) {
        if (i != 0) s += L' ';
        s += expr_[i];
    }
    return s;
}

std::wstring CalcEngine::MainText() const
{
    if (error_ != nullptr) return std::wstring(error_);
    if (entryActive_) return DisplayEntry();
    return timemath::FormatHm(hasAcc_ ? acc_ : Dec());
}

std::wstring CalcEngine::DecimalText() const
{
    return error_ != nullptr ? std::wstring() : timemath::FormatDecimal(CurrentMinutes(), places_);
}

std::wstring CalcEngine::Caption() const
{
    if (error_ != nullptr) return std::wstring();
    if (!entryActive_ && !hasAcc_) return std::wstring();
    return timemath::FormatWords(CurrentMinutes());
}

bool CalcEngine::IsError() const
{
    return error_ != nullptr;
}

bool CalcEngine::HasEntry() const
{
    return entryActive_;
}

bool CalcEngine::IsEnabled(const std::wstring& key) const
{
    if (error_ == nullptr) return true;
    std::wstring k = Canon(key);
    return (k.size() == 1 && timemath::IsDigit(k[0])) || k == L"C" || k == L"CE";
}

std::wstring CalcEngine::CopyMainText() const
{
    return error_ != nullptr ? std::wstring() : MainText();
}

std::wstring CalcEngine::CopyDecimalText() const
{
    return DecimalText();
}

Dec CalcEngine::CurrentMinutes() const
{
    if (entryActive_) {
        Dec v;
        return timemath::TryParseEntry(entry_, v) ? v : Dec();
    }
    return hasAcc_ ? acc_ : Dec();
}

std::wstring CalcEngine::DisplayEntry() const
{
    if (entry_.empty()) return L"0";
    std::wstring sep = timemath::DecimalSeparator();
    std::wstring r;
    for (size_t i = 0; i < entry_.size(); ++i) {
        if (entry_[i] == L',') r += sep;
        else r += entry_[i];
    }
    return r;
}

// ------------------------------------------------------------ input

void CalcEngine::Press(const std::wstring& rawKey)
{
    std::wstring key = Canon(rawKey);
    if (key == L"CE") { ClearEntry(); return; }
    if (key == L"C") { ResetAll(); return; }
    if (key == L"BACK") { Backspace(); return; }
    if (key.size() == 1) {
        wchar_t c = key[0];
        if (timemath::IsDigit(c)) { Digit(c); return; }
        if (c == L':' || c == L',') { Separator(c); return; }
        if (c == L'+' || c == L'-' || c == L'*' || c == L'/') { Operator(c); return; }
        if (c == L'=') { Equal(); return; }
    }
}

bool CalcEngine::Paste(const std::wstring& text)
{
    std::wstring t = detail::Trim(text);
    if (t.empty()) return false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'u' || t[i] == L'U' || t[i] == L'h' || t[i] == L'H') t[i] = L':';
    }
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'.') t[i] = L',';
    }
    Dec v;
    if (!timemath::TryParseEntry(t, v)) return false;
    if (error_ != nullptr) ResetAll();
    else if (justEq_) StartNew();
    // A typed entry that starts with a separator shows a leading zero; so does a pasted one.
    entry_ = (t[0] == L':' || t[0] == L',') ? L"0" + t : t;
    entryActive_ = true;
    return true;
}

void CalcEngine::Digit(wchar_t d)
{
    if (error_ != nullptr) ResetAll();
    else if (justEq_) StartNew();
    BeginEntry();
    size_t sep = entry_.find_first_of(L":,");
    if (sep == std::wstring::npos) {
        if (static_cast<int>(entry_.size()) >= timemath::MaxHourDigits) return;
    } else {
        int after = static_cast<int>(entry_.size() - sep - 1);
        int max = entry_[sep] == L':' ? timemath::MaxMinuteDigits : timemath::MaxDecimals;
        if (after >= max) return;
    }
    entry_ += d;
}

void CalcEngine::Separator(wchar_t s)
{
    if (error_ != nullptr) return;
    if (justEq_) StartNew();
    BeginEntry();
    if (entry_.find_first_of(L":,") != std::wstring::npos) return;
    entry_ = (entry_.empty() ? std::wstring(L"0") : entry_) + s;
}

void CalcEngine::BeginEntry()
{
    if (entryActive_) return;
    entryActive_ = true;
    entry_.clear();
}

void CalcEngine::Backspace()
{
    if (error_ != nullptr || !entryActive_ || justEq_) return;
    if (!entry_.empty()) entry_.erase(entry_.size() - 1);
}

void CalcEngine::ClearEntry()
{
    if (error_ != nullptr || justEq_) {
        ResetAll();
        return;
    }
    if (!entryActive_ && !hasAcc_) return;
    entryActive_ = true;
    entry_.clear();
}

void CalcEngine::ResetAll()
{
    StartNew();
    error_ = nullptr;
}

// A new calculation: forget accumulator, pending operator and repeat state.
void CalcEngine::StartNew()
{
    entry_.clear();
    entryActive_ = false;
    acc_ = Dec();
    hasAcc_ = false;
    pendingOp_ = 0;
    expr_.clear();
    justEq_ = false;
    hasLast_ = false;
    lastOp_ = 0;
    lastOperand_ = Dec();
}

void CalcEngine::Operator(wchar_t o)
{
    if (error_ != nullptr) return;
    if (entryActive_) {
        Dec v = EntryMinutes();
        if (!hasAcc_ || pendingOp_ == 0) {
            expr_.clear();
            expr_.push_back(timemath::FormatHm(v));
            if (OutOfRange(v)) {
                // A typed or pasted entry (9999999:99) can already be 10 000 000 h or more.
                error_ = OverflowText;
                expr_.push_back(OpText(o));
                pendingOp_ = 0;
                ClearEntryState();
                return;
            }
            acc_ = v;
            hasAcc_ = true;
        } else {
            std::wstring text = OperandText(pendingOp_, v);
            bool ok = Apply(pendingOp_, v);
            expr_.push_back(text);
            if (!ok) {
                expr_.push_back(OpText(o));
                pendingOp_ = 0;
                ClearEntryState();
                return;
            }
        }
        pendingOp_ = o;
        expr_.push_back(OpText(o));
        ClearEntryState();
        hasLast_ = false;
    } else if (justEq_) {
        // Continue with the result.
        expr_.clear();
        expr_.push_back(timemath::FormatHm(acc_));
        pendingOp_ = o;
        expr_.push_back(OpText(o));
        justEq_ = false;
        hasLast_ = false;
    } else if (pendingOp_ != 0) {
        // No new entry: replace the pending operator.
        pendingOp_ = o;
        expr_[expr_.size() - 1] = OpText(o);
    } else {
        // Nothing yet: start from zero, like Windows Calculator.
        acc_ = Dec();
        hasAcc_ = true;
        expr_.clear();
        expr_.push_back(timemath::FormatHm(Dec()));
        pendingOp_ = o;
        expr_.push_back(OpText(o));
    }
}

void CalcEngine::Equal()
{
    if (error_ != nullptr) return;
    if (entryActive_) {
        Dec v = EntryMinutes();
        if (hasAcc_ && pendingOp_ != 0) {
            Finish(pendingOp_, v);
        } else {
            // "0:90 =" just normalizes the entry; it starts a fresh calculation.
            expr_.clear();
            expr_.push_back(timemath::FormatHm(v));
            expr_.push_back(L"=");
            hasLast_ = false;
            pendingOp_ = 0;
            ClearEntryState();
            if (OutOfRange(v)) {
                error_ = OverflowText;
                return;
            }
            acc_ = v;
            hasAcc_ = true;
            justEq_ = true;
        }
    } else if (hasAcc_ && pendingOp_ != 0) {
        // "7:30 + =" uses the accumulator as the second operand.
        Dec operand = acc_;
        Finish(pendingOp_, operand);
    } else if (justEq_ && hasLast_) {
        // "=" again repeats the last operation.
        std::wstring first = timemath::FormatHm(acc_);
        wchar_t o = lastOp_;
        Dec v = lastOperand_;
        expr_.clear();
        expr_.push_back(first);
        expr_.push_back(OpText(o));
        expr_.push_back(OperandText(o, v));
        expr_.push_back(L"=");
        if (!Apply(o, v)) return;
    }
}

// Completes the chain: acc = acc <op> v, "... =" in the expression line.
void CalcEngine::Finish(wchar_t o, const Dec& v)
{
    std::wstring text = OperandText(o, v);
    bool ok = Apply(o, v);
    expr_.push_back(text);
    expr_.push_back(L"=");
    pendingOp_ = 0;
    ClearEntryState();
    if (!ok) return;
    lastOp_ = o;
    lastOperand_ = v;
    hasLast_ = true;
    justEq_ = true;
}

void CalcEngine::ClearEntryState()
{
    entry_.clear();
    entryActive_ = false;
}

Dec CalcEngine::EntryMinutes() const
{
    Dec v;
    return timemath::TryParseEntry(entry_, v) ? v : Dec();
}

// acc = acc <op> operand. Returns false and enters the error state when the operation is
// undefined or the result is out of range.
bool CalcEngine::Apply(wchar_t o, const Dec& operandMinutes)
{
    try {
        Dec r;
        switch (o) {
        case L'+': r = acc_ + operandMinutes; break;
        case L'-': r = acc_ - operandMinutes; break;
        case L'*': r = acc_ * operandMinutes / Dec(60); break;
        default:
            if (operandMinutes == Dec()) {
                error_ = DivideByZeroText;
                return false;
            }
            r = acc_ * Dec(60) / operandMinutes;
            break;
        }
        if (OutOfRange(r)) {
            error_ = OverflowText;
            return false;
        }
        acc_ = r;
        hasAcc_ = true;
        return true;
    } catch (const DecOverflow&) {
        error_ = OverflowText;
        return false;
    }
}

} // namespace hc
