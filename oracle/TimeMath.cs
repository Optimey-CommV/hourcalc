// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - pure time arithmetic and formatting. No WPF references.
//
// Every duration is a System.Decimal number of minutes, so nothing is ever
// subject to binary floating point error. Display rounding (minutes to the
// nearest minute, decimal hours to 2..6 places, midpoint away from zero) happens
// only in the Format* helpers; callers keep the full precision value.

using System;
using System.Globalization;

namespace HourCalc
{
    public static class TimeMath
    {
        /// <summary>Maximum digits before the separator in an entry or field.</summary>
        public const int MaxHourDigits = 7;

        /// <summary>Maximum digits after a ':' separator (minutes).</summary>
        public const int MaxMinuteDigits = 2;

        /// <summary>Maximum digits after a decimal separator.</summary>
        public const int MaxDecimals = 6;

        /// <summary>Decimal places of the decimal hours output by default.</summary>
        public const int DefaultDecimals = 2;

        /// <summary>Fewest decimal places the Advanced menu offers.</summary>
        public const int MinDisplayDecimals = 2;

        /// <summary>Most decimal places the Advanced menu offers.</summary>
        public const int MaxDisplayDecimals = 6;

        /// <summary>|result| must stay below this many minutes (10 000 000 hours).</summary>
        public const decimal LimitMinutes = 10000000m * 60m;

        // Decimal division is inexact (1/6 = 0.1666...), so a value that is mathematically an exact
        // half (2.5 minutes, 0.125 hour) can sit a hair below the midpoint. Rounding to this many
        // places first (far below anything a display shows) puts such a value back on the midpoint.
        const int MinutesGuardPlaces = 9;
        const int HoursGuardPlaces = 12;

        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        /// <summary>The decimal separator of the current culture (read on every call).</summary>
        public static string DecimalSeparator
        {
            get { return CultureInfo.CurrentCulture.NumberFormat.NumberDecimalSeparator; }
        }

        public static bool IsDigit(char c)
        {
            return c >= '0' && c <= '9';
        }

        public static bool AllDigits(string s)
        {
            for (int i = 0; i < s.Length; i++)
                if (!IsDigit(s[i])) return false;
            return true;
        }

        /// <summary>Keeps a decimal places setting inside 2..6 (anything else gives the default).</summary>
        public static int ClampDecimals(int places)
        {
            return places < MinDisplayDecimals || places > MaxDisplayDecimals ? DefaultDecimals : places;
        }

        /// <summary>Rounds a minute count to the nearest minute, midpoint away from zero.</summary>
        public static decimal RoundMinutes(decimal minutes)
        {
            decimal guarded = Math.Round(minutes, MinutesGuardPlaces, MidpointRounding.AwayFromZero);
            return Math.Round(guarded, 0, MidpointRounding.AwayFromZero);
        }

        /// <summary>Rounds decimal hours to 2 places, midpoint away from zero.</summary>
        public static decimal RoundHours(decimal hours)
        {
            return RoundHours(hours, DefaultDecimals);
        }

        /// <summary>Rounds decimal hours to the given number of places (2..6), midpoint away from zero.</summary>
        public static decimal RoundHours(decimal hours, int places)
        {
            decimal guarded = Math.Round(hours, HoursGuardPlaces, MidpointRounding.AwayFromZero);
            return Math.Round(guarded, ClampDecimals(places), MidpointRounding.AwayFromZero);
        }

        /// <summary>"HH:mm", at least two hour digits, minutes rounded; "-" prefix when negative.</summary>
        public static string FormatHm(decimal minutes)
        {
            decimal r = RoundMinutes(minutes);
            long total = (long)Math.Abs(r);
            string s = (total / 60).ToString("00", Inv) + ":" + (total % 60).ToString("00", Inv);
            return r < 0 ? "-" + s : s;
        }

        /// <summary>Decimal hours with exactly 2 decimals in the current culture, no grouping.</summary>
        public static string FormatDecimal(decimal minutes)
        {
            return FormatDecimal(minutes, DefaultDecimals);
        }

        /// <summary>Decimal hours with exactly <paramref name="places"/> decimals (2..6), current culture, no grouping.</summary>
        public static string FormatDecimal(decimal minutes, int places)
        {
            int p = ClampDecimals(places);
            decimal h = RoundHours(minutes / 60m, p);
            string s = Math.Abs(h).ToString("0." + new string('0', p), CultureInfo.CurrentCulture);
            return h < 0 ? "-" + s : s;
        }

        /// <summary>The value in words: "8 h 30 min", "-1 h 30 min".</summary>
        public static string FormatWords(decimal minutes)
        {
            decimal r = RoundMinutes(minutes);
            long total = (long)Math.Abs(r);
            string s = (total / 60).ToString(Inv) + " h " + (total % 60).ToString(Inv) + " min";
            return r < 0 ? "-" + s : s;
        }

        /// <summary>
        /// A value (given as minutes) shown as plain decimal hours without padding:
        /// "2", "1,5". Used for the factor operand of * and /.
        /// </summary>
        public static string FormatFactor(decimal minutes)
        {
            decimal h = Math.Round(minutes / 60m, MaxDecimals, MidpointRounding.AwayFromZero);
            if (h == 0m) return "0";
            string s = Math.Abs(h).ToString("0.######", CultureInfo.CurrentCulture);
            return h < 0 ? "-" + s : s;
        }

        /// <summary>
        /// Parses calculator entry text into minutes. The internal entry format uses
        /// ':' for h:mm and ',' for decimal hours (the decimal point '.' is accepted as
        /// ','). "" is a valid zero entry. A trailing separator is allowed ("7:" = 7 h).
        /// Minutes of 60 or more carry ("0:90" = 1 h 30 min).
        /// </summary>
        public static bool TryParseEntry(string entry, out decimal minutes)
        {
            minutes = 0m;
            if (entry == null) return false;
            int sep = entry.IndexOfAny(new[] { ':', ',', '.' });
            string a = sep < 0 ? entry : entry.Substring(0, sep);
            string b = sep < 0 ? "" : entry.Substring(sep + 1);
            if (!AllDigits(a) || !AllDigits(b)) return false;
            if (a.Length > MaxHourDigits) return false;
            if (sep >= 0 && entry[sep] == ':')
            {
                if (b.Length > MaxMinuteDigits) return false;
                decimal h = a.Length == 0 ? 0m : decimal.Parse(a, Inv);
                decimal m = b.Length == 0 ? 0m : decimal.Parse(b, Inv);
                minutes = h * 60m + m;
                return true;
            }
            if (b.Length > MaxDecimals) return false;
            if (a.Length == 0 && b.Length == 0)
            {
                minutes = 0m;
                return true;
            }
            decimal hours = decimal.Parse((a.Length == 0 ? "0" : a) + "." + (b.Length == 0 ? "0" : b), Inv);
            minutes = hours * 60m;
            return true;
        }
    }
}
