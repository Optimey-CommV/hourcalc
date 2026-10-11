// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the hours calculator state machine (tab 2). No WPF references.
//
// Drive it with Press(key); read the display state from the properties.
// Keys: "0".."9", ":", ",", "+", "-", "*", "/", "=", "CE", "C", "BACK".
// ("." is an alias of ",", "u" and "h" are aliases of ":".)
//
// Internally every value is a decimal number of minutes. Entries are durations:
// "7:30" (h:mm), "7,5" (decimal hours) or "8" (whole hours). For * and / the
// second operand is a plain factor: its value in decimal hours.
// Execution is immediate and left to right, like Windows Calculator Standard.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace HourCalc
{
    public sealed class CalcEngine
    {
        public const string DivideByZeroText = "Cannot divide by zero";
        public const string OverflowText = "Overflow";

        // The entry being typed. Internal separators are ':' and ','.
        string _entry = "";
        bool _entryActive;      // an entry is being typed ("" counts as zero)

        decimal _acc;           // accumulator, minutes, full precision
        bool _hasAcc;
        char _op;               // pending operator, '\0' when none
        readonly List<string> _expr = new List<string>();
        bool _justEq;           // the last action was "=": the result is shown

        char _lastOp;           // for repeated "="
        decimal _lastOperand;   // minutes
        bool _hasLast;

        string _error;          // null, or the text that replaces the display

        int _decimals = TimeMath.DefaultDecimals;

        public CalcEngine()
        {
        }

        /// <summary>A calculator whose decimal hours output has <paramref name="decimalPlaces"/> places (2..6).</summary>
        public CalcEngine(int decimalPlaces)
        {
            DecimalPlaces = decimalPlaces;
        }

        /// <summary>
        /// Decimal places of the decimal hours output (2..6, default 2). Only the display
        /// follows it: the accumulator keeps its full precision, so changing it re-formats
        /// the values shown from the exact value.
        /// </summary>
        public int DecimalPlaces
        {
            get { return _decimals; }
            set { _decimals = TimeMath.ClampDecimals(value); }
        }

        // ------------------------------------------------------------ display

        /// <summary>The expression line, e.g. "07:30 + 07:20 =" (ASCII operators).</summary>
        public string Expression { get { return string.Join(" ", _expr); } }

        /// <summary>Left display: the h:mm result, the raw entry, or the error text.</summary>
        public string MainText
        {
            get
            {
                if (_error != null) return _error;
                if (_entryActive) return DisplayEntry();
                return TimeMath.FormatHm(_hasAcc ? _acc : 0m);
            }
        }

        /// <summary>Right display: the decimal hours of what the left shows ("" on error).</summary>
        public string DecimalText
        {
            get { return _error != null ? "" : TimeMath.FormatDecimal(CurrentMinutes(), _decimals); }
        }

        /// <summary>The value in words under the left field ("" when idle or on error).</summary>
        public string Caption
        {
            get
            {
                if (_error != null) return "";
                if (!_entryActive && !_hasAcc) return "";
                return TimeMath.FormatWords(CurrentMinutes());
            }
        }

        public bool IsError { get { return _error != null; } }

        /// <summary>True while an entry (possibly empty = zero) is being typed.</summary>
        public bool HasEntry { get { return _entryActive; } }

        /// <summary>False for keys that are disabled in the current state (error state).</summary>
        public bool IsEnabled(string key)
        {
            if (_error == null) return true;
            key = Canon(key);
            return (key.Length == 1 && TimeMath.IsDigit(key[0])) || key == "C" || key == "CE";
        }

        /// <summary>The h:mm text to copy, or "" on error.</summary>
        public string CopyMainText { get { return _error != null ? "" : MainText; } }

        /// <summary>The decimal text to copy, or "" on error.</summary>
        public string CopyDecimalText { get { return DecimalText; } }

        decimal CurrentMinutes()
        {
            if (_entryActive)
            {
                decimal v;
                return TimeMath.TryParseEntry(_entry, out v) ? v : 0m;
            }
            return _hasAcc ? _acc : 0m;
        }

        string DisplayEntry()
        {
            if (_entry.Length == 0) return "0";
            return _entry.Replace(",", TimeMath.DecimalSeparator);
        }

        // ------------------------------------------------------------ input

        static string Canon(string key)
        {
            if (key == null) return "";
            switch (key)
            {
                case ".": return ",";
                case "u":
                case "U":
                case "h":
                case "H": return ":";
                case "Back":
                case "BACKSPACE": return "BACK";
                case "ce": return "CE";
                case "c": return "C";
                default: return key;
            }
        }

        public void Press(string key)
        {
            key = Canon(key);
            switch (key)
            {
                case "CE": ClearEntry(); return;
                case "C": ResetAll(); return;
                case "BACK": Backspace(); return;
            }
            if (key.Length == 1)
            {
                char c = key[0];
                if (TimeMath.IsDigit(c)) { Digit(c); return; }
                if (c == ':' || c == ',') { Separator(c); return; }
                if (c == '+' || c == '-' || c == '*' || c == '/') { Operator(c); return; }
                if (c == '=') { Equal(); return; }
            }
        }

        /// <summary>
        /// Pastes text as the current entry when it parses (digits with at most one of
        /// ':' ',' '.'). Returns false and changes nothing otherwise.
        /// </summary>
        public bool Paste(string text)
        {
            if (text == null) return false;
            string t = text.Trim();
            if (t.Length == 0) return false;
            for (int i = 0; i < t.Length; i++)
            {
                if (t[i] == 'u' || t[i] == 'U' || t[i] == 'h' || t[i] == 'H')
                    t = t.Substring(0, i) + ":" + t.Substring(i + 1);
            }
            t = t.Replace('.', ',');
            decimal v;
            if (!TimeMath.TryParseEntry(t, out v)) return false;
            if (_error != null) ResetAll();
            else if (_justEq) StartNew();
            // A typed entry that starts with a separator shows a leading zero; so does a pasted one.
            _entry = t[0] == ':' || t[0] == ',' ? "0" + t : t;
            _entryActive = true;
            return true;
        }

        void Digit(char d)
        {
            if (_error != null) ResetAll();
            else if (_justEq) StartNew();
            BeginEntry();
            int sep = _entry.IndexOfAny(new[] { ':', ',' });
            if (sep < 0)
            {
                if (_entry.Length >= TimeMath.MaxHourDigits) return;
            }
            else
            {
                int after = _entry.Length - sep - 1;
                int max = _entry[sep] == ':' ? TimeMath.MaxMinuteDigits : TimeMath.MaxDecimals;
                if (after >= max) return;
            }
            _entry += d;
        }

        void Separator(char s)
        {
            if (_error != null) return;
            if (_justEq) StartNew();
            BeginEntry();
            if (_entry.IndexOfAny(new[] { ':', ',' }) >= 0) return;
            _entry = (_entry.Length == 0 ? "0" : _entry) + s;
        }

        void BeginEntry()
        {
            if (_entryActive) return;
            _entryActive = true;
            _entry = "";
        }

        void Backspace()
        {
            if (_error != null || !_entryActive || _justEq) return;
            if (_entry.Length > 0) _entry = _entry.Substring(0, _entry.Length - 1);
        }

        void ClearEntry()
        {
            if (_error != null || _justEq)
            {
                ResetAll();
                return;
            }
            if (!_entryActive && !_hasAcc) return;
            _entryActive = true;
            _entry = "";
        }

        void ResetAll()
        {
            StartNew();
            _error = null;
        }

        // A new calculation: forget accumulator, pending operator and repeat state.
        void StartNew()
        {
            _entry = "";
            _entryActive = false;
            _acc = 0m;
            _hasAcc = false;
            _op = '\0';
            _expr.Clear();
            _justEq = false;
            _hasLast = false;
            _lastOp = '\0';
            _lastOperand = 0m;
        }

        void Operator(char o)
        {
            if (_error != null) return;
            if (_entryActive)
            {
                decimal v = EntryMinutes();
                if (!_hasAcc || _op == '\0')
                {
                    _expr.Clear();
                    _expr.Add(TimeMath.FormatHm(v));
                    if (OutOfRange(v))
                    {
                        // A typed or pasted entry (9999999:99) can already be 10 000 000 h or more.
                        _error = OverflowText;
                        _expr.Add(OpText(o));
                        _op = '\0';
                        ClearEntryState();
                        return;
                    }
                    _acc = v;
                    _hasAcc = true;
                }
                else
                {
                    string text = OperandText(_op, v);
                    bool ok = Apply(_op, v);
                    _expr.Add(text);
                    if (!ok) { _expr.Add(OpText(o)); _op = '\0'; ClearEntryState(); return; }
                }
                _op = o;
                _expr.Add(OpText(o));
                ClearEntryState();
                _hasLast = false;
            }
            else if (_justEq)
            {
                // Continue with the result.
                _expr.Clear();
                _expr.Add(TimeMath.FormatHm(_acc));
                _op = o;
                _expr.Add(OpText(o));
                _justEq = false;
                _hasLast = false;
            }
            else if (_op != '\0')
            {
                // No new entry: replace the pending operator.
                _op = o;
                _expr[_expr.Count - 1] = OpText(o);
            }
            else
            {
                // Nothing yet: start from zero, like Windows Calculator.
                _acc = 0m;
                _hasAcc = true;
                _expr.Clear();
                _expr.Add(TimeMath.FormatHm(0m));
                _op = o;
                _expr.Add(OpText(o));
            }
        }

        void Equal()
        {
            if (_error != null) return;
            if (_entryActive)
            {
                decimal v = EntryMinutes();
                if (_hasAcc && _op != '\0')
                {
                    Finish(_op, v);
                }
                else
                {
                    // "0:90 =" just normalizes the entry; it starts a fresh calculation.
                    _expr.Clear();
                    _expr.Add(TimeMath.FormatHm(v));
                    _expr.Add("=");
                    _hasLast = false;
                    _op = '\0';
                    ClearEntryState();
                    if (OutOfRange(v))
                    {
                        _error = OverflowText;
                        return;
                    }
                    _acc = v;
                    _hasAcc = true;
                    _justEq = true;
                }
            }
            else if (_hasAcc && _op != '\0')
            {
                // "7:30 + =" uses the accumulator as the second operand.
                Finish(_op, _acc);
            }
            else if (_justEq && _hasLast)
            {
                // "=" again repeats the last operation.
                string first = TimeMath.FormatHm(_acc);
                char o = _lastOp;
                decimal v = _lastOperand;
                _expr.Clear();
                _expr.Add(first);
                _expr.Add(OpText(o));
                _expr.Add(OperandText(o, v));
                _expr.Add("=");
                if (!Apply(o, v)) return;
            }
        }

        // Completes the chain: acc = acc <op> v, "... =" in the expression line.
        void Finish(char o, decimal v)
        {
            string text = OperandText(o, v);
            bool ok = Apply(o, v);
            _expr.Add(text);
            _expr.Add("=");
            _op = '\0';
            ClearEntryState();
            if (!ok) return;
            _lastOp = o;
            _lastOperand = v;
            _hasLast = true;
            _justEq = true;
        }

        void ClearEntryState()
        {
            _entry = "";
            _entryActive = false;
        }

        decimal EntryMinutes()
        {
            decimal v;
            return TimeMath.TryParseEntry(_entry, out v) ? v : 0m;
        }

        // acc = acc <op> operand. Returns false and enters the error state when the
        // operation is undefined or the result is out of range.
        bool Apply(char o, decimal operandMinutes)
        {
            try
            {
                decimal r;
                switch (o)
                {
                    case '+': r = checked(_acc + operandMinutes); break;
                    case '-': r = checked(_acc - operandMinutes); break;
                    case '*': r = checked(_acc * operandMinutes / 60m); break;
                    default:
                        if (operandMinutes == 0m)
                        {
                            _error = DivideByZeroText;
                            return false;
                        }
                        r = checked(_acc * 60m / operandMinutes);
                        break;
                }
                if (OutOfRange(r))
                {
                    _error = OverflowText;
                    return false;
                }
                _acc = r;
                _hasAcc = true;
                return true;
            }
            catch (OverflowException)
            {
                _error = OverflowText;
                return false;
            }
        }

        static bool OutOfRange(decimal minutes)
        {
            return Math.Abs(minutes) >= TimeMath.LimitMinutes;
        }

        static string OpText(char o)
        {
            return o.ToString();
        }

        // Durations show as HH:mm, factors (second operand of * and /) as decimals.
        static string OperandText(char o, decimal minutes)
        {
            return (o == '*' || o == '/') ? TimeMath.FormatFactor(minutes) : TimeMath.FormatHm(minutes);
        }
    }
}
