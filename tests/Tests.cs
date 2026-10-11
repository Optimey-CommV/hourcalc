// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc unit tests: a console runner, no test framework.
// Compiled together with src\TimeMath.cs, src\Converter.cs, src\CalcEngine.cs and src\Settings.cs.
// Every table runs under nl-BE and en-US; expected strings are written with ','
// as decimal separator and converted to the culture's separator when compared.
// Exit code 0 only when every check passes.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using HourCalc;

static class Tests
{
    static int _checks;
    static int _failures;
    static string _culture = "";

    static int Main()
    {
        foreach (string name in new[] { "nl-BE", "en-US" })
        {
            _culture = name;
            CultureInfo ci = CultureInfo.GetCultureInfo(name);
            CultureInfo.DefaultThreadCurrentCulture = ci;
            Thread.CurrentThread.CurrentCulture = ci;
            if (TimeMath.DecimalSeparator != (name == "nl-BE" ? "," : "."))
            {
                Fail("culture", "decimal separator of " + name + " is unexpected: " + TimeMath.DecimalSeparator);
            }
            FormatTests();
            ConverterTableTests();
            ConverterFilterTests();
            ConverterRoundTripTests();
            CalcRequiredTests();
            CalcExpressionTests();
            CalcEntryTests();
            CalcStateTests();
            CalcErrorTests();
            CalcPasteTests();
            CalcPrecisionTests();
            CalcLimitTests();
            ConvertButtonTests();
            DecimalPlacesTests();
            SettingsTests();
        }
        Console.WriteLine();
        Console.WriteLine("HourCalc tests: " + _checks + " checks, " + _failures + " failed.");
        return _failures == 0 ? 0 : 1;
    }

    // ------------------------------------------------------------ helpers

    static void Fail(string what, string message)
    {
        _failures++;
        Console.WriteLine("FAIL [" + _culture + "] " + what + ": " + message);
    }

    static void Eq(string what, string expected, string actual)
    {
        _checks++;
        if (expected != actual)
            Fail(what, "expected <" + expected + "> but got <" + actual + ">");
    }

    static void True(string what, bool value)
    {
        _checks++;
        if (!value) Fail(what, "expected true");
    }

    static void False(string what, bool value)
    {
        _checks++;
        if (value) Fail(what, "expected false");
    }

    // Expected strings use ',' as decimal separator; the culture may use '.'.
    static string S(string s)
    {
        return s.Replace(",", TimeMath.DecimalSeparator);
    }

    // A calculator with the given decimal places, after pressing the keys.
    static CalcEngine KeysAt(int places, string keys)
    {
        var e = new CalcEngine(places);
        Press(e, keys);
        return e;
    }

    static string Dec(decimal m)
    {
        return m.ToString("0.######", CultureInfo.InvariantCulture);
    }

    // Presses a key string: characters one by one, spaces ignored,
    // <CE> <C> <BK> for the named keys.
    static CalcEngine Keys(string keys)
    {
        var e = new CalcEngine();
        Press(e, keys);
        return e;
    }

    static void Press(CalcEngine e, string keys)
    {
        for (int i = 0; i < keys.Length; i++)
        {
            char c = keys[i];
            if (c == ' ') continue;
            if (c == '<')
            {
                int end = keys.IndexOf('>', i);
                string name = keys.Substring(i + 1, end - i - 1);
                e.Press(name == "BK" ? "BACK" : name);
                i = end;
                continue;
            }
            e.Press(c.ToString());
        }
    }

    // ------------------------------------------------------------ formatting

    static void FormatTests()
    {
        Eq("FormatHm 510", "08:30", TimeMath.FormatHm(510m));
        Eq("FormatHm 0", "00:00", TimeMath.FormatHm(0m));
        Eq("FormatHm 7425 (123:45)", "123:45", TimeMath.FormatHm(7425m));
        Eq("FormatHm 0.5 rounds up", "00:01", TimeMath.FormatHm(0.5m));
        Eq("FormatHm 0.49 rounds down", "00:00", TimeMath.FormatHm(0.49m));
        Eq("FormatHm 89.5 carries", "01:30", TimeMath.FormatHm(89.5m));
        Eq("FormatHm 59.5 carries to the hour", "01:00", TimeMath.FormatHm(59.5m));
        Eq("FormatHm -90", "-01:30", TimeMath.FormatHm(-90m));
        Eq("FormatHm -0.5 away from zero", "-00:01", TimeMath.FormatHm(-0.5m));
        Eq("FormatHm -0.4 shows no sign", "00:00", TimeMath.FormatHm(-0.4m));
        Eq("FormatHm 9999999:59", "9999999:59", TimeMath.FormatHm(9999999m * 60m + 59m));

        Eq("FormatDecimal 510", S("8,50"), TimeMath.FormatDecimal(510m));
        Eq("FormatDecimal 500", S("8,33"), TimeMath.FormatDecimal(500m));
        Eq("FormatDecimal 0", S("0,00"), TimeMath.FormatDecimal(0m));
        Eq("FormatDecimal 112.5 midpoint away", S("1,88"), TimeMath.FormatDecimal(112.5m));
        Eq("FormatDecimal 0.3 min = 0.005 h rounds up", S("0,01"), TimeMath.FormatDecimal(0.3m));
        Eq("FormatDecimal 7.5 min = 0.125 h", S("0,13"), TimeMath.FormatDecimal(7.5m));
        Eq("FormatDecimal -90", S("-1,50"), TimeMath.FormatDecimal(-90m));
        Eq("FormatDecimal -0.1 shows no sign", S("0,00"), TimeMath.FormatDecimal(-0.1m));
        Eq("FormatDecimal no grouping", S("12345,00"), TimeMath.FormatDecimal(12345m * 60m));
        Eq("FormatDecimal 7425", S("123,75"), TimeMath.FormatDecimal(7425m));

        Eq("FormatWords 510", "8 h 30 min", TimeMath.FormatWords(510m));
        Eq("FormatWords 45", "0 h 45 min", TimeMath.FormatWords(45m));
        Eq("FormatWords 720", "12 h 0 min", TimeMath.FormatWords(720m));
        Eq("FormatWords -90", "-1 h 30 min", TimeMath.FormatWords(-90m));
        Eq("FormatWords 0", "0 h 0 min", TimeMath.FormatWords(0m));

        Eq("FormatFactor 120", "2", TimeMath.FormatFactor(120m));
        Eq("FormatFactor 90", S("1,5"), TimeMath.FormatFactor(90m));
        Eq("FormatFactor 0", "0", TimeMath.FormatFactor(0m));
        Eq("FormatFactor 80 (1:20)", S("1,333333"), TimeMath.FormatFactor(80m));

        decimal m;
        True("TryParseEntry 7:30", TimeMath.TryParseEntry("7:30", out m)); Eq("7:30 minutes", "450", Dec(m));
        True("TryParseEntry 7,5", TimeMath.TryParseEntry("7,5", out m)); Eq("7,5 minutes", "450", Dec(m));
        True("TryParseEntry 7.5", TimeMath.TryParseEntry("7.5", out m)); Eq("7.5 minutes", "450", Dec(m));
        True("TryParseEntry 8", TimeMath.TryParseEntry("8", out m)); Eq("8 minutes", "480", Dec(m));
        True("TryParseEntry 0:90", TimeMath.TryParseEntry("0:90", out m)); Eq("0:90 minutes", "90", Dec(m));
        True("TryParseEntry 7:3", TimeMath.TryParseEntry("7:3", out m)); Eq("7:3 minutes", "423", Dec(m));
        True("TryParseEntry 7:", TimeMath.TryParseEntry("7:", out m)); Eq("7: minutes", "420", Dec(m));
        True("TryParseEntry empty", TimeMath.TryParseEntry("", out m)); Eq("empty minutes", "0", Dec(m));
        True("TryParseEntry 0,1", TimeMath.TryParseEntry("0,1", out m)); Eq("0,1 minutes", "6", Dec(m));
        False("TryParseEntry letters", TimeMath.TryParseEntry("7x", out m));
        False("TryParseEntry too many minute digits", TimeMath.TryParseEntry("7:300", out m));
        False("TryParseEntry too many hour digits", TimeMath.TryParseEntry("12345678", out m));
        False("TryParseEntry two separators", TimeMath.TryParseEntry("1,2,3", out m));
    }

    // ------------------------------------------------------------ converter

    // field, input text, expected other-field text (',' as decimal separator), expected caption
    static readonly object[][] ConverterTable =
    {
        new object[] { Converter.Left,  "08:30",  "8,50",   "8 h 30 min" },
        new object[] { Converter.Right, "8,50",   "08:30",  "8 h 30 min" },
        new object[] { Converter.Right, "8,5",    "08:30",  "8 h 30 min" },
        new object[] { Converter.Left,  "08:20",  "8,33",   "8 h 20 min" },
        new object[] { Converter.Right, "8,33",   "08:20",  "8 h 20 min" },
        new object[] { Converter.Left,  "830",    "8,50",   "8 h 30 min" },
        new object[] { Converter.Left,  "8.30",   "8,50",   "8 h 30 min" },
        new object[] { Converter.Left,  "8u30",   "8,50",   "8 h 30 min" },
        new object[] { Converter.Left,  "8h30",   "8,50",   "8 h 30 min" },
        new object[] { Converter.Left,  "0:45",   "0,75",   "0 h 45 min" },
        new object[] { Converter.Right, "0,1",    "00:06",  "0 h 6 min" },
        new object[] { Converter.Left,  "123:45", "123,75", "123 h 45 min" },
        new object[] { Converter.Right, "8,51",   "08:31",  "8 h 31 min" },
        new object[] { Converter.Left,  "0:90",   "1,50",   "1 h 30 min" },
        new object[] { Converter.Left,  "8",      "8,00",   "8 h 0 min" },
        new object[] { Converter.Left,  "12",     "12,00",  "12 h 0 min" },
        new object[] { Converter.Left,  "1230",   "12,50",  "12 h 30 min" },
        new object[] { Converter.Left,  "8:3",    "8,05",   "8 h 3 min" },
        new object[] { Converter.Left,  "8:",     "8,00",   "8 h 0 min" },
        new object[] { Converter.Left,  ":30",    "0,50",   "0 h 30 min" },
        new object[] { Converter.Left,  "0",      "0,00",   "0 h 0 min" },
        new object[] { Converter.Left,  "7:59",   "7,98",   "7 h 59 min" },
        new object[] { Converter.Right, "0",      "00:00",  "0 h 0 min" },
        new object[] { Converter.Right, ",5",     "00:30",  "0 h 30 min" },
        new object[] { Converter.Right, "8,",     "08:00",  "8 h 0 min" },
        new object[] { Converter.Right, "8,333",  "08:20",  "8 h 20 min" },
        new object[] { Converter.Right, "8,999",  "09:00",  "9 h 0 min" },
        new object[] { Converter.Right, "123,75", "123:45", "123 h 45 min" },
        new object[] { Converter.Right, "8.5",    "08:30",  "8 h 30 min" },
        new object[] { Converter.Right, "0,01",   "00:01",  "0 h 1 min" },
        new object[] { Converter.Right, "0,008",  "00:00",  "0 h 0 min" },
    };

    static void ConverterTableTests()
    {
        foreach (object[] row in ConverterTable)
        {
            int field = (int)row[0];
            string input = S((string)row[1]);
            string expectOther = S((string)row[2]);
            string expectCaption = (string)row[3];
            string other, caption;
            Converter.Compute(field, input, out other, out caption);
            string tag = (field == Converter.Left ? "left " : "right ") + input;
            Eq("converter " + tag, expectOther, other);
            Eq("converter caption " + tag, expectCaption, caption);
        }

        // Empty or digit-less sources clear the other field and the caption.
        string o, c;
        foreach (string empty in new[] { "", ":", "," + "", "abc" })
        {
            Converter.Compute(Converter.Left, empty, out o, out c);
            Eq("left source '" + empty + "' clears other", "", o);
            Eq("left source '" + empty + "' clears caption", "", c);
            Converter.Compute(Converter.Right, empty, out o, out c);
            Eq("right source '" + empty + "' clears other", "", o);
            Eq("right source '" + empty + "' clears caption", "", c);
        }

        // Normalization (what Convert writes back into the active field).
        Eq("normalize 8:3", "08:03", Converter.Normalize(Converter.Left, "8:3"));
        Eq("normalize 830", "08:30", Converter.Normalize(Converter.Left, "830"));
        Eq("normalize 0:90", "01:30", Converter.Normalize(Converter.Left, "0:90"));
        Eq("normalize 8", "08:00", Converter.Normalize(Converter.Left, "8"));
        Eq("normalize 8u30", "08:30", Converter.Normalize(Converter.Left, "8u30"));
        Eq("normalize 8,5", S("8,50"), Converter.Normalize(Converter.Right, S("8,5")));
        Eq("normalize 8.5", S("8,50"), Converter.Normalize(Converter.Right, "8.5"));
        Eq("normalize 8,333", S("8,33"), Converter.Normalize(Converter.Right, S("8,333")));
        Eq("normalize ,5", S("0,50"), Converter.Normalize(Converter.Right, S(",5")));
        Eq("normalize empty left", "", Converter.Normalize(Converter.Left, ""));
        Eq("normalize empty right", "", Converter.Normalize(Converter.Right, ""));
    }

    static void ConverterFilterTests()
    {
        // Left field: digits plus one separator, shown as ':'.
        Eq("filter L 8:30", "8:30", Converter.Filter(Converter.Left, "8:30"));
        Eq("filter L 8.30", "8:30", Converter.Filter(Converter.Left, "8.30"));
        Eq("filter L 8u30", "8:30", Converter.Filter(Converter.Left, "8u30"));
        Eq("filter L 8U30", "8:30", Converter.Filter(Converter.Left, "8U30"));
        Eq("filter L 8h30", "8:30", Converter.Filter(Converter.Left, "8h30"));
        Eq("filter L comma ignored", "830", Converter.Filter(Converter.Left, "8,30"));
        Eq("filter L at most 2 minute digits", "8:30", Converter.Filter(Converter.Left, "8:3045"));
        Eq("filter L second separator ignored", "1:23", Converter.Filter(Converter.Left, "1:2:3"));
        Eq("filter L letters dropped", "12", Converter.Filter(Converter.Left, "a1b2c"));
        Eq("filter L minus dropped", "15", Converter.Filter(Converter.Left, "-15"));
        Eq("filter L 7 hour digits", "1234567", Converter.Filter(Converter.Left, "123456789"));
        Eq("filter L 7 hour digits with separator", "1234567:30", Converter.Filter(Converter.Left, "123456789:30"));
        Eq("filter L empty", "", Converter.Filter(Converter.Left, ""));
        Eq("filter L null", "", Converter.Filter(Converter.Left, null));
        Eq("filter L spaces dropped", "8:30", Converter.Filter(Converter.Left, " 8 : 30 "));

        // Right field: digits plus one decimal separator, shown as the culture's.
        Eq("filter R 8,5", S("8,5"), Converter.Filter(Converter.Right, "8,5"));
        Eq("filter R 8.5", S("8,5"), Converter.Filter(Converter.Right, "8.5"));
        Eq("filter R colon ignored", "830", Converter.Filter(Converter.Right, "8:30"));
        Eq("filter R u ignored", "8", Converter.Filter(Converter.Right, "8u"));
        Eq("filter R second separator ignored", S("1,23"), Converter.Filter(Converter.Right, "1,2,3"));
        Eq("filter R at most 6 decimals", S("0,123456"), Converter.Filter(Converter.Right, "0,1234567"));
        Eq("filter R 7 integer digits", "1234567", Converter.Filter(Converter.Right, "123456789"));
        Eq("filter R leading separator kept", S(",5"), Converter.Filter(Converter.Right, ",5"));
        Eq("filter R minus dropped", S("1,5"), Converter.Filter(Converter.Right, "-1,5"));
        Eq("filter R idempotent", S("8,50"), Converter.Filter(Converter.Right, S("8,50")));
        Eq("filter L idempotent", "08:30", Converter.Filter(Converter.Left, "08:30"));
    }

    static void ConverterRoundTripTests()
    {
        // Left -> right -> left is exact to the minute for 100 hours.
        int bad = 0;
        for (int minutes = 0; minutes <= 6000; minutes++)
        {
            string left = TimeMath.FormatHm(minutes);
            string right, cap;
            Converter.Compute(Converter.Left, left, out right, out cap);
            string back, cap2;
            Converter.Compute(Converter.Right, right, out back, out cap2);
            if (back != left) bad++;
        }
        Eq("left -> right -> left exact for 0..6000 minutes (bad count)", "0", bad.ToString(CultureInfo.InvariantCulture));

        // 2 decimals carry at most 0.3 minute (0.005 hour) of quantization error.
        int tooBig = 0;
        for (int minutes = 0; minutes <= 6000; minutes++)
        {
            string right = TimeMath.FormatDecimal(minutes);
            decimal back;
            Converter.TryGetMinutes(Converter.Right, right, out back);
            if (Math.Abs(back - minutes) > 0.3m) tooBig++;
        }
        Eq("decimal text is within 0.3 min of the minutes (violations)", "0", tooBig.ToString(CultureInfo.InvariantCulture));

        // Typing every hundredth from 0,00 to 100,00 gives a minute count within 0.5 of
        // the exact value (the rounding to whole minutes).
        int off = 0;
        for (int cents = 0; cents <= 10000; cents++)
        {
            string right = (cents / 100).ToString(CultureInfo.InvariantCulture) + TimeMath.DecimalSeparator +
                           (cents % 100).ToString("00", CultureInfo.InvariantCulture);
            decimal exact, shown;
            Converter.TryGetMinutes(Converter.Right, right, out exact);
            string other, cap;
            Converter.Compute(Converter.Right, right, out other, out cap);
            Converter.TryGetMinutes(Converter.Left, other, out shown);
            if (Math.Abs(shown - exact) > 0.5m) off++;
        }
        Eq("right -> left is the nearest minute (violations)", "0", off.ToString(CultureInfo.InvariantCulture));
    }

    // ------------------------------------------------------------ calculator

    static void Result(string keys, string left, string right)
    {
        CalcEngine e = Keys(keys);
        Eq("calc '" + keys + "' left", S(left), e.MainText);
        Eq("calc '" + keys + "' right", S(right), e.DecimalText);
    }

    static void CalcRequiredTests()
    {
        Result("7:30 + 7:20 =", "14:50", "14,83");
        Result("8 + 0:30 =", "08:30", "8,50");
        Result("7,5 + 7:20 =", "14:50", "14,83");
        Result("7:30 * 5 =", "37:30", "37,50");
        Result("7:30 / 4 =", "01:53", "1,88");
        Result("7:00 - 8:30 =", "-01:30", "-1,50");
        Result("7:30 + 1:00 = =", "09:30", "9,50");
        Result("0:90 =", "01:30", "1,50");
        Result("1:00 + 2:00 * 2 =", "06:00", "6,00");
        Result("7:30 / 0 =", "Cannot divide by zero", "");

        // More arithmetic.
        Result("7:30 + =", "15:00", "15,00");
        Result("7:30 / 4 = * 4 =", "07:30", "7,50");
        Result("7:30 * 1,5 =", "11:15", "11,25");
        Result("7:30 * 1:30 =", "11:15", "11,25");
        Result("7:30 / 1,5 =", "05:00", "5,00");
        Result("7:30 / 1:30 =", "05:00", "5,00");
        Result("7:30 * 2 = =", "30:00", "30,00");
        Result("7:30 - 1:00 = =", "05:30", "5,50");
        Result("7:00 - 8:30 = + 2:00 =", "00:30", "0,50");
        Result("5 + 3 = + 2 =", "10:00", "10,00");
        Result("7:30 + 1 = 5 =", "05:00", "5,00");
        Result("1:20 + 1:20 + 1:20 =", "04:00", "4,00");
        Result("0,1 + 0,1 =", "00:12", "0,20");
        Result("8:30 - 0,5 =", "08:00", "8,00");
        Result("12 * 0 =", "00:00", "0,00");
        Result("7u30 + 7h20 =", "14:50", "14,83");
        Result("7:30 + 7:20 + 8:00 =", "22:50", "22,83");
        Result("1:00 + 1:00 * 3 + 1:00 =", "07:00", "7,00");
        Result("99:59 + 0:01 =", "100:00", "100,00");
        Result("0:30 / 3 * 3 =", "00:30", "0,50");
        Result("1:00 / 3 * 3 =", "01:00", "1,00");
        Result("2:00 / 7 * 7 =", "02:00", "2,00");
        Result("9999999 + 0:59 =", "9999999:59", "9999999,98");
    }

    static void CalcExpressionTests()
    {
        Eq("expr after =", "07:30 + 07:20 =", Keys("7:30 + 7:20 =").Expression);
        Eq("expr chain", "07:30 + 07:20 + 08:00 +", Keys("7:30 + 7:20 + 8:00 +").Expression);
        Eq("expr after first operator", "07:30 +", Keys("7:30 +").Expression);
        Eq("expr factor", "07:30 * 2 =", Keys("7:30 * 2 =").Expression);
        Eq("expr factor decimal", S("07:30 / 1,5 ="), Keys("7:30 / 1,5 =").Expression);
        Eq("expr factor from h:mm", S("07:30 / 1,5 ="), Keys("7:30 / 1:30 =").Expression);
        Eq("expr entry as decimal hours shows HH:mm", "07:30 + 07:20 =", Keys("7,5 + 7:20 =").Expression);
        Eq("expr repeat", "08:30 + 01:00 =", Keys("7:30 + 1:00 = =").Expression);
        Eq("expr repeat twice", "09:30 + 01:00 =", Keys("7:30 + 1:00 = = =").Expression);
        Eq("expr repeat factor", "15:00 * 2 =", Keys("7:30 * 2 = =").Expression);
        Eq("expr operator replaced", "07:30 -", Keys("7:30 + -").Expression);
        Eq("expr operator replaced 3x", "07:30 /", Keys("7:30 + - /").Expression);
        Eq("expr continue after =", "14:50 +", Keys("7:30 + 7:20 = +").Expression);
        Eq("expr + with accumulator", "07:30 + 07:30 =", Keys("7:30 + =").Expression);
        Eq("expr 0:90 =", "01:30 =", Keys("0:90 =").Expression);
        Eq("expr before anything", "", new CalcEngine().Expression);
        Eq("expr of divide by zero", "07:30 / 0 =", Keys("7:30 / 0 =").Expression);
        Eq("expr operator first", "00:00 +", Keys("+").Expression);
        Eq("expr new entry after = clears", "", Keys("7:30 + 1 = 5").Expression);
        Eq("expr after C", "", Keys("7:30 + 1 <C>").Expression);
        Eq("expr negative accumulator", "-01:30 +", Keys("7:00 - 8:30 = +").Expression);

        // Running result shows after an operator.
        CalcEngine e = Keys("7:30 + 7:20 +");
        Eq("running result left", "14:50", e.MainText);
        Eq("running result right", S("14,83"), e.DecimalText);
        Eq("running result caption", "14 h 50 min", e.Caption);
        e = Keys("7:30 + 7:20 + 8:00 +");
        Eq("running result 3 operands", "22:50", e.MainText);
    }

    static void CalcEntryTests()
    {
        // Idle.
        CalcEngine e = new CalcEngine();
        Eq("idle left", "00:00", e.MainText);
        Eq("idle right", S("0,00"), e.DecimalText);
        Eq("idle caption", "", e.Caption);
        False("idle has no entry", e.HasEntry);
        False("idle no error", e.IsError);

        // Mid-entry: raw text as typed, live decimal value, caption in words.
        e = Keys("7:30 + 7:2");
        Eq("mid-entry left", "7:2", e.MainText);
        Eq("mid-entry right", S("7,03"), e.DecimalText);
        Eq("mid-entry caption", "7 h 2 min", e.Caption);
        Eq("mid-entry expression", "07:30 +", e.Expression);
        True("mid-entry has entry", e.HasEntry);

        e = Keys("7,5");
        Eq("decimal entry left", S("7,5"), e.MainText);
        Eq("decimal entry right", S("7,50"), e.DecimalText);
        Eq("decimal entry caption", "7 h 30 min", e.Caption);

        e = Keys("12");
        Eq("whole hours left", "12", e.MainText);
        Eq("whole hours right", S("12,00"), e.DecimalText);
        Eq("whole hours caption", "12 h 0 min", e.Caption);

        // Limits.
        Eq("max 7 hour digits", "1234567", Keys("12345678").MainText);
        Eq("max 2 minute digits", "7:30", Keys("7:300").MainText);
        Eq("max 6 decimals", S("1,123456"), Keys("1,1234567").MainText);
        Eq("one separator per entry", "7:30", Keys("7:3:0").MainText);
        Eq("no ':' after ','", S("7,5"), Keys("7,5:").MainText);
        Eq("no ',' after ':'", "7:3", Keys("7:3,").MainText);
        Eq("leading ','", S("0,5"), Keys(",5").MainText);
        Eq("leading ':'", "0:5", Keys(":5").MainText);
        Eq("leading ':' value", S("0,08"), Keys(":5").DecimalText);
        Eq("typed leading zero kept", "07:30", Keys("07:30").MainText);

        // Aliases.
        Eq("alias u", "7:30", Keys("7u30").MainText);
        Eq("alias h", "7:30", Keys("7h30").MainText);
        Eq("alias .", S("7,5"), Keys("7.5").MainText);

        // Backspace edits the entry only.
        Eq("backspace once", "7:", Keys("7:3<BK>").MainText);
        Eq("backspace twice", "7", Keys("7:3<BK><BK>").MainText);
        Eq("backspace to empty shows 0", "0", Keys("7<BK>").MainText);
        Eq("backspace to empty value", S("0,00"), Keys("7<BK>").DecimalText);
        Eq("backspace on empty entry", "0", Keys("7<BK><BK>").MainText);
        Eq("backspace has no effect on a result", "08:30", Keys("7:30 + 1 = <BK>").MainText);
        Eq("backspace before any entry", "00:00", Keys("<BK>").MainText);
        Eq("typing after backspace", "7:5", Keys("7:3<BK>5").MainText);
        Eq("backspace then operator uses the shortened entry", "07:00", Keys("7:3<BK><BK>+").MainText);
    }

    static void CalcStateTests()
    {
        // CE clears the current entry.
        CalcEngine e = Keys("7:30 + 5 <CE>");
        Eq("CE shows zero", "0", e.MainText);
        Eq("CE keeps expression", "07:30 +", e.Expression);
        Press(e, "=");
        Eq("CE then = adds zero", "07:30", e.MainText);
        e = Keys("7:30 + 5 <CE> 2 =");
        Eq("CE then new entry", "09:30", e.MainText);

        // C clears everything.
        e = Keys("7:30 + 5 <C>");
        Eq("C left", "00:00", e.MainText);
        Eq("C right", S("0,00"), e.DecimalText);
        Eq("C expression", "", e.Expression);
        Eq("C caption", "", e.Caption);
        Press(e, "+ 1 =");
        Eq("after C starts fresh", "01:00", e.MainText);

        // CE after = starts over; CE before anything does nothing visible.
        e = Keys("7:30 + 1 = <CE>");
        Eq("CE after = resets", "00:00", e.MainText);
        Eq("CE before anything", "00:00", Keys("<CE>").MainText);
        Eq("CE before anything caption", "", Keys("<CE>").Caption);

        // After =, digits start a new calculation, operators continue.
        e = Keys("7:30 + 1 = 2");
        Eq("digit after = starts new", "2", e.MainText);
        e = Keys("7:30 + 1 = + 2 =");
        Eq("operator after = continues", "10:30", e.MainText);
        e = Keys("7:30 + 1 = :");
        Eq("separator after = starts new", "0:", e.MainText.Replace(TimeMath.DecimalSeparator, ","));

        // Separator inside a pending state.
        e = Keys("7:30 + ,5 =");
        Eq("0,5 as second operand", "08:00", e.MainText);

        // "=" without anything to do.
        e = Keys("=");
        Eq("= at idle left", "00:00", e.MainText);
        Eq("= at idle expression", "", e.Expression);
        e = Keys("0:90 = =");
        Eq("= twice after a bare value", "01:30", e.MainText);

        // Negative results and captions.
        e = Keys("7:00 - 8:30 =");
        Eq("negative caption", "-1 h 30 min", e.Caption);
        Eq("negative decimal", S("-1,50"), e.DecimalText);
        e = Keys("0:30 - 0:30 =");
        Eq("zero result", "00:00", e.MainText);
        Eq("zero decimal", S("0,00"), e.DecimalText);
        Eq("zero caption", "0 h 0 min", e.Caption);
        e = Keys("0:01 - 0:02 =");
        Eq("-1 minute", "-00:01", e.MainText);

        // Full precision accumulator: only the display rounds.
        e = Keys("7:30 / 4 =");
        Eq("precision caption (rounded)", "1 h 53 min", e.Caption);
        e = Keys("7:30 / 4 = * 4 =");
        Eq("precision restored", "07:30", e.MainText);
        e = Keys("7:30 / 4 = + 0:07 =");
        Eq("precision accumulates", "02:00", e.MainText);
        Eq("precision accumulates right", S("1,99"), e.DecimalText);

        // Copy texts.
        e = Keys("7:30 + 7:20 =");
        Eq("copy left", "14:50", e.CopyMainText);
        Eq("copy right", S("14,83"), e.CopyDecimalText);
        e = Keys("7:30 / 0 =");
        Eq("copy left on error", "", e.CopyMainText);
        Eq("copy right on error", "", e.CopyDecimalText);

        // Key aliases and robustness.
        e = new CalcEngine();
        e.Press("garbage");
        e.Press("");
        e.Press(null);
        Eq("unknown keys ignored", "00:00", e.MainText);
        e = Keys("7:30 + 7:20 =");
        e.Press("c");
        Eq("lowercase c", "00:00", e.MainText);
    }

    static void CalcErrorTests()
    {
        CalcEngine e = Keys("7:30 / 0 =");
        True("div0 is error", e.IsError);
        Eq("div0 text", "Cannot divide by zero", e.MainText);
        Eq("div0 right empty", "", e.DecimalText);
        Eq("div0 caption empty", "", e.Caption);
        foreach (string k in new[] { "+", "-", "*", "/", "=", ":", ",", "BACK" })
            False("div0 disables " + k, e.IsEnabled(k));
        foreach (string k in new[] { "0", "5", "9", "C", "CE" })
            True("div0 keeps " + k, e.IsEnabled(k));
        // Disabled keys do nothing.
        Press(e, "+ = : ,");
        Eq("keys ignored in error", "Cannot divide by zero", e.MainText);
        // A digit resets and starts an entry.
        e.Press("5");
        False("digit clears the error", e.IsError);
        Eq("digit after error starts entry", "5", e.MainText);
        Eq("expression cleared after error digit", "", e.Expression);
        // C and CE reset.
        e = Keys("7:30 / 0 = <C>");
        False("C clears error", e.IsError);
        Eq("C after error", "00:00", e.MainText);
        e = Keys("7:30 / 0 = <CE>");
        False("CE clears error", e.IsError);
        Eq("CE after error", "00:00", e.MainText);
        Eq("CE after error expression", "", e.Expression);
        // Division by zero through a chain and a zero factor spelled as h:mm.
        e = Keys("7:30 / 0:00 +");
        Eq("div0 mid-chain", "Cannot divide by zero", e.MainText);
        e = Keys("7:30 / 0,0 =");
        Eq("div0 with 0,0", "Cannot divide by zero", e.MainText);
        e = Keys("0 / 0 =");
        Eq("0/0", "Cannot divide by zero", e.MainText);
        // Dividing by a tiny non-zero factor is fine (1 000 000 hours is below the limit).
        e = Keys("1:00 / 0,000001 =");
        Eq("tiny factor", "1000000:00", e.MainText);
        Eq("tiny factor right", S("1000000,00"), e.DecimalText);
        e = Keys("10:00 / 0,000001 =");
        Eq("tiny factor overflows", "Overflow", e.MainText);

        // Overflow.
        e = Keys("9999999 * 9999999 =");
        Eq("overflow multiply", "Overflow", e.MainText);
        Eq("overflow right empty", "", e.DecimalText);
        False("overflow disables +", e.IsEnabled("+"));
        e = Keys("9999999 + 1 =");
        Eq("overflow add", "Overflow", e.MainText);
        e = Keys("9999999 + 0:59 =");
        False("below the limit is fine", e.IsError);
        e = Keys("0 - 9999999 - 1 =");
        Eq("overflow negative", "Overflow", e.MainText);
        e = Keys("9999999 + 1 = 5");
        Eq("digit clears overflow", "5", e.MainText);
        Eq("overflow shown mid-chain", "Overflow", Keys("9999999 + 1 *").MainText);
    }

    static void CalcPasteTests()
    {
        CalcEngine e = new CalcEngine();
        True("paste 08:30", e.Paste("08:30"));
        Eq("pasted left", "08:30", e.MainText);
        Eq("pasted right", S("8,50"), e.DecimalText);
        True("paste with spaces", e.Paste("  1:15 "));
        Eq("pasted with spaces", "1:15", e.MainText);
        True("paste 1.5", e.Paste("1.5"));
        Eq("pasted 1.5", S("1,5"), e.MainText);
        Eq("pasted 1.5 value", S("1,50"), e.DecimalText);
        True("paste 1,5", e.Paste("1,5"));
        Eq("pasted 1,5", S("1,5"), e.MainText);
        True("paste 8u30", e.Paste("8u30"));
        Eq("pasted 8u30", "8:30", e.MainText);
        True("paste 8", e.Paste("8"));
        Eq("pasted 8", "8", e.MainText);

        string before = e.MainText;
        False("paste garbage", e.Paste("abc"));
        False("paste empty", e.Paste(""));
        False("paste null", e.Paste(null));
        False("paste negative", e.Paste("-1:30"));
        False("paste too many digits", e.Paste("12345678"));
        False("paste 1,2,3", e.Paste("1,2,3"));
        False("paste 7:300", e.Paste("7:300"));
        Eq("failed paste changes nothing", before, e.MainText);

        // Pasting after = starts a new calculation; typing continues the pasted entry.
        e = Keys("7:30 + 1 =");
        True("paste after =", e.Paste("2:00"));
        Eq("paste after = is a new entry", "2:00", e.MainText);
        Eq("paste after = clears expression", "", e.Expression);
        Press(e, "+ 1 =");
        Eq("pasted value is usable", "03:00", e.MainText);

        e = Keys("7:30 +");
        True("paste as second operand", e.Paste("0:30"));
        Press(e, "=");
        Eq("pasted second operand", "08:00", e.MainText);

        e = Keys("7:30 / 0 =");
        True("paste resets error", e.Paste("1:00"));
        False("paste cleared error", e.IsError);
        Eq("paste after error", "1:00", e.MainText);
    }

    static void CalcPrecisionTests()
    {
        // The decimal type keeps these exact: no 0.1 + 0.2 style errors.
        Result("0,1 + 0,2 =", "00:18", "0,30");
        Result("0,7 + 0,1 =", "00:48", "0,80");
        Result("8,33 + 0,01 =", "08:20", "8,34");
        // 1/3 of an hour times 3 comes back to the minute.
        Result("1:00 / 3 = * 3 =", "01:00", "1,00");
        // Midpoint away from zero on both displays.
        Result("0:01 / 2 =", "00:01", "0,01");
        Result("0:03 / 2 =", "00:02", "0,03");
        Result("0:00 - 0:01 / 2 =", "-00:01", "-0,01");
        // 100 additions of 0:01 are exactly 1:40.
        var sb = new StringBuilder("0:01");
        for (int i = 0; i < 99; i++) sb.Append("+0:01");
        sb.Append("=");
        Result(sb.ToString(), "01:40", "1,67");
        // Long chains keep working.
        var sb2 = new StringBuilder("1");
        for (int i = 0; i < 200; i++) sb2.Append("+1");
        sb2.Append("=");
        Result(sb2.ToString(), "201:00", "201,00");
    }

    // ------------------------------------------------------------ limits (review findings)

    static void CalcLimitTests()
    {
        // A typed or pasted entry of 10 000 000 hours or more is an overflow, like the same value
        // reached through an operation (minutes up to 99 can be typed: 9999999:60 is 10000000:00).
        foreach (string k in new[] { "9999999:60 =", "9999999:99 =", "9999999:60 +", "9999999:99 *", "9999999:60 -", "9999999:60 /" })
        {
            CalcEngine e = Keys(k);
            Eq("overflow for '" + k + "'", "Overflow", e.MainText);
            True("... is an error", e.IsError);
            Eq("... right is empty", "", e.DecimalText);
        }
        CalcEngine p = new CalcEngine();
        True("paste 9999999:99", p.Paste("9999999:99"));
        Press(p, "=");
        Eq("paste 9999999:99 then = overflows", "Overflow", p.MainText);
        p = new CalcEngine();
        p.Paste("9999999:99");
        Press(p, "+");
        Eq("paste 9999999:99 then + overflows", "Overflow", p.MainText);
        Eq("a digit leaves the overflow", "5", KeysAfter("9999999:60 =", "5").MainText);
        Eq("just below the limit is fine", "9999999:59", Keys("9999999:59 =").MainText);
        Eq("... and 9999999:59 + 0 too", "9999999:59", Keys("9999999:59 + 0 =").MainText);
        Eq("... and 9999999,999999 = is below the limit", "10000000:00", Keys("9999999,999999 =").MainText);
        False("... no error", Keys("9999999,999999 =").IsError);

        // A pasted entry that starts with a separator shows a leading zero, like a typed one.
        foreach (string t in new[] { ",5", ".5", ":30", ":", "," })
        {
            CalcEngine typed = Keys(t == ".5" ? ",5" : t);
            CalcEngine pasted = new CalcEngine();
            True("paste <" + t + ">", pasted.Paste(t));
            Eq("pasted <" + t + "> shows like the typed entry", typed.MainText, pasted.MainText);
            Eq("pasted <" + t + "> has the same value", typed.DecimalText, pasted.DecimalText);
        }
        Eq("pasted ,5 value", S("0,50"), PasteThen(",5").DecimalText);
        Eq("pasted :30 =", "00:30", PasteThen(":30", "=").MainText);
    }

    static CalcEngine KeysAfter(string keys, string more)
    {
        CalcEngine e = Keys(keys);
        Press(e, more);
        return e;
    }

    static CalcEngine PasteThen(string text, string keys)
    {
        CalcEngine e = new CalcEngine();
        e.Paste(text);
        Press(e, keys);
        return e;
    }

    static CalcEngine PasteThen(string text)
    {
        return PasteThen(text, "");
    }

    // ------------------------------------------------------------ Convert (the button)

    static void ConvertButtonTests()
    {
        string normal, other, caption;

        // Convert tidies the active text but never moves the other field: it is computed from the
        // text that is in the field, not from the tidied (rounded) text.
        Converter.Convert(Converter.Right, S("8,058"), 2, out normal, out other, out caption);
        Eq("Convert 8,058: text", S("8,06"), normal);
        Eq("Convert 8,058: left stays 08:03", "08:03", other);
        Eq("Convert 8,058: caption", "8 h 3 min", caption);
        Converter.Convert(Converter.Right, S("9,994"), 2, out normal, out other, out caption);
        Eq("Convert 9,994: left stays 10:00", "10:00", other);
        Eq("Convert 9,994: text", S("9,99"), normal);
        Converter.Convert(Converter.Right, S("0,0083"), 2, out normal, out other, out caption);
        Eq("Convert 0,0083: left stays 00:00", "00:00", other);
        Converter.Convert(Converter.Right, S("8,058"), 4, out normal, out other, out caption);
        Eq("Convert 8,058 at 4 places: text", S("8,0580"), normal);
        Converter.Convert(Converter.Left, "8:3", 2, out normal, out other, out caption);
        Eq("Convert 8:3: text", "08:03", normal);
        Eq("Convert 8:3: right", S("8,05"), other);
        Converter.Convert(Converter.Left, ":", 2, out normal, out other, out caption);
        Eq("Convert on a lone separator: text", "", normal);
        Eq("Convert on a lone separator: other", "", other);
        Eq("Convert on a lone separator: caption", "", caption);

        // The 8 digit hour count that formatting can produce (10000000) survives Convert and Normalize.
        Eq("Normalize keeps 10000000,65", S("10000000,65"), Converter.Normalize(Converter.Right, S("10000000,65")));
        Eq("Normalize keeps 10000000:39", "10000000:39", Converter.Normalize(Converter.Left, "10000000:39"));
        Eq("Normalize 9999999:60", "10000000:00", Converter.Normalize(Converter.Left, "9999999:60"));
        Eq("Normalize 9999999,999", S("10000000,00"), Converter.Normalize(Converter.Right, S("9999999,999")));
        Eq("Normalize 9999999,999999 twice", S("10000000,00"), Converter.Normalize(Converter.Right, Converter.Normalize(Converter.Right, S("9999999,999999"))));
        Converter.Convert(Converter.Right, S("10000000,65"), 2, out normal, out other, out caption);
        Eq("Convert 10000000,65: text", S("10000000,65"), normal);
        Eq("Convert 10000000,65: left", "10000000:39", other);
        Converter.Convert(Converter.Left, "10000000:39", 2, out normal, out other, out caption);
        Eq("Convert 10000000:39: right", S("10000000,65"), other);
        Eq("typed text still stops at 7 hour digits", "1000000:39", Converter.Filter(Converter.Left, "10000000:39"));
        Eq("field text may carry 10000000", "10000000:39", Converter.FilterField(Converter.Left, "10000000:39"));
        Eq("... but no other 8 digit count", "2000000:00", Converter.FilterField(Converter.Left, "20000000:00"));
        Eq("... nor 10000001", "1000000:01", Converter.FilterField(Converter.Left, "10000001:01"));

        // Random texts: Convert agrees with the live conversion and normalizing is idempotent.
        var rnd = new Random(11);
        string alpha = "0123456789:,.uh";
        int bad = 0;
        for (int n = 0; n < 20000; n++)
        {
            int f = rnd.Next(2);
            var sb = new StringBuilder();
            int len = rnd.Next(0, 12);
            for (int i = 0; i < len; i++) sb.Append(alpha[rnd.Next(alpha.Length)]);
            string text = Converter.Filter(f, sb.ToString());
            string o, c;
            Converter.Compute(f, text, out o, out c);
            string nrm, o2, c2;
            Converter.Convert(f, text, 2, out nrm, out o2, out c2);
            if (o != o2 || c != c2 || nrm != Converter.Normalize(f, text) || Converter.Normalize(f, nrm) != nrm) bad++;
        }
        Eq("Convert matches the live conversion on random text (bad count)", "0", bad.ToString());
    }

    // ------------------------------------------------------------ decimal places (SPEC 6)

    static void DecimalPlacesTests()
    {
        // The SPEC examples: 08:20 is 8,33 / 8,333 / 8,3333 / 8,33333 / 8,333333.
        string[] expect = { "8,33", "8,333", "8,3333", "8,33333", "8,333333" };
        for (int p = 2; p <= 6; p++)
        {
            Eq("FormatDecimal 08:20 at " + p, S(expect[p - 2]), TimeMath.FormatDecimal(500m, p));
            string o, c;
            Converter.Compute(Converter.Left, "08:20", p, out o, out c);
            Eq("converter 08:20 at " + p, S(expect[p - 2]), o);
            Eq("converter caption does not depend on the places", "8 h 20 min", c);
            Eq("Format at " + p, S(expect[p - 2]), Converter.Format(Converter.Right, 500m, p));
            Eq("calculator 08:20 at " + p, S(expect[p - 2]), KeysAt(p, "8:20 =").DecimalText);
        }
        Eq("the default is 2 places", S("8,33"), TimeMath.FormatDecimal(500m));
        Eq("the default calculator has 2 places", "2", new CalcEngine().DecimalPlaces.ToString());
        Eq("h:mm stays whole minutes at 6 places", "08:20", Converter.Format(Converter.Left, 500m, 6));
        Eq("Normalize 8,5 at 4 places", S("8,5000"), Converter.Normalize(Converter.Right, S("8,5"), 4));
        Eq("Normalize 08:30 at 4 places (left)", "08:30", Converter.Normalize(Converter.Left, "830", 4));

        // 7:30 / 4 = 1,875 hours.
        Eq("7:30 / 4 at 2", S("1,88"), KeysAt(2, "7:30 / 4 =").DecimalText);
        Eq("7:30 / 4 at 3", S("1,875"), KeysAt(3, "7:30 / 4 =").DecimalText);
        Eq("7:30 / 4 at 4", S("1,8750"), KeysAt(4, "7:30 / 4 =").DecimalText);
        Eq("7:30 / 4 at 6", S("1,875000"), KeysAt(6, "7:30 / 4 =").DecimalText);
        Eq("7:00 - 8:30 at 3 places", S("-1,500"), KeysAt(3, "7:00 - 8:30 =").DecimalText);

        // 0:01 is 0,016666... hours.
        Eq("0:01 at 2", S("0,02"), TimeMath.FormatDecimal(1m, 2));
        Eq("0:01 at 3", S("0,017"), TimeMath.FormatDecimal(1m, 3));
        Eq("0:01 at 4", S("0,0167"), TimeMath.FormatDecimal(1m, 4));
        Eq("0:01 at 5", S("0,01667"), TimeMath.FormatDecimal(1m, 5));
        Eq("0:01 at 6", S("0,016667"), TimeMath.FormatDecimal(1m, 6));
        Eq("calculator 0:01 at 4", S("0,0167"), KeysAt(4, "0:1 =").DecimalText);
        Eq("-0:01 at 4 keeps its sign", S("-0,0167"), TimeMath.FormatDecimal(-1m, 4));
        Eq("a value that rounds to zero has no sign (at 4 places)", S("0,0000"), TimeMath.FormatDecimal(-0.001m, 4));

        // Changing the places re-formats from the exact value, not from the rounded text.
        CalcEngine e = KeysAt(2, "7:30 / 4 =");
        Eq("start", S("1,88"), e.DecimalText);
        e.DecimalPlaces = 4;
        Eq("re-formatted to 4", S("1,8750"), e.DecimalText);
        Eq("... the left display is unchanged", "01:53", e.MainText);
        Eq("... the caption is unchanged", "1 h 53 min", e.Caption);
        e.DecimalPlaces = 2;
        Eq("back to 2 from the exact value", S("1,88"), e.DecimalText);
        e = KeysAt(2, "1:00 / 3 =");
        e.DecimalPlaces = 6;
        Eq("1:00 / 3 at 6 places", S("0,333333"), e.DecimalText);
        Press(e, "* 3 =");
        Eq("the accumulator kept its full precision", "01:00", e.MainText);
        Eq("... 1,000000 hours", S("1,000000"), e.DecimalText);
        e = KeysAt(2, "7:3");
        e.DecimalPlaces = 4;
        Eq("a live entry follows the places", S("7,0500"), e.DecimalText);

        // Anything outside 2..6 means the default.
        e = new CalcEngine();
        foreach (int bad in new[] { -1, 0, 1, 7, 100 })
        {
            e.DecimalPlaces = 4;
            e.DecimalPlaces = bad;
            Eq("places " + bad + " is the default", "2", e.DecimalPlaces.ToString());
        }
        Eq("FormatDecimal with an invalid places value uses the default", S("8,33"), TimeMath.FormatDecimal(500m, 9));
        Eq("ClampDecimals 6", "6", TimeMath.ClampDecimals(6).ToString());
        Eq("ClampDecimals 3", "3", TimeMath.ClampDecimals(3).ToString());

        // Typed input keeps up to 6 decimals whatever the setting; more places make the round trip finer.
        string back, cap;
        Converter.Compute(Converter.Right, S("8,333333"), 6, out back, out cap);
        Eq("8,333333 gives 08:20", "08:20", back);
        int bad2 = 0;
        for (int m = 0; m <= 6000; m++)
            for (int p = 2; p <= 6; p++)
            {
                string right = TimeMath.FormatDecimal(m, p);
                string left, c2;
                Converter.Compute(Converter.Right, right, p, out left, out c2);
                if (left != TimeMath.FormatHm(m)) bad2++;
            }
        Eq("left -> right -> left is exact at every number of places (bad count)", "0", bad2.ToString());

        // Midpoint away from zero at any number of places, also for values that are an exact half
        // but not exactly representable after a division.
        Eq("0:05 / 6 * 3 shows 00:03", "00:03", Keys("0:05 / 6 * 3 =").MainText);
        Eq("0,125 h at 2 places", S("0,13"), TimeMath.FormatDecimal(7.5m, 2));
        Eq("0,125 h at 3 places", S("0,125"), TimeMath.FormatDecimal(7.5m, 3));
        Eq("an exact half minute from divisions rounds up", "00:03", Keys("0:05 / 6 * 3 =").MainText);
        Eq("... and its decimal at 2 places", S("0,04"), Keys("0:05 / 6 * 3 =").DecimalText);
        Eq("1:00 / 3 / 2 * 6 / 3 = still 01:00", "01:00", Keys("1:00 / 3 / 2 * 6 / 3 * 3 =").MainText);
    }

    // ------------------------------------------------------------ HourCalc.ini

    static string ThemeNum(ThemeMode t) { return ((int)t).ToString(); }

    // The two parsers on the same ini text: "<places> <theme>".
    static string BothOf(string text)
    {
        return Settings.ParseDecimalPlaces(text) + " " + ThemeNum(Settings.ParseTheme(text));
    }

    static string Loaded(string path)
    {
        int places;
        ThemeMode theme;
        Settings.Load(path, out places, out theme);
        return places + " " + ThemeNum(theme);
    }

    // The settings file on disk: where it lives, how it is saved (the same cases as tests\native\core_tests.cpp).
    static void SettingsSaveTests(string dir)
    {
        const string file5dark = "DecimalPlaces=5\r\nTheme=dark\r\n";

        // The folder is made by the first save (SPEC 6c), one level.
        string sub = Path.Combine(dir, "HourCalc");
        string subIni = Path.Combine(sub, "HourCalc.ini");
        False("the folder does not exist yet", Directory.Exists(sub));
        True("the first save makes the folder", Settings.Save(subIni, 4, ThemeMode.Light));
        Eq("... and the file is in it", "DecimalPlaces=4\r\nTheme=light\r\n", File.ReadAllText(subIni));
        True("a save into the existing folder", Settings.Save(subIni, 5, ThemeMode.Dark));
        Eq("... replaces the contents", file5dark, File.ReadAllText(subIni));
        False("a chain of missing folders is not made", Settings.Save(Path.Combine(dir, "a", "b", "HourCalc.ini"), 3, ThemeMode.Auto));
        False("... not even its first folder", Directory.Exists(Path.Combine(dir, "a")));
        string subTheme = Path.Combine(dir, "fromtheme", "HourCalc.ini");
        True("SaveTheme makes the folder too", Settings.SaveTheme(subTheme, ThemeMode.Dark, 3));
        Eq("... and writes both", "DecimalPlaces=3\r\nTheme=dark\r\n", File.ReadAllText(subTheme));
        Eq("no temporary file is left in the folders", "0", (Directory.GetFiles(sub, "*.tmp").Length + Directory.GetFiles(Path.Combine(dir, "fromtheme"), "*.tmp").Length + Directory.GetFiles(dir, "*.tmp").Length).ToString());
        Directory.Delete(Path.Combine(dir, "fromtheme"), true);

        // One setting at a time: the other one is read from the file again.
        string f = Path.Combine(dir, "keep.ini");
        True("SaveTheme without a file: the places of the caller", Settings.SaveTheme(f, ThemeMode.Dark, 5));
        Eq("... both are in the file", file5dark, File.ReadAllText(f));
        File.Delete(f);
        True("SavePlaces without a file: the theme of the caller", Settings.SavePlaces(f, 4, ThemeMode.Light));
        Eq("... both are in the file", "DecimalPlaces=4\r\nTheme=light\r\n", File.ReadAllText(f));
        // Two HourCalc windows that both started with the defaults. A picks Dark, then B changes the places: the Dark of A survives.
        File.Delete(f);
        True("A picks Dark", Settings.SaveTheme(f, ThemeMode.Dark, 2));
        True("B (which still believes in Auto) picks 4 places", Settings.SavePlaces(f, 4, ThemeMode.Auto));
        Eq("B keeps the Dark of A", "DecimalPlaces=4\r\nTheme=dark\r\n", File.ReadAllText(f));
        True("A (which still believes in 2 places) picks Light", Settings.SaveTheme(f, ThemeMode.Light, 2));
        Eq("A keeps the 4 places of B", "DecimalPlaces=4\r\nTheme=light\r\n", File.ReadAllText(f));
        Eq("a load gives both", "4 1", Loaded(f));
        File.WriteAllText(f, "DecimalPlaces=6");
        True("a file with only the places", Settings.SaveTheme(f, ThemeMode.Dark, 3));
        Eq("... the places of the file stay, not those of the caller", "DecimalPlaces=6\r\nTheme=dark\r\n", File.ReadAllText(f));
        File.WriteAllText(f, "Theme=dark");
        True("a file with only the theme", Settings.SavePlaces(f, 5, ThemeMode.Light));
        Eq("... the theme of the file stays", file5dark, File.ReadAllText(f));
        File.WriteAllText(f, "nothing useful in here\r\n");
        True("a file without a setting in it", Settings.SavePlaces(f, 5, ThemeMode.Dark));
        Eq("... has the default theme for the other one", "DecimalPlaces=5\r\nTheme=auto\r\n", File.ReadAllText(f));
        File.WriteAllText(f, "");
        True("an empty file", Settings.SaveTheme(f, ThemeMode.Light, 4));
        Eq("... has the default places for the other one", "DecimalPlaces=2\r\nTheme=light\r\n", File.ReadAllText(f));
        False("SavePlaces with an empty path", Settings.SavePlaces("", 3, ThemeMode.Auto));
        False("SavePlaces with a null path", Settings.SavePlaces(null, 3, ThemeMode.Auto));
        False("SaveTheme with an empty path", Settings.SaveTheme("", ThemeMode.Auto, 3));
        False("SaveTheme on a folder", Settings.SaveTheme(dir, ThemeMode.Dark, 3));
        False("SavePlaces on a folder", Settings.SavePlaces(dir, 3, ThemeMode.Dark));

        // A file that is there but cannot be read is not overwritten with the defaults of a failed read.
        File.WriteAllText(f, file5dark);
        using (new FileStream(f, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            Eq("locked: a load gives the defaults", "2 0", Loaded(f));
            False("locked: SaveTheme writes nothing", Settings.SaveTheme(f, ThemeMode.Light, 2));
            False("locked: SavePlaces writes nothing", Settings.SavePlaces(f, 3, ThemeMode.Auto));
            False("locked: a plain Save cannot replace it either", Settings.Save(f, 3, ThemeMode.Auto));
        }
        Eq("the file is as it was", file5dark, File.ReadAllText(f));
        Eq("the failed saves left no temporary file", "0", Directory.GetFiles(dir, "*.tmp").Length.ToString());
        True("unlocked again: SaveTheme", Settings.SaveTheme(f, ThemeMode.Light, 2));
        Eq("... keeps the 5 places that the failed read of the start-up never saw", "DecimalPlaces=5\r\nTheme=light\r\n", File.ReadAllText(f));

        // The attributes of the file.
        FileAttributes[] attrs = { FileAttributes.Hidden, FileAttributes.System, FileAttributes.Hidden | FileAttributes.System };
        foreach (FileAttributes a in attrs)
        {
            File.SetAttributes(f, FileAttributes.Normal);
            File.WriteAllText(f, file5dark);
            File.SetAttributes(f, a);
            True("a " + a + " settings file is saved", Settings.Save(f, 3, ThemeMode.Light));
            File.SetAttributes(f, FileAttributes.Normal);
            Eq("... with the new contents (" + a + ")", "DecimalPlaces=3\r\nTheme=light\r\n", File.ReadAllText(f));
        }
        File.WriteAllText(f, file5dark);
        File.SetAttributes(f, FileAttributes.ReadOnly);
        False("a read-only settings file is not saved (it is the user's mark)", Settings.Save(f, 3, ThemeMode.Light));
        False("... nor by SaveTheme", Settings.SaveTheme(f, ThemeMode.Light, 3));
        False("... nor by SavePlaces", Settings.SavePlaces(f, 3, ThemeMode.Light));
        File.SetAttributes(f, FileAttributes.Normal);
        Eq("... and it is as it was", file5dark, File.ReadAllText(f));
        Eq("no temporary file is left behind", "0", Directory.GetFiles(dir, "*.tmp").Length.ToString());

        // The default path (SPEC 6c).
        string def = Settings.DefaultPath;
        True("the default path is in the local application data folder", def != null &&
            string.Equals(def, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HourCalc", "HourCalc.ini"), StringComparison.OrdinalIgnoreCase));
        Eq("... and ends in HourCalc\\HourCalc.ini", "HourCalc\\HourCalc.ini", def.Substring(def.Length - 21));

        File.Delete(f);
        File.Delete(subIni);
        Directory.Delete(sub);
    }

    static void SettingsTests()
    {
        Eq("empty text", "2", Settings.ParseDecimalPlaces("").ToString());
        Eq("null text", "2", Settings.ParseDecimalPlaces(null).ToString());
        Eq("DecimalPlaces=3", "3", Settings.ParseDecimalPlaces("DecimalPlaces=3").ToString());
        Eq("with CRLF", "4", Settings.ParseDecimalPlaces("DecimalPlaces=4\r\n").ToString());
        Eq("case and spaces", "6", Settings.ParseDecimalPlaces("  decimalplaces = 6  ").ToString());
        Eq("comments and sections are skipped", "5", Settings.ParseDecimalPlaces("; comment\n# another\n[HourCalc]\nDecimalPlaces=5\n").ToString());
        Eq("another key is ignored", "2", Settings.ParseDecimalPlaces("Other=4").ToString());
        Eq("7 is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=7").ToString());
        Eq("1 is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=1").ToString());
        Eq("-3 is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=-3").ToString());
        Eq("text is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=abc").ToString());
        Eq("empty value is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=").ToString());
        Eq("3.5 is invalid", "2", Settings.ParseDecimalPlaces("DecimalPlaces=3.5").ToString());
        Eq("the first DecimalPlaces line decides", "3", Settings.ParseDecimalPlaces("DecimalPlaces=3\nDecimalPlaces=5").ToString());
        Eq("a missing file", "2", Settings.LoadDecimalPlaces(Path.Combine(Path.GetTempPath(), "HourCalc-missing-" + Guid.NewGuid().ToString("N") + ".ini")).ToString());
        Eq("a null path", "2", Settings.LoadDecimalPlaces(null).ToString());
        Eq("file name", "HourCalc.ini", Settings.FileName);

        // ---- the theme
        Eq("theme: empty text is auto", "0", ThemeNum(Settings.ParseTheme("")));
        Eq("theme: null text is auto", "0", ThemeNum(Settings.ParseTheme(null)));
        Eq("theme: the default is auto", "0", ThemeNum(Settings.DefaultTheme));
        Eq("theme: Theme=auto", "0", ThemeNum(Settings.ParseTheme("Theme=auto")));
        Eq("theme: Theme=light", "1", ThemeNum(Settings.ParseTheme("Theme=light")));
        Eq("theme: Theme=dark", "2", ThemeNum(Settings.ParseTheme("Theme=dark")));
        Eq("theme: key and value are not case sensitive", "2", ThemeNum(Settings.ParseTheme("THEME=DARK")));
        Eq("theme: ... mixed case", "1", ThemeNum(Settings.ParseTheme("tHeMe=LiGhT")));
        Eq("theme: spaces around key and value", "1", ThemeNum(Settings.ParseTheme("  theme = Light  ")));
        Eq("theme: with CRLF", "2", ThemeNum(Settings.ParseTheme("Theme=dark\r\n")));
        Eq("theme: comments and sections are skipped", "2", ThemeNum(Settings.ParseTheme("; comment\n# another\n[HourCalc]\nTheme=dark\n")));
        Eq("theme: commented out", "0", ThemeNum(Settings.ParseTheme(";Theme=dark")));
        Eq("theme: a section line", "0", ThemeNum(Settings.ParseTheme("[Theme=dark]")));
        Eq("theme: another key is ignored", "0", ThemeNum(Settings.ParseTheme("Other=dark")));
        Eq("theme: a longer key is another key", "0", ThemeNum(Settings.ParseTheme("Themes=dark")));
        Eq("theme: no value is auto", "0", ThemeNum(Settings.ParseTheme("Theme=")));
        Eq("theme: no equals sign is auto", "0", ThemeNum(Settings.ParseTheme("Theme")));
        Eq("theme: an unknown value is auto", "0", ThemeNum(Settings.ParseTheme("Theme=blue")));
        Eq("theme: a longer value is auto", "0", ThemeNum(Settings.ParseTheme("Theme=darker")));
        Eq("theme: two words are auto", "0", ThemeNum(Settings.ParseTheme("Theme=dark light")));
        Eq("theme: a number is auto", "0", ThemeNum(Settings.ParseTheme("Theme=2")));
        Eq("theme: the first Theme line decides", "2", ThemeNum(Settings.ParseTheme("Theme=dark\nTheme=light")));
        Eq("theme: ... even when it is invalid", "0", ThemeNum(Settings.ParseTheme("Theme=blue\nTheme=dark")));
        Eq("theme: a NUL after the value is not the value", "0", ThemeNum(Settings.ParseTheme("Theme=dark\0")));
        Eq("DecimalPlaces and Theme in either order", "4 2", BothOf("DecimalPlaces=4\r\nTheme=dark\r\n"));
        Eq("... the other order", "5 1", BothOf("Theme=light\nDecimalPlaces=5"));
        Eq("... with duplicates the first of each key decides", "3 2", BothOf("Theme=dark\nDecimalPlaces=3\nTheme=light\nDecimalPlaces=6"));
        Eq("an invalid theme does not change the places", "3 0", BothOf("DecimalPlaces=3\nTheme=blue"));
        Eq("an invalid place count does not change the theme", "2 2", BothOf("DecimalPlaces=9\nTheme=dark"));
        Eq("a file with only Theme has the default places", "2 1", BothOf("Theme=light"));
        Eq("a file with only DecimalPlaces has the theme auto", "6 0", BothOf("DecimalPlaces=6"));
        Eq("ThemeToString auto", "auto", Settings.ThemeToString(ThemeMode.Auto));
        Eq("ThemeToString light", "light", Settings.ThemeToString(ThemeMode.Light));
        Eq("ThemeToString dark", "dark", Settings.ThemeToString(ThemeMode.Dark));
        Eq("ThemeToString of a value outside the enumeration is auto", "auto", Settings.ThemeToString((ThemeMode)7));
        Eq("theme of a missing file", "0", ThemeNum(Settings.LoadTheme(Path.Combine(Path.GetTempPath(), "HourCalc-missing-" + Guid.NewGuid().ToString("N") + ".ini"))));
        Eq("theme of a null path", "0", ThemeNum(Settings.LoadTheme(null)));

        string dir = Path.Combine(Path.GetTempPath(), "HourCalcTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            string path = Path.Combine(dir, "HourCalc.ini");
            int places;
            ThemeMode theme;
            Settings.Load(Path.Combine(dir, "missing.ini"), out places, out theme);
            Eq("Load of a missing file: the defaults", "2 0", places + " " + ThemeNum(theme));
            Settings.Load(null, out places, out theme);
            Eq("Load of a null path: the defaults", "2 0", places + " " + ThemeNum(theme));
            True("save 4 and dark", Settings.Save(path, 4, ThemeMode.Dark));
            Eq("the file holds both settings, DecimalPlaces first", "DecimalPlaces=4\r\nTheme=dark\r\n", File.ReadAllText(path));
            Eq("load 4", "4", Settings.LoadDecimalPlaces(path).ToString());
            Eq("load dark", "2", ThemeNum(Settings.LoadTheme(path)));
            Settings.Load(path, out places, out theme);
            Eq("Load reads both", "4 2", places + " " + ThemeNum(theme));
            True("save 6 and light", Settings.Save(path, 6, ThemeMode.Light));
            Eq("the file after a second save", "DecimalPlaces=6\r\nTheme=light\r\n", File.ReadAllText(path));
            Eq("load 6", "6", Settings.LoadDecimalPlaces(path).ToString());
            Eq("load light", "1", ThemeNum(Settings.LoadTheme(path)));
            True("save auto", Settings.Save(path, 3, ThemeMode.Auto));
            Eq("the file with the theme auto", "DecimalPlaces=3\r\nTheme=auto\r\n", File.ReadAllText(path));
            True("save an invalid value writes the default", Settings.Save(path, 12, ThemeMode.Dark));
            Eq("... which loads as the default", "2", Settings.LoadDecimalPlaces(path).ToString());
            Eq("... and keeps the theme", "2", ThemeNum(Settings.LoadTheme(path)));
            True("save a theme outside the enumeration", Settings.Save(path, 5, (ThemeMode)9));
            Eq("... is written as auto", "DecimalPlaces=5\r\nTheme=auto\r\n", File.ReadAllText(path));
            // Changing one setting never loses the other: read both, change one, save both.
            True("save 5 and dark", Settings.Save(path, 5, ThemeMode.Dark));
            Settings.Load(path, out places, out theme);
            True("change the places only", Settings.Save(path, 3, theme));
            Eq("... the theme survived", "DecimalPlaces=3\r\nTheme=dark\r\n", File.ReadAllText(path));
            Settings.Load(path, out places, out theme);
            True("change the theme only", Settings.Save(path, places, ThemeMode.Light));
            Eq("... the places survived", "DecimalPlaces=3\r\nTheme=light\r\n", File.ReadAllText(path));
            File.WriteAllText(path, "DecimalPlaces=3");
            Eq("a hand-written file", "3", Settings.LoadDecimalPlaces(path).ToString());
            Eq("... without a Theme line", "0", ThemeNum(Settings.LoadTheme(path)));
            File.WriteAllText(path, "theme = DARK\r\n; the places are missing\r\n");
            Settings.Load(path, out places, out theme);
            Eq("a hand-written file with only a theme", "2 2", places + " " + ThemeNum(theme));
            File.WriteAllText(path, "DecimalPlaces=3\r\nTheme=dark\r\nsome more text that goes\r\n");
            True("a longer file is cut when saved", Settings.Save(path, 5, ThemeMode.Light));
            Eq("... to the two settings", "DecimalPlaces=5\r\nTheme=light\r\n", File.ReadAllText(path));
            False("a folder whose parent does not exist is not an error, just not saved", Settings.Save(Path.Combine(dir, "nope", "deeper", "HourCalc.ini"), 3, ThemeMode.Auto));
            False("... and not made", Directory.Exists(Path.Combine(dir, "nope")));
            False("a null path is not saved", Settings.Save(null, 3, ThemeMode.Auto));
            False("an empty path is not saved", Settings.Save("", 3, ThemeMode.Auto));
            False("a path that is a folder is not saved", Settings.Save(dir, 3, ThemeMode.Auto));
            File.Delete(path);
            SettingsSaveTests(dir);
            Eq("nothing is left in the test folder (no temporary files)", "0", Directory.GetFiles(dir, "*.tmp").Length.ToString());
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
        }
    }
}
