// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Adversarial review of the native core: a second differential test, independent of
// tests\Differential.cs (nothing is shared; the oracle sources are compiled in again).
//
//   DifferentialReview.exe <hc_cli.exe> [--threads=N] [--only=<text>] [--scale=F] [--cultures=N] [--seed=N] [--state]
//   DifferentialReview.exe --cli "<command line>" [...]
//   --cli: the native program through a command line, for the Linux build: --cli "ssh.exe -T -o BatchMode=yes <host> <path of hc_cli>"
//          (then at most 6 threads unless --threads=N says otherwise; its files are reached through the fs.* commands of hc_cli)
//   --state: <review_state_cli.exe> instead, calculator suites only; compares the private fields too
//   --scale=F: the random suites run F of their cases; the finite (exhaustive) suites run every 1/F-th case, spread over
//              the whole enumeration; the suites that do not depend on the culture (Dec, settings) always run in full
//   --cultures=N: only the first N of the ten decimal separators (2 = nl-BE and en-US)
//
// What it adds over tests\Differential.cs:
//   - calculator sequences of up to 200 keys, four generators: mixed, a decimal places change after
//     every key, growth (repeated * / and =), dense Paste fuzz (random UTF-16, every kind of
//     white space, very long numbers, separator soup); other seeds;
//   - Paste over every UTF-16 code unit in six positions and four calculator states;
//   - a grid of System.Decimal values: 40 mantissas (the 96 bit limits, 2^k, 5^k, 10^k, ties)
//     times 18 scales, all pairs, plus the shapes acc*60/op and acc*op/60;
//   - TimeMath and converter functions on that grid with every number of places;
//   - converter fuzz (random UTF-16, long digit strings, the separators of the culture);
//   - Settings: OrdinalIgnoreCase over every UTF-16 code unit in every position of the key,
//     white space around the key and the value, files with odd endings;
//   - ten decimal separators: nl-BE, en-US, fa-IR, de-CH, five made-up ones, and the user's own.
// The suites tagged (info) are outside the contract of the core (text that the UI never passes in);
// their mismatches are listed but do not fail the run. (The one difference that the oleaut32 based Dec had, a division whose
// quotient has a 96 bit mantissa within 2^37 of 2^96, is gone with the own Dec; nothing is excused any more.)

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using HourCalc;

static class DifferentialReview
{
    static string _cliPath;
    static int _threads = 10;
    static string _only = "";
    static double _scale = 1.0;
    static int _cultureCount = 10;
    static ulong _seed = 4242424242UL;
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    static Cli[] _pool;

    [ThreadStatic] static int _cul;          // index of the culture the current worker runs

    // ---------------------------------------------------------------- escaping (same protocol as hc_cli.cpp)

    static string U(int cp) { return ((char)cp).ToString(); }

    static void EscTo(StringBuilder o, string s)
    {
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            switch (c)
            {
                case '\\': o.Append("\\\\"); break;
                case ' ': o.Append("\\s"); break;
                case '\n': o.Append("\\n"); break;
                case '\r': o.Append("\\r"); break;
                case '\t': o.Append("\\t"); break;
                case '|': o.Append("\\p"); break;
                default:
                    if (c < 0x20 || c > 0x7E) o.Append("\\u").Append(((int)c).ToString("X4", Inv));
                    else o.Append(c);
                    break;
            }
        }
    }

    static string Esc(string s)
    {
        var o = new StringBuilder(s.Length + 8);
        EscTo(o, s);
        return o.ToString();
    }

    static string Tok(string s) { return s.Length == 0 ? "\\e" : Esc(s); }

    static string Unesc(string s)
    {
        if (s == "\\e") return "";
        var o = new StringBuilder();
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (c != '\\' || i + 1 >= s.Length) { o.Append(c); continue; }
            char k = s[++i];
            switch (k)
            {
                case '\\': o.Append('\\'); break;
                case 's': o.Append(' '); break;
                case 'n': o.Append('\n'); break;
                case 'r': o.Append('\r'); break;
                case 't': o.Append('\t'); break;
                case 'p': o.Append('|'); break;
                case 'u':
                    {
                        int v;
                        if (i + 4 <= s.Length - 1 && int.TryParse(s.Substring(i + 1, 4), NumberStyles.HexNumber, Inv, out v)) { o.Append((char)v); i += 4; }
                        else o.Append("\\u");
                    }
                    break;
                default: o.Append('\\').Append(k); break;
            }
        }
        return o.ToString();
    }

    static string Pretty(string line)
    {
        if (line.StartsWith("!")) return line;
        var parts = line.Split(' ');
        var o = new StringBuilder();
        for (int i = 0; i < parts.Length; i++)
        {
            if (i > 0) o.Append("  ");
            string p = parts[i];
            if (p.IndexOf('|') >= 0)
            {
                var f = p.Split('|');
                for (int j = 0; j < f.Length; j++) { if (j > 0) o.Append(" | "); o.Append('<').Append(Unesc(f[j])).Append('>'); }
            }
            else if (i == 0) o.Append(p);
            else o.Append('<').Append(Unesc(p)).Append('>');
        }
        return o.ToString();
    }

    // ---------------------------------------------------------------- the native process

    sealed class Cli : IDisposable
    {
        readonly Process _p;
        readonly Stream _in;
        readonly StreamReader _out;

        public Cli(string path, string arguments)
        {
            var psi = new ProcessStartInfo(path, arguments)
            {
                UseShellExecute = false,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.ASCII
            };
            _p = Process.Start(psi);
            _in = _p.StandardInput.BaseStream;
            _out = new StreamReader(_p.StandardOutput.BaseStream, Encoding.ASCII, false, 1 << 16);
            if (arguments.Length > 0)
            {
                // Through a command line (ssh.exe ...) anything may be printed in front of the first answer: wait for "pong".
                byte[] ping = Encoding.ASCII.GetBytes("ping\n");
                _in.Write(ping, 0, ping.Length);
                _in.Flush();
                for (int i = 0; i < 50; i++)
                {
                    string line = _out.ReadLine();
                    if (line == null || line == "pong") break;
                }
            }
        }

        public string[] Batch(List<string> lines)
        {
            var sb = new StringBuilder();
            foreach (string l in lines) sb.Append(l).Append('\n');
            byte[] bytes = Encoding.ASCII.GetBytes(sb.ToString());
            Exception writeError = null;
            var writer = new Thread(() =>
            {
                try { _in.Write(bytes, 0, bytes.Length); _in.Flush(); }
                catch (Exception e) { writeError = e; }
            });
            writer.Start();
            var result = new string[lines.Count];
            for (int i = 0; i < result.Length; i++)
            {
                string r = _out.ReadLine();
                if (r == null)
                {
                    writer.Join();
                    string where = lines[Math.Min(i, lines.Count - 1)];
                    if (where.Length > 300) where = where.Substring(0, 300) + "...";
                    throw new InvalidOperationException("hc_cli.exe ended early (crash?) at request " + i + " of " + result.Length + ": " + where + (writeError != null ? " (" + writeError.Message + ")" : ""));
                }
                result[i] = r;
            }
            writer.Join();
            return result;
        }

        public string Query(string line) { return Batch(new List<string> { line })[0]; }

        public void Dispose()
        {
            try { _in.Close(); } catch (Exception) { }
            try { if (!_p.WaitForExit(5000)) _p.Kill(); } catch (Exception) { }
            _p.Dispose();
        }
    }

    // ---------------------------------------------------------------- where the native program runs

    // --cli "<command line>" starts the native program through a command line instead of as a file on this machine, for example
    //   --cli "C:\Windows\System32\OpenSSH\ssh.exe -T -o BatchMode=yes hourcalc-debian ~/hc-core-build/native/core/hc_cli"   (the Linux build).
    // That program does not share this process's file system, environment or user culture. The tests then reach its files through
    // the fs.* commands of hc_cli (N, NWrite, NRead ...), take the "user" decimal separator from its answer, and replace the checks
    // that only exist on Windows (a file locked by another program, the file attributes, %LOCALAPPDATA%) by their Linux
    // counterparts (a file without permissions, the permission bits, $XDG_CONFIG_HOME). Same Windows semantics otherwise.
    static string _cliCmd;
    static bool _linux;                  // the native program says it is the Linux build ("os" -> linux)
    static string _dir, _ndir;           // the folder of the settings checks: for this process, and for the native program

    static void SplitCommand(string cmd, out string file, out string args)
    {
        cmd = cmd.Trim();
        if (cmd.StartsWith("\""))
        {
            int end = cmd.IndexOf('"', 1);
            if (end < 0) end = cmd.Length - 1;
            file = cmd.Substring(1, end - 1);
            args = cmd.Substring(end + 1).Trim();
        }
        else
        {
            int sp = cmd.IndexOf(' ');
            file = sp < 0 ? cmd : cmd.Substring(0, sp);
            args = sp < 0 ? "" : cmd.Substring(sp + 1).Trim();
        }
    }

    static Cli StartCli()
    {
        if (_cliCmd == null) return new Cli(_cliPath, "");
        string file, args;
        SplitCommand(_cliCmd, out file, out args);
        return new Cli(file, args);
    }

    static string Hex(byte[] b)
    {
        return b.Length == 0 ? "\\e" : BitConverter.ToString(b).Replace("-", "").ToLowerInvariant();
    }

    static byte[] FromHex(string h)
    {
        if (h == "\\e") return new byte[0];
        var b = new byte[h.Length / 2];
        for (int i = 0; i < b.Length; i++) b[i] = byte.Parse(h.Substring(2 * i, 2), NumberStyles.HexNumber, Inv);
        return b;
    }

    // A path under _dir as the native program sees it.
    static string N(string p)
    {
        if (!_linux) return p;
        if (p.Length <= _dir.Length) return _ndir;
        return _ndir + "/" + p.Substring(_dir.Length + 1).Replace('\\', '/');
    }

    static void NWrite(string p, byte[] bytes)
    {
        if (!_linux) { File.WriteAllBytes(p, bytes); return; }
        string r = _pool[0].Query("fs.write " + Esc(N(p)) + " " + Hex(bytes));
        if (r != "1") throw new InvalidOperationException("fs.write failed for " + N(p) + ": " + r);
    }

    static void NWriteText(string p, string text) { NWrite(p, new UTF8Encoding(false).GetBytes(text)); }

    // The file for both sides: here for the reference, there for the native program (the same file when they share the machine).
    static void WriteBoth(string p, byte[] bytes)
    {
        File.WriteAllBytes(p, bytes);
        if (_linux) NWrite(p, bytes);
    }

    // The bytes of the native program's file, null when there is none.
    static byte[] NRead(string p)
    {
        if (!_linux) return File.Exists(p) ? File.ReadAllBytes(p) : null;
        string r = _pool[0].Query("fs.read " + Esc(N(p)));
        return r == "-" ? null : FromHex(r);
    }

    static string NBytes(string p)
    {
        byte[] b = NRead(p);
        return b == null ? "(none)" : BitConverter.ToString(b);
    }

    static bool NDirExists(string p)
    {
        return !_linux ? Directory.Exists(p) : _pool[0].Query("fs.isdir " + Esc(N(p))) == "1";
    }

    static void NDelete(string p)
    {
        if (!_linux) { if (File.Exists(p)) File.Delete(p); }
        else _pool[0].Query("fs.del " + Esc(N(p)));
    }

    // The temporary files that are left in the folder of the run, on both sides.
    static int TmpCount(string dir)
    {
        int n = Directory.GetFiles(dir, "*.tmp").Length;
        if (_linux) n += int.Parse(_pool[0].Query("fs.count " + Esc(_ndir) + " .tmp"), Inv);
        return n;
    }

    // The oracle reads a file that the native program wrote (and the other way round): a copy on this side, a copy on that side.
    static string LocalCopyOfNative(string p)
    {
        if (!_linux) return p;
        string copy = p + ".copy";
        byte[] b = NRead(p);
        File.WriteAllBytes(copy, b ?? new byte[0]);
        return copy;
    }

    static string NativeCopyOfLocal(string p)
    {
        if (_linux) NWrite(p, File.ReadAllBytes(p));
        return p;
    }

    // ---------------------------------------------------------------- cases

    struct Case
    {
        public string Line;
        public Func<string> Exp;
        public object Tag;
        public Case(string line, Func<string> exp) { Line = line; Exp = exp; Tag = null; }
    }

    static string Guard(Func<string> f)
    {
        try { return f(); }
        catch (OverflowException) { return "!OVERFLOW"; }
        catch (DivideByZeroException) { return "!DIVZERO"; }
        catch (FormatException) { return "!FORMAT"; }
        catch (Exception) { return "!EXC"; }
    }

    static string D(decimal m) { return m.ToString(Inv); }
    static string B(bool b) { return b ? "1" : "0"; }
    static string I(int n) { return n.ToString(Inv); }

    static Case TmHm(decimal m) { return new Case("tm.hm " + D(m), () => Tok(TimeMath.FormatHm(m))); }
    static Case TmDec(decimal m) { return new Case("tm.dec " + D(m), () => Tok(TimeMath.FormatDecimal(m))); }
    static Case TmDecP(decimal m, int p) { return new Case("tm.dec " + D(m) + " " + p, () => Tok(TimeMath.FormatDecimal(m, p))); }
    static Case TmWords(decimal m) { return new Case("tm.words " + D(m), () => Tok(TimeMath.FormatWords(m))); }
    static Case TmFactor(decimal m) { return new Case("tm.factor " + D(m), () => Tok(TimeMath.FormatFactor(m))); }
    static Case TmRoundMin(decimal m) { return new Case("tm.roundmin " + D(m), () => D(TimeMath.RoundMinutes(m))); }
    static Case TmRoundH(decimal m) { return new Case("tm.roundh " + D(m), () => D(TimeMath.RoundHours(m))); }
    static Case TmRoundHP(decimal m, int p) { return new Case("tm.roundh " + D(m) + " " + p, () => D(TimeMath.RoundHours(m, p))); }
    static Case TmParse(string entry)
    {
        return new Case("tm.parse " + Tok(entry), () =>
        {
            decimal m;
            bool ok = TimeMath.TryParseEntry(entry, out m);
            return B(ok) + " " + D(m);
        });
    }
    static Case CvFormat(int f, decimal m) { return new Case("cv.format " + f + " " + D(m), () => Tok(Converter.Format(f, m))); }
    static Case CvFormatP(int f, decimal m, int p) { return new Case("cv.format " + f + " " + D(m) + " " + p, () => Tok(Converter.Format(f, m, p))); }
    static Case CvGetMin(int f, string t)
    {
        return new Case("cv.getmin " + f + " " + Tok(t), () =>
        {
            decimal m;
            bool ok = Converter.TryGetMinutes(f, t, out m);
            return B(ok) + " " + D(m);
        });
    }
    static Case CvNormP(int f, string t, int p) { return new Case("cv.norm " + f + " " + Tok(t) + " " + p, () => Tok(Converter.Normalize(f, t, p))); }
    static Case CvComputeP(int f, string t, int p)
    {
        return new Case("cv.compute " + f + " " + Tok(t) + " " + p, () =>
        {
            string o, c;
            Converter.Compute(f, t, p, out o, out c);
            return Tok(o) + " " + Tok(c);
        });
    }
    static Case CvConvert(int f, string t, int p)
    {
        return new Case("cv.convert " + f + " " + Tok(t) + " " + p, () =>
        {
            string n, o, c;
            Converter.Convert(f, t, p, out n, out o, out c);
            return Tok(n) + " " + Tok(o) + " " + Tok(c);
        });
    }

    static Case CvAll(string t, long k)
    {
        return new Case("cv.all " + Tok(t) + " " + k, () => CvAllExpected(t, (int)k));
    }

    static string CvAllExpected(string t, int k)
    {
        int p = 2 + k % 5;
        bool plain = (k / 5) % 4 == 0;
        var sb = new StringBuilder();
        for (int f = 0; f < 2; f++)
        {
            if (f != 0) sb.Append('|');
            string ff = Converter.FilterField(f, t);
            decimal m;
            bool ok = Converter.TryGetMinutes(f, ff, out m);
            string norm, other, caption, cnorm, cother, ccaption;
            if (plain)
            {
                norm = Converter.Normalize(f, t);
                Converter.Compute(f, t, out other, out caption);
            }
            else
            {
                norm = Converter.Normalize(f, t, p);
                Converter.Compute(f, t, p, out other, out caption);
            }
            Converter.Convert(f, t, p, out cnorm, out cother, out ccaption);
            EscTo(sb, Converter.Filter(f, t)); sb.Append('|');
            EscTo(sb, ff); sb.Append('|');
            sb.Append(ok ? '1' : '0').Append('|');
            sb.Append(D(m)).Append('|');
            EscTo(sb, norm); sb.Append('|');
            EscTo(sb, other); sb.Append('|');
            EscTo(sb, caption); sb.Append('|');
            EscTo(sb, cnorm); sb.Append('|');
            EscTo(sb, cother); sb.Append('|');
            EscTo(sb, ccaption);
        }
        return sb.ToString();
    }

    // --- CalcEngine
    struct Op
    {
        public char K;       // 'k' key, 'p' paste, 'd' decimal places
        public string A;
        public Op(char k, string a) { K = k; A = a; }
        public string Token() { return K == 'd' ? "d:" + A : K + ":" + Esc(A); }
    }

    sealed class Seq
    {
        public int Places;               // -1: the constructor without places
        public List<Op> Ops;
    }

    static readonly string[] AllKeys =
    {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ",", ".", "u", "U", "h", "H",
        "+", "-", "*", "/", "=", "CE", "C", "BACK", "Back", "BACKSPACE", "ce", "c", "x", "", "Ce", "bAck"
    };

    // --state: the private fields of the reference, read by reflection (the native side is review_state_cli.exe).
    static bool _state;
    static System.Reflection.FieldInfo FieldOf(string name)
    {
        return typeof(CalcEngine).GetField(name, System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
    }
    static readonly System.Reflection.FieldInfo FAcc = FieldOf("_acc"), FHasAcc = FieldOf("_hasAcc"), FOp = FieldOf("_op"), FLastOp = FieldOf("_lastOp"),
        FLastOperand = FieldOf("_lastOperand"), FHasLast = FieldOf("_hasLast"), FJustEq = FieldOf("_justEq"), FEntry = FieldOf("_entry"),
        FEntryActive = FieldOf("_entryActive"), FError = FieldOf("_error");

    static void AppendState(StringBuilder sb, CalcEngine e)
    {
        sb.Append('|'); EscTo(sb, ((decimal)FAcc.GetValue(e)).ToString(Inv));
        sb.Append('|').Append((bool)FHasAcc.GetValue(e) ? '1' : '0');
        sb.Append('|').Append(((int)(char)FOp.GetValue(e)).ToString(Inv));
        sb.Append('|').Append(((int)(char)FLastOp.GetValue(e)).ToString(Inv));
        sb.Append('|'); EscTo(sb, ((decimal)FLastOperand.GetValue(e)).ToString(Inv));
        sb.Append('|').Append((bool)FHasLast.GetValue(e) ? '1' : '0');
        sb.Append('|').Append((bool)FJustEq.GetValue(e) ? '1' : '0');
        string entry = (string)FEntry.GetValue(e);
        sb.Append('|'); if (entry.Length == 0) sb.Append("\\e"); else EscTo(sb, entry);
        sb.Append('|').Append((bool)FEntryActive.GetValue(e) ? '1' : '0');
        string err = (string)FError.GetValue(e);
        sb.Append('|'); if (err == null) sb.Append('-'); else EscTo(sb, err);
    }

    static string CeLine(Seq s)
    {
        var sb = new StringBuilder(_state ? "ce.state " : "ce.run ");
        sb.Append(s.Places < 0 ? "-" : I(s.Places));
        foreach (Op op in s.Ops) sb.Append(' ').Append(op.Token());
        return sb.ToString();
    }

    static string CeEval(Seq s)
    {
        var e = s.Places < 0 ? new CalcEngine() : new CalcEngine(s.Places);
        var sb = new StringBuilder();
        foreach (Op op in s.Ops)
        {
            string paste = "-";
            switch (op.K)
            {
                case 'k': e.Press(op.A); break;
                case 'p': paste = e.Paste(op.A) ? "1" : "0"; break;
                default: e.DecimalPlaces = int.Parse(op.A, Inv); break;
            }
            EscTo(sb, e.Expression); sb.Append('|');
            EscTo(sb, e.MainText); sb.Append('|');
            EscTo(sb, e.DecimalText); sb.Append('|');
            EscTo(sb, e.Caption); sb.Append('|');
            sb.Append(e.IsError ? '1' : '0').Append('|');
            sb.Append(e.HasEntry ? '1' : '0').Append('|');
            sb.Append(e.DecimalPlaces.ToString(Inv)).Append('|');
            sb.Append(paste);
            if (_state) AppendState(sb, e);
            sb.Append(' ');
        }
        EscTo(sb, e.CopyMainText); sb.Append('|');
        EscTo(sb, e.CopyDecimalText); sb.Append('|');
        foreach (string k in AllKeys) sb.Append(e.IsEnabled(k) ? '1' : '0');
        return sb.ToString();
    }

    static Case CeCase(Seq s)
    {
        var c = new Case(CeLine(s), () => CeEval(s));
        c.Tag = s;
        return c;
    }

    static Seq KeysSeq(int places, params string[] keys)
    {
        var s = new Seq { Places = places, Ops = new List<Op>() };
        foreach (string k in keys) s.Ops.Add(new Op('k', k));
        return s;
    }

    // --- Dec
    static IEnumerable<Case> DecPair(decimal a, decimal b)
    {
        string sa = D(a), sb = D(b);
        yield return new Case("dec.add " + sa + " " + sb, () => D(a + b));
        yield return new Case("dec.sub " + sa + " " + sb, () => D(a - b));
        yield return new Case("dec.mul " + sa + " " + sb, () => D(a * b));
        yield return new Case("dec.div " + sa + " " + sb, () => D(a / b));
        yield return new Case("dec.cmp " + sa + " " + sb, () => B(a == b) + B(a != b) + B(a < b) + B(a <= b) + B(a > b) + B(a >= b));
    }

    static IEnumerable<Case> DecDivOnly(decimal a, decimal b)
    {
        yield return new Case("dec.div " + D(a) + " " + D(b), () => D(a / b));
    }

    static IEnumerable<Case> DecUnary(decimal a)
    {
        string sa = D(a);
        for (int r = 0; r <= 28; r++)
        {
            int rr = r;
            yield return new Case("dec.round " + sa + " " + rr, () => D(Math.Round(a, rr, MidpointRounding.AwayFromZero)));
            yield return new Case("dec.rounde " + sa + " " + rr, () => D(Math.Round(a, rr)));
        }
        yield return new Case("dec.trunc " + sa, () => D(Math.Truncate(a)));
        yield return new Case("dec.abs " + sa, () => D(Math.Abs(a)));
        yield return new Case("dec.neg " + sa, () => D(-a));
        yield return new Case("dec.int64 " + sa, () => ((long)a).ToString(Inv));
        yield return new Case("dec.parse " + sa, () => D(decimal.Parse(sa, Inv)));
        yield return new Case("dec.iszero " + sa, () => B(a == 0m));
        yield return new Case("dec.isneg " + sa, () => B(a < 0m));
    }

    // ---------------------------------------------------------------- random numbers

    sealed class Rng
    {
        ulong _s;
        public Rng(ulong seed) { _s = seed; }
        public ulong Next()
        {
            _s += 0x9E3779B97F4A7C15UL;
            ulong z = _s;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;
            return z ^ (z >> 31);
        }
        public int Int(int n) { return (int)(Next() % (ulong)n); }
        public bool Chance(int percent) { return Int(100) < percent; }
        public T Pick<T>(T[] a) { return a[Int(a.Length)]; }
    }

    static Rng RngFor(string tag, long id)
    {
        ulong h = _seed;
        foreach (char ch in tag) h = h * 1099511628211UL + ch;
        return new Rng(h * 1000003UL + (ulong)id * 7919UL + (ulong)(_cul + 1) * 104729UL + 12345UL);
    }

    // ---------------------------------------------------------------- suites

    sealed class Suite
    {
        public string Name;
        public long Count;
        public Func<long, IEnumerable<Case>> Gen;
        public double Share = 1.0;          // fraction of Count for the cultures after the first two
        public bool Once;                   // culture independent: the first culture only
        public bool Info;                   // outside the contract: mismatches listed, not counted
#pragma warning disable 0649
        public bool Light;                  // only the first two cultures (no suite sets it at the moment)
#pragma warning restore 0649
        public bool Exhaustive;             // ids are a finite enumeration: never run past Count
        public bool NoThrow;                // no input of this suite may make the core throw (the UI would crash): '!' answers count as mismatches
    }

    sealed class Sample
    {
        public string Line, Expected, Actual;
        public object Tag;
    }

    sealed class Stats
    {
        public string Name;
        public long Cases;
        public long Mismatches;
        public readonly List<Sample> Samples = new List<Sample>();
    }

    sealed class Cul
    {
        public string Name;
        public string Sep;           // what the native core is told ("" = no override: the core reads the user's own)
        public CultureInfo Ci;
    }

    const int MaxSamples = 12;
    static long _totalCases;
    static long _totalMismatches;
    static long _infoMismatches;
    static readonly object PrintLock = new object();

    static void Worker(Suite s, Stats st, Cul cul, int culIndex, long count, long stride, long[] next, long block, int index)
    {
        Thread.CurrentThread.CurrentCulture = cul.Ci;
        Thread.CurrentThread.CurrentUICulture = CultureInfo.InvariantCulture;
        _cul = culIndex;
        Cli cli = _pool[index];
        cli.Query("sep " + Tok(cul.Sep));
        var cases = new List<Case>(8192);
        var lines = new List<string>(8192);
        Action flush = () =>
        {
            if (cases.Count == 0) return;
            string[] got = cli.Batch(lines);
            long bad = 0;
            for (int i = 0; i < cases.Count; i++)
            {
                string exp = Guard(cases[i].Exp);
                bool threw = s.NoThrow && (got[i].Length > 0 && got[i][0] == '!');
                if (exp != got[i] || threw)
                {
                    bad++;
                    lock (st)
                    {
                        if (st.Samples.Count < MaxSamples)
                            st.Samples.Add(new Sample { Line = cases[i].Line, Expected = exp, Actual = got[i] + (threw ? "   <-- the core threw" : ""), Tag = cases[i].Tag });
                    }
                }
            }
            Interlocked.Add(ref st.Cases, cases.Count);
            if (bad > 0) Interlocked.Add(ref st.Mismatches, bad);
            cases.Clear();
            lines.Clear();
        };
        for (;;)
        {
            long from = Interlocked.Add(ref next[0], block) - block;
            if (from >= count) break;
            long to = Math.Min(count, from + block);
            for (long id = from; id < to; id++)
            {
                long gid = stride == 1 ? id : Math.Min(Math.Max(1, s.Count) - 1, id * stride + culIndex % stride);
                foreach (Case c in s.Gen(gid))
                {
                    cases.Add(c);
                    lines.Add(c.Line);
                }
                if (cases.Count >= 2048) flush();
            }
        }
        flush();
    }

    static void RunSuite(Suite s, Cul cul, int culIndex)
    {
        long stride = culIndex < 2 ? 1 : Math.Max(1, (long)Math.Round(1.0 / s.Share));
        double scale = _scale;
        if (s.Once) scale = 1.0;
        else if (s.Exhaustive && scale < 1.0)
        {
            stride = Math.Max(stride, (long)Math.Round(1.0 / scale));
            scale = 1.0;
        }
        long count = Math.Max(1, (long)(s.Count / stride * scale));
        if (s.Exhaustive || s.Once) count = Math.Min(count, Math.Max(1, s.Count / stride));
        var st = new Stats { Name = s.Name + " [" + cul.Name + (cul.Sep.Length == 0 ? " user" : " '" + cul.Sep + "'") + "]" };
        var sw = Stopwatch.StartNew();
        long block = Math.Max(1, Math.Min(256, count / (_threads * 8)));
        var next = new long[1];
        var threads = new Thread[(int)Math.Min(_threads, Math.Max(1, count / 32))];
        Exception failure = null;
        for (int t = 0; t < threads.Length; t++)
        {
            int index = t;
            threads[t] = new Thread(() =>
            {
                try { Worker(s, st, cul, culIndex, count, stride, next, block, index); }
                catch (Exception e) { failure = e; }
            });
            threads[t].Start();
        }
        foreach (Thread t in threads) t.Join();
        if (failure != null) throw new InvalidOperationException("suite " + st.Name + ": " + failure.Message, failure);
        if (s.Info) Interlocked.Add(ref _infoMismatches, st.Mismatches);
        else
        {
            Interlocked.Add(ref _totalCases, st.Cases);
            Interlocked.Add(ref _totalMismatches, st.Mismatches);
        }
        Console.WriteLine(string.Format(Inv, "{0,-58} {1,12:N0} checks {2,6} mismatches {3,7:N1} s{4}", st.Name, st.Cases, st.Mismatches, sw.Elapsed.TotalSeconds, s.Info ? "  (info)" : ""));
        if (st.Mismatches > 0) PrintSamples(st, cul);
    }

    // ---------------------------------------------------------------- mismatch report

    static bool Differs(Seq s, Cli cli) { return Guard(() => CeEval(s)) != cli.Query(CeLine(s)); }

    static Seq Minimize(Seq s, Cli cli)
    {
        var cur = new Seq { Places = s.Places, Ops = new List<Op>(s.Ops) };
        if (!Differs(cur, cli)) return cur;
        string[] exp = Guard(() => CeEval(cur)).Split(' ');
        string[] got = cli.Query(CeLine(cur)).Split(' ');
        for (int i = 0; i < Math.Min(exp.Length, got.Length); i++)
        {
            if (exp[i] != got[i])
            {
                if (i < cur.Ops.Count) cur.Ops.RemoveRange(i + 1, cur.Ops.Count - i - 1);
                break;
            }
        }
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 6)
        {
            changed = false;
            for (int i = 0; i < cur.Ops.Count; i++)
            {
                var trial = new Seq { Places = cur.Places, Ops = new List<Op>(cur.Ops) };
                trial.Ops.RemoveAt(i);
                if (trial.Ops.Count > 0 && Differs(trial, cli)) { cur = trial; changed = true; i--; }
            }
        }
        if (cur.Places >= 0)
        {
            var trial = new Seq { Places = -1, Ops = cur.Ops };
            if (Differs(trial, cli)) cur = trial;
        }
        return cur;
    }

    static string DescribeSeq(Seq s)
    {
        var sb = new StringBuilder();
        sb.Append(s.Places < 0 ? "new CalcEngine()" : "new CalcEngine(" + s.Places + ")");
        foreach (Op op in s.Ops)
        {
            sb.Append(op.K == 'k' ? " Press(" : op.K == 'p' ? " Paste(" : " DecimalPlaces=");
            sb.Append(op.K == 'd' ? op.A : "\"" + Esc(op.A) + "\"");
            if (op.K != 'd') sb.Append(")");
        }
        return sb.ToString();
    }

    static string Clip(string s) { return s.Length <= 500 ? s : s.Substring(0, 500) + " ..."; }

    static int FirstDiff(string exp, string got, out string e, out string g)
    {
        string[] a = exp.Split(' ');
        string[] b = got.Split(' ');
        for (int i = 0; i < Math.Max(a.Length, b.Length); i++)
        {
            e = i < a.Length ? a[i] : "(none)";
            g = i < b.Length ? b[i] : "(none)";
            if (e != g) return i;
        }
        e = g = "";
        return -1;
    }

    static void PrintSamples(Stats st, Cul cul)
    {
        lock (PrintLock)
        {
            Cli cli = _pool[0];
            Thread.CurrentThread.CurrentCulture = cul.Ci;
            cli.Query("sep " + Tok(cul.Sep));
            int n = 0;
            foreach (Sample m in st.Samples)
            {
                if (++n > 8) break;
                Seq seq = m.Tag as Seq;
                if (seq == null)
                {
                    Console.WriteLine("  MISMATCH {0}", Clip(Pretty(m.Line)));
                    Console.WriteLine("    oracle: {0}", Clip(Pretty(m.Expected)));
                    Console.WriteLine("    native: {0}", Clip(Pretty(m.Actual)));
                    continue;
                }
                Seq min = Minimize(seq, cli);
                string exp = Guard(() => CeEval(min)), got = cli.Query(CeLine(min));
                string e, g;
                int step = FirstDiff(exp, got, out e, out g);
                Console.WriteLine("  MISMATCH calculator sequence of {0} ops, minimal reproduction of {1} ops:", seq.Ops.Count, min.Ops.Count);
                Console.WriteLine("    {0}", Clip(DescribeSeq(min)));
                Console.WriteLine("    request: {0}", Clip(CeLine(min)));
                Console.WriteLine("    first difference after op {0}{1}", step + 1, step >= 0 && step < min.Ops.Count ? " (" + min.Ops[step].Token() + ")" : " (the final record)");
                Console.WriteLine("    record = expression | left | right | caption | error | entry | places | paste result");
                Console.WriteLine("    oracle: {0}", Clip(Pretty(e)));
                Console.WriteLine("    native: {0}", Clip(Pretty(g)));
            }
            if (st.Mismatches > n - 1) Console.WriteLine(string.Format(Inv, "  ... {0} mismatches in all", st.Mismatches));
        }
    }

    // ---------------------------------------------------------------- calculator generators

    static readonly string[] Digits = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    static readonly string[] SepKeys = { ":", ",", ".", "u", "U", "h", "H" };
    static readonly string[] OpKeys = { "+", "-", "*", "/" };
    static readonly string[] GrowthOps = { "*", "/", "*", "/", "*", "/", "+", "-" };
    static readonly string[] CtlKeys = { "CE", "C", "BACK", "Back", "BACKSPACE", "ce", "c" };
    static readonly string[] FullKeys =
    {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ",", ".", "u", "U", "h", "H",
        "+", "-", "*", "/", "=", "CE", "C", "BACK", "Back", "BACKSPACE", "ce", "c", "x", ""
    };
    static string[] _oddKeys;
    static string[] OddKeys
    {
        get
        {
            if (_oddKeys == null)
                _oddKeys = new[] { "x", "", "12", "Ce", "cE", "++", "==", "BACK ", " BACK", "back", "Backspace", "backspace", "bAck", "C ", "+ ", "0 ", "00", U(0x0663), U(0xFF13), ";", "e", "E", "%", "\t", "\0", "\n", "=:", "-:", "..", "CEC", "Cc", "BACKSPACE ", U(0x2212), U(0xFF0B), U(0x00D7), U(0x00F7), "\\", "|" };
            return _oddKeys;
        }
    }

    static readonly string[] BigNums =
    {
        "9999999", "9999999:99", "9999999:59", "9999999:60", "9999999,999999", "9999999,5", "5999999:59", "5000000:30", "5000000", "6000000",
        "1000000", "999999", "1234567", "0,000001", "0,00001", "0:01", "1", "6", "60", "0:59", "99:99", "3", "7", "11", "13", "0,5", "1,5",
        "2", "0", "00", "000000", "0000000", "0:00", "0,0", "7:30", "8:20", "0,333333", "0,666667", "0,1", "0,7", "8,33", "1:20", "0,999999",
        "166666", "3333333", "9", "99", "999", "4:00", "0:30", "0,25", "0,125", "0,0625", "0,03125"
    };

    static void TypeNumber(Rng r, List<Op> ops, bool big)
    {
        if (r.Chance(big ? 75 : 45))
        {
            foreach (char c in r.Pick(BigNums)) ops.Add(new Op('k', c.ToString()));
            return;
        }
        int before = r.Int(9);
        bool nines = r.Chance(30);
        for (int i = 0; i < before; i++) ops.Add(new Op('k', nines ? "9" : r.Pick(Digits)));
        if (r.Chance(50))
        {
            ops.Add(new Op('k', r.Pick(SepKeys)));
            int after = r.Int(9);
            for (int i = 0; i < after; i++) ops.Add(new Op('k', nines && r.Chance(70) ? "9" : r.Pick(Digits)));
        }
    }

    // ---- Paste fuzz

    static readonly int[] WhiteCodes =
    {
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0x85, 0xA0, 0x1680, 0x180E, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
        0x2008, 0x2009, 0x200A, 0x200B, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000, 0xFEFF, 0x00, 0x1C, 0x1D, 0x1E, 0x1F, 0x0B, 0x061C, 0x200E, 0x200F
    };

    static readonly string[] PastePool =
    {
        "08:30", "1:15", "1.5", "1,5", "8u30", "8", "  1:15 ", "\t7\n", ":30", ",5", ".5", ":", ",", "7:", "0:90", "9999999:99",
        "9999999:60", "9999999,999999", "0,000001", "123456", "1234567", "12:3", "abc", "", "-1:30", "12345678", "1,2,3", "7:300",
        "1:2:3", "7,1234567", "1.2.3", "1 2", "7h", "H30", "u", "5.", "5,", "0,0", "00:00", "1e5", "+5", "7:30\0", "\0", "0000000", "0000000:00",
        "7,0000001", "99999999", "0.", ".0", "0:", ":0", "h", "U", "uu", "7hh", "8.5.", "8,5,", ".,", ",.", "::", "7 :30", "7: 30", "1\n2", "\r\n5\r\n"
    };

    static string Pad(Rng r)
    {
        var sb = new StringBuilder();
        int n = r.Int(4);
        for (int i = 0; i < n; i++) sb.Append((char)r.Pick(WhiteCodes));
        return sb.ToString();
    }

    static string DigitRun(Rng r, int max)
    {
        int n = r.Int(max + 1);
        var sb = new StringBuilder();
        for (int i = 0; i < n; i++) sb.Append((char)('0' + r.Int(10)));
        return sb.ToString();
    }

    static readonly string[] SepForms = { ":", ",", ".", "u", "U", "h", "H", "", "::", ",,", ":,", ".,", "h.", "u:" };

    static string RandomUtf16(Rng r, int maxLen)
    {
        int n = 1 + r.Int(maxLen);
        var sb = new StringBuilder();
        for (int i = 0; i < n; i++)
        {
            int k = r.Int(10);
            if (k < 4) sb.Append((char)('0' + r.Int(10)));
            else if (k < 5) sb.Append(r.Pick(new[] { ':', ',', '.', 'u', 'U', 'h', 'H', ' ' }));
            else if (k < 6) sb.Append((char)r.Pick(WhiteCodes));
            else if (k < 7) sb.Append((char)(0xD800 + r.Int(0x800)));            // surrogates, lone or paired by luck
            else if (k < 8) sb.Append((char)(0x0660 + r.Int(10)));               // Arabic-Indic digits
            else if (k < 9) sb.Append((char)(0xFF10 + r.Int(10)));               // full width digits
            else sb.Append((char)r.Int(0x10000));
        }
        return sb.ToString();
    }

    static string FuzzPaste(Rng r)
    {
        switch (r.Int(10))
        {
            case 0:
            case 1: return r.Pick(PastePool);
            case 2:
            case 3:
            case 4:
                {
                    string t = Pad(r) + DigitRun(r, 9) + r.Pick(SepForms) + DigitRun(r, 9);
                    if (r.Chance(15)) t += r.Pick(SepForms) + DigitRun(r, 4);
                    return t + Pad(r);
                }
            case 5:
            case 6: return RandomUtf16(r, 40);
            case 7:
                {
                    var sb = new StringBuilder();
                    int n = 8 + r.Int(90);
                    for (int i = 0; i < n; i++) sb.Append((char)('0' + r.Int(10)));
                    if (r.Chance(50)) sb.Insert(r.Int(sb.Length + 1), r.Pick(SepForms));
                    return sb.ToString();
                }
            case 8:
                {
                    const string soup = "0123456789:,.uUhH 05:,.:,.";
                    var sb = new StringBuilder();
                    int n = 1 + r.Int(16);
                    for (int i = 0; i < n; i++) sb.Append(soup[r.Int(soup.Length)]);
                    return sb.ToString();
                }
            default:
                {
                    string t = Pad(r) + DigitRun(r, 8) + r.Pick(SepForms) + DigitRun(r, 8);
                    if (t.Length == 0) return t;
                    int pos = r.Int(t.Length);
                    switch (r.Int(3))
                    {
                        case 0: return t.Remove(pos, 1);
                        case 1: return t.Insert(pos, ((char)r.Int(0x10000)).ToString());
                        default: return t.Substring(0, pos) + ((char)r.Int(0x3000)).ToString() + t.Substring(pos + 1);
                    }
                }
        }
    }

    // modes: 0 mixed, 1 decimal places change after every key, 2 growth, 3 Paste dense
    static readonly int[][] ModeWeights =
    {
        //          digit sep op  eq  ctl num paste d  odd full
        new[] {     26,   8, 16, 10,  5, 15,  8,    5, 3,  4 },
        new[] {     26,   8, 16, 10,  5, 15,  8,    5, 3,  4 },
        new[] {      8,   3, 22, 30,  3, 25,  3,    2, 1,  3 },
        new[] {     10,   5, 10,  8,  5,  5, 50,    2, 2,  3 }
    };

    static void PickAction(Rng r, int mode, List<Op> ops)
    {
        int[] w = ModeWeights[mode];
        int c = r.Int(100);
        int idx = 0;
        while (idx < w.Length - 1 && c >= w[idx]) { c -= w[idx]; idx++; }
        switch (idx)
        {
            case 0: ops.Add(new Op('k', r.Pick(Digits))); break;
            case 1: ops.Add(new Op('k', r.Pick(SepKeys))); break;
            case 2: ops.Add(new Op('k', mode == 2 ? r.Pick(GrowthOps) : r.Pick(OpKeys))); break;
            case 3:
                {
                    int reps = mode == 2 && r.Chance(40) ? 1 + r.Int(6) : 1;
                    for (int i = 0; i < reps; i++) ops.Add(new Op('k', "="));
                    break;
                }
            case 4: ops.Add(new Op('k', r.Pick(CtlKeys))); break;
            case 5: TypeNumber(r, ops, mode == 2); break;
            case 6: ops.Add(new Op('p', FuzzPaste(r))); break;
            case 7: ops.Add(new Op('d', RandomPlaces(r))); break;
            case 8: ops.Add(new Op('k', r.Pick(OddKeys))); break;
            default: ops.Add(new Op('k', r.Pick(FullKeys))); break;
        }
    }

    static string RandomPlaces(Rng r)
    {
        if (r.Chance(80)) return I(2 + r.Int(5));
        return I(r.Pick(new[] { 0, 1, 7, -1, 100, 999999999, -999999999, 8, 9, 10, 1000 }));
    }

    static Seq GenSeq(Rng r, int mode, int maxKeys)
    {
        var s = new Seq { Ops = new List<Op>() };
        int pl = r.Int(100);
        s.Places = pl < 40 ? -1 : pl < 92 ? 2 + r.Int(5) : r.Pick(new[] { 0, 1, 7, -3, 100, 999999999, -999999999 });
        int target = r.Chance(35) ? Math.Max(1, maxKeys - r.Int(10)) : 1 + r.Int(maxKeys);
        int keys = 0;
        while (keys < target)
        {
            int before = s.Ops.Count;
            PickAction(r, mode, s.Ops);
            if (mode == 1)
            {
                // a places change after every key, also inside a typed number
                var added = s.Ops.GetRange(before, s.Ops.Count - before);
                s.Ops.RemoveRange(before, added.Count);
                foreach (Op o in added)
                {
                    s.Ops.Add(o);
                    if (o.K != 'd') s.Ops.Add(new Op('d', RandomPlaces(r)));
                }
            }
            for (int i = before; i < s.Ops.Count; i++) if (s.Ops[i].K != 'd') keys++;
        }
        return s;
    }

    static Suite CalcRandom(string name, int mode, int maxKeys, long count, double share)
    {
        return new Suite
        {
            Name = name,
            NoThrow = true,
            Count = count,
            Share = share,
            Gen = id =>
            {
                var r = RngFor("calc" + mode, id);
                return new[] { CeCase(GenSeq(r, mode, maxKeys)) };
            }
        };
    }

    // Paste of every UTF-16 code unit in six positions and four calculator states.
    static Suite PasteBmp()
    {
        const int Forms = 6, States = 4;
        return new Suite
        {
            Name = "calculator Paste, every code unit x 6 forms x 4 states",
            NoThrow = true,
            Exhaustive = true,
            Count = 65536L * Forms * States,
            Share = 0.1,
            Gen = id =>
            {
                int c = (int)(id % 65536);
                int form = (int)((id / 65536) % Forms);
                int state = (int)(id / 65536 / Forms);
                string ch = ((char)c).ToString();
                string text;
                switch (form)
                {
                    case 0: text = ch; break;
                    case 1: text = ch + "5"; break;
                    case 2: text = "5" + ch; break;
                    case 3: text = "5" + ch + "5"; break;
                    case 4: text = "1" + ch + "30"; break;
                    default: text = ch + "1:30" + ch; break;
                }
                var s = new Seq { Places = -1, Ops = new List<Op>() };
                switch (state)
                {
                    case 1: foreach (string k in new[] { "7", ":", "3", "0", "+", "1", "=" }) s.Ops.Add(new Op('k', k)); break;
                    case 2: foreach (string k in new[] { "7", "/", "0", "=" }) s.Ops.Add(new Op('k', k)); break;
                    case 3: foreach (string k in new[] { "1", "+", "7", ":", "3" }) s.Ops.Add(new Op('k', k)); break;
                    default: break;
                }
                s.Ops.Add(new Op('p', text));
                s.Ops.Add(new Op('k', "="));
                s.Ops.Add(new Op('k', "+"));
                s.Ops.Add(new Op('k', "1"));
                s.Ops.Add(new Op('k', "="));
                return new[] { CeCase(s) };
            }
        };
    }

    // Exhaustive key sequences over a small key set at a depth the reference test did not reach.
    static Suite CalcDeep(string name, string[] keys, int maxLen, double share)
    {
        long total = 0, count = 1;
        for (int len = 1; len <= maxLen; len++) { count *= keys.Length; total += count; }
        return new Suite
        {
            Name = name,
            NoThrow = true,
            Exhaustive = true,
            Count = total,
            Share = share,
            Gen = id =>
            {
                int n = keys.Length;
                long i = id;
                for (int len = 1; len <= maxLen; len++)
                {
                    long cnt = 1;
                    for (int k = 0; k < len; k++) cnt *= n;
                    if (i < cnt)
                    {
                        var ks = new string[len];
                        long x = i;
                        for (int k = len - 1; k >= 0; k--) { ks[k] = keys[(int)(x % n)]; x /= n; }
                        int[] pl = { -1, 2, 3, 4, 5, 6 };
                        return new[] { CeCase(KeysSeq(pl[id % pl.Length], ks)) };
                    }
                    i -= cnt;
                }
                return new Case[0];
            }
        };
    }

    // Paste and keys, exhaustive: [paste] key key key.
    static Suite PasteChains()
    {
        string[] pastes = { "7:30", "1,5", "0:90", "9999999:99", "9999999,999999", "0,000001", ":", ",", "7:", "5000000", "5999999:59", "00:00", "u", "abc", " 1:15 " };
        string[] keys = { "+", "-", "*", "/", "=", "1", "0", ":", ",", "BACK", "CE" };
        int depth = 4;
        long per = 1;
        for (int i = 0; i < depth; i++) per *= keys.Length;
        return new Suite
        {
            Name = "calculator Paste chains (14 pastes x 11 keys^4, and paste in the middle)",
            NoThrow = true,
            Exhaustive = true,
            Count = pastes.Length * per * 2,
            Share = 0.2,
            Gen = id =>
            {
                bool mid = id >= pastes.Length * per;
                long x = mid ? id - pastes.Length * per : id;
                string p = pastes[(int)(x / per)];
                long rest = x % per;
                var ks = new string[depth];
                for (int k = depth - 1; k >= 0; k--) { ks[k] = keys[(int)(rest % keys.Length)]; rest /= keys.Length; }
                var s = new Seq { Places = (int)(id % 7) - 1, Ops = new List<Op>() };
                if (mid)
                {
                    s.Ops.Add(new Op('k', ks[0])); s.Ops.Add(new Op('k', ks[1]));
                    s.Ops.Add(new Op('p', p));
                    s.Ops.Add(new Op('k', ks[2])); s.Ops.Add(new Op('k', ks[3]));
                    s.Ops.Add(new Op('p', p));
                }
                else
                {
                    s.Ops.Add(new Op('p', p));
                    foreach (string k in ks) s.Ops.Add(new Op('k', k));
                    s.Ops.Add(new Op('p', "1:00"));
                }
                return new[] { CeCase(s) };
            }
        };
    }

    // ---------------------------------------------------------------- the decimal grid

    static decimal[] _grid;
    static decimal[] Grid
    {
        get
        {
            if (_grid != null) return _grid;
            decimal[] mant =
            {
                0m, 1m, 2m, 3m, 5m, 6m, 7m, 9m, 10m, 11m, 12m, 15m, 25m, 45m, 50m, 55m, 60m, 99m, 100m, 101m, 125m, 600m, 1000m, 12345m,
                599999999m, 600000000m, 600000001m, 4294967295m, 4294967296m, 18446744073709551615m, 18446744073709551616m,
                39614081257132168796771975168m, 79228162514264337593543950335m, 79228162514264337593543950334m,
                10000000000000000000000000000m, 9999999999999999999999999999m, 5000000000000000000000000000m, 5000000000000000000000000001m,
                1000000000000000000000000001m, 3333333333333333333333333333m, 6666666666666666666666666667m, 1428571428571428571428571429m,
                59999999999999999999999999999m, 134217728m, 536870912m, 2147483648m, 1220703125m, 95367431640625m, 7450580596923828125m,
                1152921504606846976m, 123456789012345678901234567m, 999999999999999999m
            };
            int[] scales = { 0, 1, 2, 3, 5, 6, 9, 10, 12, 13, 14, 15, 18, 19, 20, 26, 27, 28 };
            var l = new List<decimal>();
            foreach (decimal m in mant)
                foreach (int sc in scales)
                {
                    int[] b = decimal.GetBits(m);
                    l.Add(new decimal(b[0], b[1], b[2], false, (byte)sc));
                }
            _grid = l.ToArray();
            return _grid;
        }
    }

    static Suite DecGrid()
    {
        long n = Grid.Length;
        return new Suite
        {
            Name = "Dec grid: all pairs, add sub mul div cmp, acc*60/op, acc*op/60",
            Exhaustive = true,
            Count = n * n,
            Once = true,
            Gen = id =>
            {
                decimal a = Grid[(int)(id / n)], b = Grid[(int)(id % n)];
                var r = RngFor("decgrid", id);
                if (r.Chance(30)) a = -a;
                if (r.Chance(30)) b = -b;
                var l = new List<Case>();
                l.AddRange(DecPair(a, b));
                decimal t;
                if (b != 0m)
                {
                    bool ok = true;
                    t = 0m;
                    try { t = a * 60m; } catch (OverflowException) { ok = false; }
                    if (ok) l.AddRange(DecDivOnly(t, b));
                    ok = true;
                    try { t = a * b; } catch (OverflowException) { ok = false; }
                    if (ok) l.AddRange(DecDivOnly(t, 60m));
                    ok = true;
                    try { t = a / b; } catch (OverflowException) { ok = false; }
                    if (ok)
                    {
                        decimal q = t;
                        l.Add(new Case("dec.mul " + D(q) + " " + D(b), () => D(q * b)));
                        l.Add(new Case("dec.sub " + D(a) + " " + D(q), () => D(a - q)));
                    }
                }
                return l;
            }
        };
    }

    static Suite DecGridUnary()
    {
        return new Suite
        {
            Name = "Dec grid: round away/even 0..28, trunc, abs, neg, int64, parse",
            Exhaustive = true,
            Count = Grid.Length * 2,
            Once = true,
            Gen = id =>
            {
                decimal a = Grid[(int)(id / 2)];
                if (id % 2 == 1) a = -a;
                return DecUnary(a);
            }
        };
    }

    // The TimeMath and converter functions on the grid, at the 6e8 limit, and with every number of places.
    static Suite TimeGrid()
    {
        var vals = new List<decimal>(Grid);
        foreach (decimal g in Grid) vals.Add(-g);
        decimal[] mins = { 600000000m, 599999999m, 599999999.5m, 599999999.9999999m, 599999999.999999999m, 600000039m, 9999999m * 60m + 59.5m, 9999999m * 60m + 59.4999999999m, 59.5m, 0.5m, 1.5m, 2.5m };
        foreach (decimal m in mins) { vals.Add(m); vals.Add(-m); }
        var rr = new Rng(99);
        for (int i = 0; i < 20000; i++)
        {
            int[] b = { (int)rr.Next(), (int)rr.Next(), rr.Chance(50) ? 0 : (int)(rr.Next() >> rr.Int(33)) };
            vals.Add(new decimal(b[0], b[1], b[2], rr.Chance(40), (byte)rr.Int(29)));
        }
        decimal[] arr = vals.ToArray();
        int[] places = { 2, 3, 4, 5, 6, -1, 0, 1, 7, 100, -1000, 999999999 };
        return new Suite
        {
            Name = "TimeMath/Converter formatting on the grid and the limits",
            Exhaustive = true,
            Count = arr.Length,
            Gen = id =>
            {
                decimal m = arr[(int)id];
                var l = new List<Case>();
                l.Add(TmRoundMin(m)); l.Add(TmRoundH(m)); l.Add(TmHm(m)); l.Add(TmDec(m)); l.Add(TmWords(m)); l.Add(TmFactor(m));
                l.Add(CvFormat(0, m)); l.Add(CvFormat(1, m));
                for (int p = 0; p < places.Length; p++)
                {
                    l.Add(TmRoundHP(m, places[p]));
                    l.Add(TmDecP(m, places[p]));
                    l.Add(CvFormatP(0, m, places[p]));
                    l.Add(CvFormatP(1, m, places[p]));
                }
                return l;
            }
        };
    }

    // ---------------------------------------------------------------- converter and TryParseEntry fuzz

    static string RandomConvText(Rng r, string sep)
    {
        switch (r.Int(8))
        {
            case 0: return RandomUtf16(r, 20);
            case 1:
                {
                    // long digit runs around the 7 digit limit and the 8 digit hour count
                    string d = r.Pick(new[] { "1000000", "9999999", "10000000", "100000000", "99999999", "0000000", "00000001", "1000000000" });
                    return d + r.Pick(new[] { "", ":", ":59", ":99", ",5", "," + sep, ".5", sep + "999999", sep + "9999999", "u30", "h", "0", "00", "000" });
                }
            case 2:
                {
                    // digits and the separators of the culture
                    string alphabet = "0123456789:,.uUhH " + sep;
                    var sb = new StringBuilder();
                    int n = r.Int(16);
                    for (int i = 0; i < n; i++)
                    {
                        if (r.Chance(15)) sb.Append(sep); else sb.Append(alphabet[r.Int(alphabet.Length)]);
                    }
                    return sb.ToString();
                }
            case 3:
                {
                    // structured: digits sep digits with a place for the second separator and trailing noise
                    string t = DigitRun(r, 9) + r.Pick(new[] { ":", ",", ".", sep, "u", "h", "", sep + sep, ":" + sep }) + DigitRun(r, 9);
                    if (r.Chance(25)) t += r.Pick(new[] { ":", ",", ".", sep, "x", "-", " " }) + DigitRun(r, 4);
                    return t;
                }
            case 4:
                {
                    // characters of the separator split up (multi character separators)
                    var sb = new StringBuilder();
                    int n = r.Int(10);
                    for (int i = 0; i < n; i++)
                    {
                        if (sep.Length > 0 && r.Chance(40)) sb.Append(sep[r.Int(sep.Length)]);
                        else sb.Append((char)('0' + r.Int(10)));
                    }
                    return sb.ToString();
                }
            case 5: return FuzzPaste(r);
            case 6: return DigitRun(r, 12);
            default:
                {
                    string t = DigitRun(r, 8) + sep + DigitRun(r, 8);
                    if (t.Length == 0) return t;
                    int pos = r.Int(t.Length);
                    return t.Insert(pos, r.Pick(new[] { "0", "9", ":", ",", ".", "u", "h", "-", " ", U(0x0663), U(0xFF13), "\0" }));
                }
        }
    }

    static Suite ConverterFuzz(long count)
    {
        return new Suite
        {
            Name = "converter fuzz (cv.all on random text, both fields)",
            NoThrow = true,
            Count = count,
            Share = 0.25,
            Gen = id =>
            {
                var r = RngFor("convfuzz", id);
                string sep = CulSeps[_cul];
                string t = RandomConvText(r, sep);
                return new[] { CvAll(t, (long)r.Int(1000)) };
            }
        };
    }

    static Suite ConverterPlaces(long count)
    {
        return new Suite
        {
            Name = "converter with any number of places (norm, compute, convert)",
            NoThrow = true,
            Count = count,
            Share = 0.25,
            Gen = id =>
            {
                var r = RngFor("convplaces", id);
                string sep = CulSeps[_cul];
                string t = RandomConvText(r, sep);
                int p = r.Chance(70) ? 2 + r.Int(5) : r.Pick(new[] { -1, 0, 1, 7, 8, 28, 29, 100, -100000, 999999999, -999999999 });
                int f = r.Int(2);
                return new[] { CvNormP(f, t, p), CvComputeP(f, t, p), CvConvert(f, t, p) };
            }
        };
    }

    static Suite EntryFuzz(long count)
    {
        return new Suite
        {
            Name = "TimeMath.TryParseEntry on random text",
            NoThrow = true,
            Count = count,
            Once = true,
            Gen = id =>
            {
                var r = RngFor("entry", id);
                string t;
                if (r.Chance(60))
                {
                    t = DigitRun(r, 9) + r.Pick(new[] { ":", ",", ".", "", ":,", ",:", "..", "::" }) + DigitRun(r, 9);
                    if (r.Chance(10)) t += r.Pick(new[] { ":", ",", "." }) + DigitRun(r, 3);
                }
                else t = FuzzPaste(r);
                return new[] { TmParse(t) };
            }
        };
    }

    // Converter.TryGetMinutes on text that did not go through the filter (the UI never does this).
    static Suite RawGetMinutes(long count)
    {
        return new Suite
        {
            Name = "Converter.TryGetMinutes on unfiltered text (info)",
            Count = count,
            Once = true,
            Info = true,
            Gen = id =>
            {
                var r = RngFor("rawgm", id);
                string alphabet = "0123456789:.,-+ x";
                var sb = new StringBuilder();
                int n = r.Int(9);
                for (int i = 0; i < n; i++) sb.Append(alphabet[r.Int(alphabet.Length)]);
                string t = sb.ToString();
                if (r.Chance(10)) t = DigitRun(r, 40) + r.Pick(new[] { ".", ",", ":" }) + DigitRun(r, 40);
                return new[] { CvGetMin(0, t), CvGetMin(1, t) };
            }
        };
    }

    // Dec.Parse on text with more than 28 fraction digits, or at the 96 bit edge (only reachable through unfiltered text).
    static Suite DecParseEdges()
    {
        var texts = new List<string>();
        string zeros27 = new string('0', 27);
        texts.Add("0." + zeros27 + "4"); texts.Add("0." + zeros27 + "5"); texts.Add("0." + zeros27 + "6");
        texts.Add("0." + zeros27 + "15"); texts.Add("0." + zeros27 + "25"); texts.Add("0." + zeros27 + "250"); texts.Add("0." + zeros27 + "2500000001");
        texts.Add("0." + zeros27 + "1" + "5"); texts.Add("0." + new string('0', 28) + "5");
        texts.Add("1." + new string('0', 27) + "5"); texts.Add("1." + new string('0', 27) + "15");
        texts.Add("79228162514264337593543950335"); texts.Add("79228162514264337593543950336"); texts.Add("79228162514264337593543950335.4");
        texts.Add("79228162514264337593543950335.5"); texts.Add("79228162514264337593543950334.5"); texts.Add("7922816251426433759354395033.55");
        texts.Add("7922816251426433759354395033.5"); texts.Add("7922816251426433759354395033.45"); texts.Add("792281625142643375935439503.35");
        texts.Add("0.79228162514264337593543950335"); texts.Add("0.79228162514264337593543950336"); texts.Add("0.792281625142643375935439503355");
        texts.Add("0.7922816251426433759354395033550"); texts.Add("0.79228162514264337593543950345"); texts.Add("0.792281625142643375935439503350000001");
        texts.Add("1234567890123456789012345678.9"); texts.Add("1234567890123456789012345678.95"); texts.Add("12345678901234567890123456789");
        texts.Add("0.00000000000000000000000000000000001"); texts.Add("00000000000000000000000000000000000000001.5");
        texts.Add("0.12345678901234567890123456785"); texts.Add("0.12345678901234567890123456775"); texts.Add("0.123456789012345678901234567850000000000000001");
        texts.Add("5.5"); texts.Add("0"); texts.Add("0.0"); texts.Add("000.000"); texts.Add("-0"); texts.Add("-0.0");
        string[] arr = texts.ToArray();
        return new Suite
        {
            Name = "Dec.Parse at the 28 fraction digit and 96 bit edges (info)",
            Exhaustive = true,
            Count = arr.Length,
            Once = true,
            Info = true,
            Gen = id =>
            {
                string t = arr[(int)id];
                return new[] { new Case("dec.parse " + t, () => D(decimal.Parse(t, Inv))) };
            }
        };
    }

    // ---------------------------------------------------------------- Settings

    static Suite SettingsCasing()
    {
        const string key = "DecimalPlaces";
        // forms: 0..12 replace key[i] by c; then c in front of the key, before '=', after '=', after the value,
        // around the value, between two key lines, and around a second line
        const int Forms = 20;
        return new Suite
        {
            Name = "Settings.ParseDecimalPlaces, every code unit in every position",
            NoThrow = true,
            Exhaustive = true,
            Count = 65536L * Forms,
            Once = true,
            Gen = id =>
            {
                int c = (int)(id % 65536);
                int form = (int)(id / 65536);
                string ch = ((char)c).ToString();
                string text;
                if (form < key.Length) text = key.Substring(0, form) + ch + key.Substring(form + 1) + "=4";
                else
                    switch (form - key.Length)
                    {
                        case 0: text = ch + key + "=4"; break;
                        case 1: text = key + ch + "=4"; break;
                        case 2: text = key + "=" + ch + "4"; break;
                        case 3: text = key + "=4" + ch; break;
                        case 4: text = key + "=" + ch + "4" + ch; break;
                        case 5: text = "DecimalPlaces=3" + ch + "DecimalPlaces=5"; break;
                        default: text = "x=1" + ch + "\n" + ch + key + ch + "=" + ch + "6" + ch; break;
                    }
                return new[] { new Case("st.parse " + Tok(text), () => I(Settings.ParseDecimalPlaces(text))) };
            }
        };
    }

    // The same for the theme: every UTF-16 code unit in every position of the key "Theme" and of the values auto, light and dark
    // (OrdinalIgnoreCase of the reference against CompareStringOrdinal of the core), around them, between two lines, and as the
    // separator between the two keys of the file.
    static Suite SettingsThemeCasing()
    {
        const string key = "Theme";
        string[] values = { "auto", "light", "dark" };
        const int KeyForms = 5, AroundForms = 5;
        int valueForms = values.Sum(v => v.Length);
        int forms = KeyForms + AroundForms + valueForms + 4;
        return new Suite
        {
            Name = "Settings.ParseTheme, every code unit in every position",
            NoThrow = true,
            Exhaustive = true,
            Count = 65536L * forms,
            Once = true,
            Gen = id =>
            {
                int c = (int)(id % 65536);
                int form = (int)(id / 65536);
                string ch = ((char)c).ToString();
                string text;
                if (form < KeyForms) text = key.Substring(0, form) + ch + key.Substring(form + 1) + "=dark";
                else if (form < KeyForms + AroundForms)
                    switch (form - KeyForms)
                    {
                        case 0: text = ch + key + "=dark"; break;
                        case 1: text = key + ch + "=dark"; break;
                        case 2: text = key + "=" + ch + "dark"; break;
                        case 3: text = key + "=dark" + ch; break;
                        default: text = key + "=" + ch + "dark" + ch; break;
                    }
                else if (form < KeyForms + AroundForms + valueForms)
                {
                    int k = form - KeyForms - AroundForms;
                    string v = null;
                    foreach (string cand in values)
                    {
                        if (k < cand.Length) { v = cand.Substring(0, k) + ch + cand.Substring(k + 1); break; }
                        k -= cand.Length;
                    }
                    text = key + "=" + v;
                }
                else
                    switch (form - KeyForms - AroundForms - valueForms)
                    {
                        case 0: text = "Theme=light" + ch + "Theme=dark"; break;
                        case 1: text = "x=1" + ch + "\n" + ch + key + ch + "=" + ch + "dark" + ch; break;
                        case 2: text = "DecimalPlaces=4" + ch + "Theme=light"; break;
                        default: text = "Theme=dark" + ch + "DecimalPlaces=3"; break;
                    }
                string t = text;
                return new[]
                {
                    new Case("st.parsetheme " + Tok(t), () => I((int)Settings.ParseTheme(t))),
                    new Case("st.parse " + Tok(t), () => I(Settings.ParseDecimalPlaces(t)))
                };
            }
        };
    }

    static int SettingsFileChecks()
    {
        int mismatches = 0, checks = 0;
        string dir = Path.Combine(Path.GetTempPath(), "HourCalcReview-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        _dir = dir;
        _ndir = _linux ? _pool[0].Query("fs.mktemp") : dir;
        try
        {
            Cli cli = _pool[0];
            Action<string, string, string> check = (what, expected, actual) =>
            {
                checks++;
                if (expected != actual)
                {
                    mismatches++;
                    Console.WriteLine("  MISMATCH settings files: {0}\n    oracle: {1}\n    native: {2}", what, expected, actual);
                }
            };
            int n = 0;
            string ctx = "";
            Action<string, byte[]> file = (what, bytes) =>
            {
                string path = Path.Combine(dir, "f" + (n++) + ".ini");
                WriteBoth(path, bytes);
                string q = Esc(N(path));
                check("load " + ctx + what, I(Settings.LoadDecimalPlaces(path)), cli.Query("st.load " + q));
                check("load theme " + ctx + what, I((int)Settings.LoadTheme(path)), cli.Query("st.loadtheme " + q));
                int fp;
                ThemeMode ft;
                Settings.Load(path, out fp, out ft);
                check("load both " + ctx + what, I(fp) + " " + I((int)ft), cli.Query("st.loadall " + q));
            };
            // The same odd endings with each of these as the line in front of them: the places, the theme, both.
            foreach (string okText in new[] { "DecimalPlaces=3", "Theme=dark", "Theme=light\nDecimalPlaces=3" })
            {
                ctx = okText.Replace("\n", "+") + ": ";
                byte[] ok3 = Encoding.ASCII.GetBytes(okText);
                Func<byte[], byte[], byte[]> cat = (a, b) => a.Concat(b).ToArray();
                // Odd endings: a truncated multi byte sequence, a lone byte of a UTF-16 pair, a lone surrogate, NULs.
                file("UTF-8 + C3", cat(ok3, new byte[] { 0xC3 }));
                file("UTF-8 + E2 82", cat(ok3, new byte[] { 0xE2, 0x82 }));
                file("UTF-8 + F0 9F 98", cat(ok3, new byte[] { 0xF0, 0x9F, 0x98 }));
                file("UTF-8 + FF", cat(ok3, new byte[] { 0xFF }));
                file("UTF-8 + CRLF + C3", cat(ok3, new byte[] { 0x0D, 0x0A, 0xC3 }));
                file("UTF-8 C3 + CRLF + key", cat(new byte[] { 0xC3, 0x0D, 0x0A }, ok3));
                file("UTF-8 ED A0 80 (encoded surrogate) + key line", cat(new byte[] { 0xED, 0xA0, 0x80, 0x0A }, ok3));
                file("UTF-8 key=3 + ED A0 80", cat(ok3, new byte[] { 0xED, 0xA0, 0x80 }));
                file("UTF-8 key=3 + C0 80 (overlong NUL)", cat(ok3, new byte[] { 0xC0, 0x80 }));
                file("UTF-8 key=3 + NUL", cat(ok3, new byte[] { 0 }));
                file("UTF-8 key=3 + CR only", cat(ok3, new byte[] { 0x0D }));
                file("UTF-8 key=3 + FE FF inside", cat(ok3, new byte[] { 0xFE, 0xFF }));
                file("UTF-8 BOM + key=3", cat(new byte[] { 0xEF, 0xBB, 0xBF }, ok3));
                file("UTF-8 BOM BOM + key=3", cat(new byte[] { 0xEF, 0xBB, 0xBF, 0xEF, 0xBB, 0xBF }, ok3));
                file("UTF-8 EF BB + key=3 (broken BOM)", cat(new byte[] { 0xEF, 0xBB }, ok3));
                file("UTF-8 EF BB + LF + key=3", cat(new byte[] { 0xEF, 0xBB, 0x0A }, ok3));
                byte[] u16 = new UnicodeEncoding(false, false).GetBytes(okText);
                byte[] u16be = new UnicodeEncoding(true, false).GetBytes(okText);
                byte[] u32 = new UTF32Encoding(false, false).GetBytes(okText);
                byte[] u32be = new UTF32Encoding(true, false).GetBytes(okText);
                byte[] bomLe = { 0xFF, 0xFE }, bomBe = { 0xFE, 0xFF }, bom32 = { 0xFF, 0xFE, 0, 0 }, bom32be = { 0, 0, 0xFE, 0xFF };
                file("UTF-16 LE BOM + key + odd byte", cat(cat(bomLe, u16), new byte[] { 0x41 }));
                file("UTF-16 LE BOM + key + 00", cat(cat(bomLe, u16), new byte[] { 0x00 }));
                file("UTF-16 BE BOM + key + odd byte", cat(cat(bomBe, u16be), new byte[] { 0x41 }));
                file("UTF-16 LE BOM + key + lone high surrogate", cat(cat(bomLe, u16), new byte[] { 0x3D, 0xD8 }));
                file("UTF-16 LE BOM + lone low surrogate + LF + key", cat(cat(bomLe, new byte[] { 0x00, 0xDC, 0x0A, 0x00 }), u16));
                file("UTF-32 LE BOM + key + 1 byte", cat(cat(bom32, u32), new byte[] { 0x41 }));
                file("UTF-32 LE BOM + key + 3 bytes", cat(cat(bom32, u32), new byte[] { 0x41, 0, 0 }));
                file("UTF-32 BE BOM + key + 2 bytes", cat(cat(bom32be, u32be), new byte[] { 0, 0x41 }));
                file("UTF-32 LE BOM + key + code point > 10FFFF", cat(cat(bom32, u32), new byte[] { 0x00, 0x00, 0x11, 0x00 }));
                file("UTF-32 LE BOM + key + surrogate code point", cat(cat(bom32, u32), new byte[] { 0x00, 0xD8, 0x00, 0x00 }));
                file("UTF-32 LE BOM + LF + invalid + key", cat(cat(bom32, new byte[] { 0x0A, 0, 0, 0, 0x00, 0x00, 0x11, 0x00, 0x0A, 0, 0, 0 }), u32));
                file("FF FE 00 (3 bytes)", new byte[] { 0xFF, 0xFE, 0x00 });
                file("FF FE 00 00 (UTF-32 BOM only)", new byte[] { 0xFF, 0xFE, 0x00, 0x00 });
                file("00 00 FE FF (UTF-32 BE BOM only)", new byte[] { 0x00, 0x00, 0xFE, 0xFF });
                file("00 00 FE (3 bytes)", cat(new byte[] { 0x00, 0x00, 0xFE }, ok3));
            }
            // Random byte soup built from the interesting pieces.
            var rnd = new Rng(555 + _seed);
            byte[][] pieces =
            {
                Encoding.ASCII.GetBytes("DecimalPlaces=3"), Encoding.ASCII.GetBytes("\nDecimalPlaces=4"), Encoding.ASCII.GetBytes("\r\n"), new byte[] { 0xC3 }, new byte[] { 0xC3, 0xA9 }, new byte[] { 0xFF },
                new byte[] { 0xFE }, new byte[] { 0 }, new byte[] { 0xEF, 0xBB, 0xBF }, new byte[] { 0xFF, 0xFE }, new byte[] { 0xFE, 0xFF }, new byte[] { 0xE2, 0x82 },
                new byte[] { 0xF0, 0x9F }, new byte[] { 0x20 }, new byte[] { 0x0A }, new byte[] { 0xC2, 0xA0 }, new byte[] { 0xC2, 0x85 }, Encoding.ASCII.GetBytes("DecimalPlaces="),
                Encoding.ASCII.GetBytes("Theme=dark"), Encoding.ASCII.GetBytes("\nTheme=Light"), Encoding.ASCII.GetBytes("Theme="), Encoding.ASCII.GetBytes("\nTHEME=auto\r\n"),
                Encoding.ASCII.GetBytes("dark"), Encoding.ASCII.GetBytes("\n"), new byte[] { 0x3D }
            };
            int soup = 3000;
            var lines = new List<string>();
            var exps = new List<string>();
            var names = new List<string>();
            for (int i = 0; i < soup; i++)
            {
                var ms = new MemoryStream();
                int cnt = 1 + rnd.Int(6);
                for (int j = 0; j < cnt; j++) { byte[] pc = rnd.Pick(pieces); ms.Write(pc, 0, pc.Length); }
                byte[] bytes = ms.ToArray();
                string path = Path.Combine(dir, "s" + i + ".ini");
                WriteBoth(path, bytes);
                lines.Add("st.loadall " + Esc(N(path)));
                int sp;
                ThemeMode stm;
                Settings.Load(path, out sp, out stm);
                exps.Add(I(sp) + " " + I((int)stm));
                names.Add(BitConverter.ToString(bytes));
            }
            string[] got = cli.Batch(lines);
            for (int i = 0; i < soup; i++) check("random bytes " + names[i], exps[i], got[i]);

            // A random walk of single-setting saves (SavePlaces, SaveTheme: the other setting is read from the file again), of the
            // primitive Save and of edits from outside (an editor, another HourCalc, the file deleted, a file that is no settings
            // file at all): the oracle and the native core keep the same bytes at every step, and the same results. The caller's
            // own copy of the other setting is often out of date, as in a second window that has not seen the first one's change.
            {
                var walk = new Rng(777 + _seed);
                string[] outside = { null, "", "junk", "DecimalPlaces=6", "Theme=dark", "theme = LIGHT\r\n", "Theme=blue\r\nDecimalPlaces=9\r\n", "DecimalPlaces=3\r\nTheme=Light\r\nmore text\r\n", "Theme=dark\nTheme=light" };
                Func<string, string> bytesOf = p => File.Exists(p) ? BitConverter.ToString(File.ReadAllBytes(p)) : "(none)";
                for (int w = 0; w < 12; w++)
                {
                    string a = Path.Combine(dir, "walk-o" + w + ".ini"), b = Path.Combine(dir, "walk-n" + w + ".ini");
                    int believedPlaces = 2;
                    int believedTheme = 0;
                    for (int step = 0; step < 80; step++)
                    {
                        int op = walk.Int(7);
                        string what = "walk " + w + " step " + step + " op " + op;
                        if (op <= 1)
                        {
                            int value = walk.Chance(85) ? 2 + walk.Int(5) : walk.Pick(new[] { -1, 0, 1, 7, 100 });
                            check(what + " SavePlaces " + value + " result", B(Settings.SavePlaces(a, value, (ThemeMode)believedTheme)), cli.Query("st.saveplaces " + Esc(N(b)) + " " + value + " " + believedTheme));
                            believedPlaces = value;
                        }
                        else if (op <= 3)
                        {
                            int value = walk.Chance(90) ? walk.Int(3) : walk.Pick(new[] { -1, 3, 100 });
                            check(what + " SaveTheme " + value + " result", B(Settings.SaveTheme(a, (ThemeMode)value, believedPlaces)), cli.Query("st.savetheme " + Esc(N(b)) + " " + value + " " + believedPlaces));
                            believedTheme = value;
                        }
                        else if (op == 4)
                        {
                            int p = walk.Int(7), t = walk.Int(3);
                            check(what + " Save " + p + " " + t + " result", B(Settings.Save(a, p, (ThemeMode)t)), cli.Query("st.save " + Esc(N(b)) + " " + p + " " + t));
                            believedPlaces = p;
                            believedTheme = t;
                        }
                        else
                        {
                            string text = walk.Pick(outside);
                            if (text == null) { if (File.Exists(a)) File.Delete(a); NDelete(b); }
                            else { File.WriteAllText(a, text); NWriteText(b, text); }
                        }
                        check(what + " bytes", bytesOf(a), NBytes(b));
                    }
                }
                check("the walk left no temporary file behind", "0", TmpCount(dir).ToString());
            }
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
            if (_linux) { try { _pool[0].Query("fs.rmrf " + Esc(_ndir)); } catch (Exception) { } }
        }
        Interlocked.Add(ref _totalCases, checks);
        Interlocked.Add(ref _totalMismatches, mismatches);
        Console.WriteLine(string.Format(Inv, "{0,-58} {1,12:N0} checks {2,6} mismatches", "Settings files with odd endings and random bytes", checks, mismatches));
        return mismatches;
    }

    // --coverage: what the generators reach, measured on the reference only (no native process needed).
    static void Coverage()
    {
        string[] names = { "mixed", "places after every key", "growth", "paste dense" };
        for (int mode = 0; mode < 4; mode++)
        {
            long seqs = 0, ops = 0, overflow = 0, divzero = 0, justEq = 0, scale20 = 0, scale28 = 0, pasteOk = 0, pasteNo = 0, accNonZero = 0, repeated = 0;
            long errorsSeen = 0, entriesFull = 0;
            for (long id = 0; id < 20000; id++)
            {
                _cul = 0;
                var r = RngFor("calc" + mode, id);
                Seq sq = GenSeq(r, mode, mode == 1 ? 100 : 200);
                var e = sq.Places < 0 ? new CalcEngine() : new CalcEngine(sq.Places);
                bool sawOver = false, sawDiv = false, sawJust = false, sawScale20 = false, sawScale28 = false, sawErr = false;
                string prevKey = "";
                foreach (Op op in sq.Ops)
                {
                    ops++;
                    switch (op.K)
                    {
                        case 'k': e.Press(op.A); break;
                        case 'p': if (e.Paste(op.A)) pasteOk++; else pasteNo++; break;
                        default: e.DecimalPlaces = int.Parse(op.A, Inv); break;
                    }
                    if (e.IsError) { sawErr = true; if (e.MainText == CalcEngine.OverflowText) sawOver = true; else sawDiv = true; }
                    if ((bool)FJustEq.GetValue(e)) sawJust = true;
                    if (op.K == 'k' && op.A == "=" && prevKey == "=") repeated++;
                    prevKey = op.K == 'k' ? op.A : "";
                    decimal acc = (decimal)FAcc.GetValue(e);
                    int sc = (decimal.GetBits(acc)[3] >> 16) & 0xFF;
                    if (sc >= 20) sawScale20 = true;
                    if (sc >= 28) sawScale28 = true;
                    if (acc != 0m) accNonZero++;
                    if (((string)FEntry.GetValue(e)).Length >= 7) entriesFull++;
                }
                seqs++;
                if (sawOver) overflow++;
                if (sawDiv) divzero++;
                if (sawErr) errorsSeen++;
                if (sawJust) justEq++;
                if (sawScale20) scale20++;
                if (sawScale28) scale28++;
            }
            Console.WriteLine(string.Format(Inv, "coverage {0,-24} {1} sequences, {2:N0} ops: Overflow in {3}, DivideByZero in {4}, = after = {5}x, result shown (=) in {6}, acc scale>=20 in {7}, acc scale 28 in {8}, Paste ok/rejected {9}/{10}, acc<>0 after {11} ops",
                names[mode], seqs, ops, overflow, divzero, repeated, justEq, scale20, scale28, pasteOk, pasteNo, accNonZero));
        }
    }

    // ---------------------------------------------------------------- main

    static string[] CulSeps;

    static Cul MakeCul(string name, CultureInfo ci, string sep)
    {
        return new Cul { Name = name, Ci = ci, Sep = sep };
    }

    static CultureInfo Made(string sep)
    {
        var c = new CultureInfo("en-US", false);
        c.NumberFormat.NumberDecimalSeparator = sep;
        return c;
    }

    static int Main(string[] args)
    {
        bool threadsGiven = false;
        for (int ai = 0; ai < args.Length; ai++)
        {
            string a = args[ai];
            if (a.StartsWith("--threads=")) { _threads = int.Parse(a.Substring(10), Inv); threadsGiven = true; }
            else if (a == "--cli" && ai + 1 < args.Length) _cliCmd = args[++ai];
            else if (a.StartsWith("--cli=")) _cliCmd = a.Substring(6);
            else if (a.StartsWith("--only=")) _only = a.Substring(7);
            else if (a.StartsWith("--scale=")) _scale = double.Parse(a.Substring(8), Inv);
            else if (a.StartsWith("--cultures=")) _cultureCount = int.Parse(a.Substring(11), Inv);
            else if (a.StartsWith("--seed=")) _seed = ulong.Parse(a.Substring(7), Inv);
            else if (a == "--state") _state = true;
            else if (a == "--coverage") { Coverage(); return 0; }
            else _cliPath = Path.GetFullPath(a);
        }
        if (_cliCmd != null && !threadsGiven) _threads = Math.Min(_threads, 6);
        if (_cliCmd == null && (_cliPath == null || !File.Exists(_cliPath)))
        {
            Console.WriteLine("usage: DifferentialReview.exe <path to hc_cli.exe> | --cli \"<command line>\"  [--threads=N] [--only=<text>] [--scale=F] [--cultures=N] [--seed=N] [--state]");
            return 2;
        }
        var total = Stopwatch.StartNew();
        Console.WriteLine("Review differential: C# oracle vs {0} ({1} threads, seed {2}, scale {3})", _cliCmd ?? _cliPath, _threads, _seed, _scale.ToString(Inv));

        _pool = new Cli[_threads];
        var starters = new Thread[_threads];
        for (int i = 0; i < _threads; i++)
        {
            int index = i;
            starters[i] = new Thread(() => { _pool[index] = StartCli(); });
            starters[i].Start();
        }
        foreach (Thread t in starters) t.Join();
        if (_pool[0].Query("ping") != "pong") { Console.WriteLine("hc_cli does not answer ping"); return 2; }
        _linux = _cliCmd != null && _pool[0].Query("os") == "linux";
        if (_linux) Console.WriteLine("The native program is the Linux build: its files are reached through fs.* commands.");

        var user = CultureInfo.CurrentCulture;
        if (_linux)
        {
            // The user of the native program is a Linux user: its decimal separator is the one it says it has (LANG on that machine).
            _pool[0].Query("sep \\e");
            var uc = new CultureInfo("en-US", false);
            uc.NumberFormat.NumberDecimalSeparator = Unesc(_pool[0].Query("tm.sep"));
            user = uc;
        }
        var nl = new CultureInfo("nl-BE", false);
        var en = new CultureInfo("en-US", false);
        var fa = new CultureInfo("fa-IR", false);
        var ch = new CultureInfo("de-CH", false);
        var cultures = new List<Cul>
        {
            MakeCul("nl-BE", nl, nl.NumberFormat.NumberDecimalSeparator),
            MakeCul("en-US", en, en.NumberFormat.NumberDecimalSeparator),
            MakeCul("fa-IR", fa, fa.NumberFormat.NumberDecimalSeparator),
            MakeCul("de-CH", ch, ch.NumberFormat.NumberDecimalSeparator),
            MakeCul("made-up", Made("ab"), "ab"),
            MakeCul("made-up", Made(U(0x066B)), U(0x066B)),
            MakeCul("made-up", Made(":"), ":"),
            MakeCul("made-up", Made("h"), "h"),
            MakeCul("made-up", Made(" "), " "),
            MakeCul("user", user, "")
        };
        if (_cultureCount < cultures.Count) cultures = cultures.Take(Math.Max(1, _cultureCount)).ToList();
        CulSeps = cultures.Select(c => c.Sep.Length == 0 ? user.NumberFormat.NumberDecimalSeparator : c.Sep).ToArray();
        foreach (Cul c in cultures)
            Console.WriteLine("  culture {0,-8} separator '{1}'{2}", c.Name, c.Sep.Length == 0 ? user.NumberFormat.NumberDecimalSeparator : c.Sep, c.Sep.Length == 0 ? " (no override: the core reads the user's own)" : "");

        var suites = new List<Suite>();
        suites.Add(DecGrid());
        suites.Add(DecGridUnary());
        suites.Add(TimeGrid());
        suites.Add(DecParseEdges());
        suites.Add(CalcRandom("calc mixed, up to 200 keys", 0, 200, 150000, 0.15));
        suites.Add(CalcRandom("calc decimal places change after every key", 1, 100, 120000, 0.15));
        suites.Add(CalcRandom("calc growth (repeated * / =), up to 200 keys", 2, 200, 150000, 0.15));
        suites.Add(CalcRandom("calc Paste dense, up to 200 keys", 3, 200, 150000, 0.15));
        suites.Add(PasteBmp());
        suites.Add(PasteChains());
        suites.Add(CalcDeep("calc exhaustive deep (11 keys, up to 6)", new[] { "0", "9", ",", ":", "+", "-", "*", "/", "=", "CE", "BACK" }, 6, 0.05));
        suites.Add(ConverterFuzz(1500000));
        suites.Add(ConverterPlaces(400000));
        suites.Add(EntryFuzz(600000));
        suites.Add(RawGetMinutes(300000));
        suites.Add(SettingsCasing());
        suites.Add(SettingsThemeCasing());

        if (_state) suites = suites.Where(x => x.Name.StartsWith("calc")).ToList();
        for (int ci = 0; ci < cultures.Count; ci++)
        {
            Cul c = cultures[ci];
            Console.WriteLine("-- decimal separator '{0}' ({1})", CulSeps[ci], c.Name);
            Thread.CurrentThread.CurrentCulture = c.Ci;
            foreach (Suite s in suites)
            {
                if (_only.Length > 0 && s.Name.IndexOf(_only, StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (s.Once && ci != 0) continue;
                if (s.Light && ci >= 2) continue;
                RunSuite(s, c, ci);
            }
        }
        if (!_state && (_only.Length == 0 || "settings files".IndexOf(_only, StringComparison.OrdinalIgnoreCase) >= 0)) SettingsFileChecks();

        foreach (Cli c in _pool) c.Dispose();
        Console.WriteLine();
        Console.WriteLine(string.Format(Inv, "Review differential: {0:N0} checks, {1:N0} mismatches (plus {2:N0} in the info suites), {3:N1} s", _totalCases, _totalMismatches, _infoMismatches, total.Elapsed.TotalSeconds));
        return _totalMismatches == 0 ? 0 : 1;
    }
}
