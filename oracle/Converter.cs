// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - pure converter field logic (tab 1). No WPF references.
//
// Field 0 (Left)  holds hours:minutes ("08:30").
// Field 1 (Right) holds decimal hours ("8,50", separator of the current culture).
// The window feeds raw text through Filter, then asks for the value / the other
// field's text. All functions are stateless.

using System;
using System.Globalization;
using System.Text;

namespace HourCalc
{
    public static class Converter
    {
        public const int Left = 0;
        public const int Right = 1;

        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        // A formatted value can carry one hour digit more than the 7 that can be typed:
        // 9999999:99 is 10000000:39 and 9999999,999999 rounds to 10000000,00. That is the
        // only 8 digit hour count there is, so text that was formatted here is accepted back.
        const string ShownMaxHoursPrefix = "1000000";

        /// <summary>
        /// Sanitizes raw field text typed by the user. Left: digits and one separator
        /// (':', '.', 'u', 'h' all shown as ':'), at most 2 digits after it, ',' ignored.
        /// Right: digits and one decimal separator ('.' or ',' shown as the culture's
        /// separator), at most 6 decimals, ':' ignored. At most 7 digits before the
        /// separator in both.
        /// </summary>
        public static string Filter(int field, string text)
        {
            return FilterCore(field, text, false);
        }

        /// <summary>
        /// Sanitizes text that is already in a field: typed text (at most 7 hour digits) or text this
        /// class formatted (which can carry the one 8 digit hour count, 10000000). Otherwise the same
        /// as <see cref="Filter"/>. The window sanitizes edits and the Convert button with this.
        /// </summary>
        public static string FilterField(int field, string text)
        {
            return FilterCore(field, text, true);
        }

        // shown = true: the text is already in a field, so it may be a formatted value that
        // carries the 8 digit hour count 10000000 (see ShownMaxHoursPrefix).
        static string FilterCore(int field, string text, bool shown)
        {
            if (string.IsNullOrEmpty(text)) return "";
            string dec = TimeMath.DecimalSeparator;
            var sb = new StringBuilder(text.Length);
            bool sep = false;
            int before = 0, after = 0;
            int maxAfter = field == Left ? TimeMath.MaxMinuteDigits : TimeMath.MaxDecimals;
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (TimeMath.IsDigit(c))
                {
                    if (!sep)
                    {
                        if (before < TimeMath.MaxHourDigits)
                        {
                            sb.Append(c);
                            before++;
                        }
                        else if (shown && before == TimeMath.MaxHourDigits && c == '0' &&
                                 string.CompareOrdinal(sb.ToString(), ShownMaxHoursPrefix) == 0)
                        {
                            sb.Append(c);
                            before++;
                        }
                    }
                    else if (after < maxAfter)
                    {
                        sb.Append(c);
                        after++;
                    }
                }
                else if (!sep)
                {
                    if (field == Left && (c == ':' || c == '.' || c == 'u' || c == 'U' || c == 'h' || c == 'H'))
                    {
                        sb.Append(':');
                        sep = true;
                    }
                    else if (field == Right && (c == ',' || c == '.' || dec.IndexOf(c) >= 0))
                    {
                        sb.Append(dec);
                        sep = true;
                    }
                }
            }
            return sb.ToString();
        }

        /// <summary>
        /// The value of already filtered field text in minutes (unrounded). False when
        /// the text holds no digit.
        /// </summary>
        public static bool TryGetMinutes(int field, string text, out decimal minutes)
        {
            minutes = 0m;
            if (string.IsNullOrEmpty(text)) return false;
            if (field == Left)
            {
                int sep = text.IndexOf(':');
                if (sep < 0)
                {
                    // Digits only: 1-2 digits are whole hours, 3 or more end in minutes.
                    if (text.Length <= 2)
                    {
                        minutes = decimal.Parse(text, Inv) * 60m;
                    }
                    else
                    {
                        string hh = text.Substring(0, text.Length - 2);
                        string mm = text.Substring(text.Length - 2);
                        minutes = decimal.Parse(hh, Inv) * 60m + decimal.Parse(mm, Inv);
                    }
                    return true;
                }
                string a = text.Substring(0, sep);
                string b = text.Substring(sep + 1);
                if (a.Length == 0 && b.Length == 0) return false;
                if (!TimeMath.AllDigits(a) || !TimeMath.AllDigits(b)) return false;
                decimal h = a.Length == 0 ? 0m : decimal.Parse(a, Inv);
                decimal m = b.Length == 0 ? 0m : decimal.Parse(b, Inv);
                minutes = h * 60m + m;
                return true;
            }
            else
            {
                string dec = TimeMath.DecimalSeparator;
                int sep = text.IndexOf(dec, StringComparison.Ordinal);
                string a = sep < 0 ? text : text.Substring(0, sep);
                string b = sep < 0 ? "" : text.Substring(sep + dec.Length);
                if (a.Length == 0 && b.Length == 0) return false;
                if (!TimeMath.AllDigits(a) || !TimeMath.AllDigits(b)) return false;
                decimal hours = decimal.Parse((a.Length == 0 ? "0" : a) + "." + (b.Length == 0 ? "0" : b), Inv);
                minutes = hours * 60m;
                return true;
            }
        }

        /// <summary>Formats a minute count as the text of the given field (2 decimals on the right).</summary>
        public static string Format(int field, decimal minutes)
        {
            return Format(field, minutes, TimeMath.DefaultDecimals);
        }

        /// <summary>Formats a minute count as the text of the given field; the right field shows <paramref name="places"/> decimals.</summary>
        public static string Format(int field, decimal minutes, int places)
        {
            return field == Left ? TimeMath.FormatHm(minutes) : TimeMath.FormatDecimal(minutes, places);
        }

        /// <summary>The canonical text of a field: "830" -> "08:30", "8,5" -> "8,50". "" when empty.</summary>
        public static string Normalize(int field, string text)
        {
            return Normalize(field, text, TimeMath.DefaultDecimals);
        }

        /// <summary>
        /// The canonical text of a field (see above) with <paramref name="places"/> decimals on
        /// the right. The text may be typed text or text this class formatted, so normalizing
        /// is idempotent.
        /// </summary>
        public static string Normalize(int field, string text, int places)
        {
            decimal m;
            return TryGetMinutes(field, FilterCore(field, text, true), out m) ? Format(field, m, places) : "";
        }

        /// <summary>
        /// Computes the other field's text and the caption (the value in words) from the
        /// source field's text (2 decimals). Both are "" when the source holds no value.
        /// </summary>
        public static void Compute(int sourceField, string sourceText, out string otherText, out string caption)
        {
            Compute(sourceField, sourceText, TimeMath.DefaultDecimals, out otherText, out caption);
        }

        /// <summary>As above, with <paramref name="places"/> decimals in the right field.</summary>
        public static void Compute(int sourceField, string sourceText, int places, out string otherText, out string caption)
        {
            decimal m;
            if (!TryGetMinutes(sourceField, Filter(sourceField, sourceText), out m))
            {
                otherText = "";
                caption = "";
                return;
            }
            otherText = Format(1 - sourceField, m, places);
            caption = TimeMath.FormatWords(m);
        }

        /// <summary>
        /// What the Convert button does with the text in the active field: its canonical text,
        /// plus the other field and the caption computed from what is in the field. Normalizing
        /// the text (the right field is rounded to the decimal places) must never move the other
        /// field, so everything is derived from the unrounded value: 8,058 stays 08:03 although
        /// the field now reads 8,06. All three are "" when the field holds no value.
        /// </summary>
        public static void Convert(int field, string text, int places, out string normalized, out string otherText, out string caption)
        {
            decimal m;
            if (!TryGetMinutes(field, FilterCore(field, text, true), out m))
            {
                normalized = "";
                otherText = "";
                caption = "";
                return;
            }
            normalized = Format(field, m, places);
            otherText = Format(1 - field, m, places);
            caption = TimeMath.FormatWords(m);
        }
    }
}
