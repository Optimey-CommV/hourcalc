// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Differential test: the C# reference (oracle\*.cs, compiled into this program) against the native
// core (obj\core\hc_cli.exe, line protocol described in native\cli\hc_cli.cpp). Every string the
// native core produces must equal the reference's, for the same input, in a ',' and a '.' decimal
// separator culture (and one made-up multi character separator). Exit code 0 only without mismatch.
//
//   differential.exe <hc_cli.exe> [--quick] [--threads=N] [--only=<text in suite name>]
//   differential.exe --cli "<command line>" [--quick] [--threads=N] [--only=<text>]
//     the native program through a command line, for the Linux build: --cli "wsl.exe -d Debian --cd / -- <path of hc_cli>"
//
// Suites: SPEC example tables; exhaustive converter strings; boundary converter strings; exhaustive
// calculator key sequences; random calculator sequences (fixed seed, Paste and decimal place
// changes mixed in); the TimeMath grid; System.Decimal arithmetic; Settings.
// Each mismatch is printed with its input and, for calculator sequences, a minimal reproduction.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using HourCalc;

static class Differential
{
    static string _cliPath;
    static bool _quick;
    static int _threads = Math.Max(2, Environment.ProcessorCount);
    static string _only = "";
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    static Cli[] _pool;                  // one native process per worker thread, kept for the whole run

    // ---------------------------------------------------------------- escaping (same as hc_cli.cpp)

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

    static string Tok(string s)
    {
        return s.Length == 0 ? "\\e" : Esc(s);
    }

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
                        if (i + 4 <= s.Length - 1 && int.TryParse(s.Substring(i + 1, 4), NumberStyles.HexNumber, Inv, out v))
                        {
                            o.Append((char)v);
                            i += 4;
                        }
                        else o.Append("\\u");
                    }
                    break;
                default: o.Append('\\').Append(k); break;
            }
        }
        return o.ToString();
    }

    // Shows a request or answer line with its escapes resolved, for the mismatch report.
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
                // Through a command line (wsl.exe ...) anything may be printed in front of the first answer: wait for "pong".
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
                    throw new InvalidOperationException("hc_cli.exe ended early at request " + i + " of " + result.Length + ": " + lines[Math.Min(i, lines.Count - 1)] + (writeError != null ? " (" + writeError.Message + ")" : ""));
                }
                result[i] = r;
            }
            writer.Join();
            return result;
        }

        public string Query(string line)
        {
            return Batch(new List<string> { line })[0];
        }

        public void Dispose()
        {
            try { _in.Close(); } catch (Exception) { }
            try { if (!_p.WaitForExit(5000)) _p.Kill(); } catch (Exception) { }
            _p.Dispose();
        }
    }

    // ---------------------------------------------------------------- where the native program runs

    // --cli "<command line>" starts the native program through a command line instead of as a file on this machine, for example
    //   --cli "wsl.exe -d Debian --cd / -- /home/<user>/hc-core-build/native/core/hc_cli"   (the Linux build).
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

    // --- TimeMath
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
    static Case TmClamp(int n) { return new Case("tm.clamp " + n, () => I(TimeMath.ClampDecimals(n))); }

    // --- Converter
    static Case CvFilter(int f, string t) { return new Case("cv.filter " + f + " " + Tok(t), () => Tok(Converter.Filter(f, t))); }
    static Case CvFilterField(int f, string t) { return new Case("cv.filterfield " + f + " " + Tok(t), () => Tok(Converter.FilterField(f, t))); }
    static Case CvGetMin(int f, string t)
    {
        return new Case("cv.getmin " + f + " " + Tok(t), () =>
        {
            decimal m;
            bool ok = Converter.TryGetMinutes(f, t, out m);
            return B(ok) + " " + D(m);
        });
    }
    static Case CvFormat(int f, decimal m) { return new Case("cv.format " + f + " " + D(m), () => Tok(Converter.Format(f, m))); }
    static Case CvFormatP(int f, decimal m, int p) { return new Case("cv.format " + f + " " + D(m) + " " + p, () => Tok(Converter.Format(f, m, p))); }
    static Case CvNorm(int f, string t) { return new Case("cv.norm " + f + " " + Tok(t), () => Tok(Converter.Normalize(f, t))); }
    static Case CvNormP(int f, string t, int p) { return new Case("cv.norm " + f + " " + Tok(t) + " " + p, () => Tok(Converter.Normalize(f, t, p))); }
    static Case CvCompute(int f, string t)
    {
        return new Case("cv.compute " + f + " " + Tok(t), () =>
        {
            string o, c;
            Converter.Compute(f, t, out o, out c);
            return Tok(o) + " " + Tok(c);
        });
    }
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

    // cv.all: every converter function on one text, both fields, in one request (see hc_cli.cpp).
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

    static string CeLine(Seq s)
    {
        var sb = new StringBuilder("ce.run ");
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
            sb.Append(paste).Append(' ');
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

    static Case CeKeys(int places, params string[] keys)
    {
        var s = new Seq { Places = places, Ops = new List<Op>() };
        foreach (string k in keys) s.Ops.Add(new Op('k', k));
        return CeCase(s);
    }

    // "7:30 + 7:20 =" style key strings, as in tests\Tests.cs: characters one by one, spaces ignored,
    // <CE> <C> <BK> for the named keys.
    static Seq SeqOfKeys(int places, string keys)
    {
        var s = new Seq { Places = places, Ops = new List<Op>() };
        for (int i = 0; i < keys.Length; i++)
        {
            char c = keys[i];
            if (c == ' ') continue;
            if (c == '<')
            {
                int end = keys.IndexOf('>', i);
                string name = keys.Substring(i + 1, end - i - 1);
                s.Ops.Add(new Op('k', name == "BK" ? "BACK" : name));
                i = end;
                continue;
            }
            s.Ops.Add(new Op('k', c.ToString()));
        }
        return s;
    }

    // --- Dec
    static Case DecBin(string cmd, decimal a, decimal b, Func<string> f)
    {
        return new Case(cmd + " " + D(a) + " " + D(b), f);
    }

    static IEnumerable<Case> DecPair(decimal a, decimal b)
    {
        yield return DecBin("dec.add", a, b, () => D(a + b));
        yield return DecBin("dec.sub", a, b, () => D(a - b));
        yield return DecBin("dec.mul", a, b, () => D(a * b));
        yield return DecBin("dec.div", a, b, () => D(a / b));
        yield return DecBin("dec.cmp", a, b, () => B(a == b) + B(a != b) + B(a < b) + B(a <= b) + B(a > b) + B(a >= b));
    }

    static IEnumerable<Case> DecSingle(decimal a, int n)
    {
        int r = n % 29;
        yield return new Case("dec.round " + D(a) + " " + r, () => D(Math.Round(a, r, MidpointRounding.AwayFromZero)));
        yield return new Case("dec.rounde " + D(a) + " " + r, () => D(Math.Round(a, r)));
        yield return new Case("dec.trunc " + D(a), () => D(Math.Truncate(a)));
        yield return new Case("dec.abs " + D(a), () => D(Math.Abs(a)));
        yield return new Case("dec.neg " + D(a), () => D(-a));
        yield return new Case("dec.int64 " + D(a), () => ((long)a).ToString(Inv));
        yield return new Case("dec.parse " + D(a), () => D(decimal.Parse(D(a), Inv)));
        yield return new Case("dec.iszero " + D(a), () => B(a == 0m));
        yield return new Case("dec.isneg " + D(a), () => B(a < 0m));
    }

    // --- Settings
    static Case StParse(string text) { return new Case("st.parse " + Tok(text), () => I(Settings.ParseDecimalPlaces(text))); }
    static Case StParseTheme(string text) { return new Case("st.parsetheme " + Tok(text), () => I((int)Settings.ParseTheme(text))); }

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

    static decimal RandDecimal(Rng r)
    {
        switch (r.Int(8))
        {
            case 0: return r.Int(2000001) - 1000000;                                   // integers
            case 1: return (decimal)(r.Int(2000001) - 1000000) / 100m;                  // 2 places
            case 2:                                                                     // minutes like: up to 7 digit hours, fraction
                {
                    decimal v = r.Int(10000000) * 60m + r.Int(60) + (decimal)r.Int(1000000) / 1000000m;
                    return r.Chance(30) ? -v : v;
                }
            case 3:                                                                     // quotient like: 28 digits
                {
                    decimal v = (decimal)(r.Int(1000000) + 1) / (decimal)(r.Int(997) + 1);
                    v *= 1 + r.Int(100);
                    return r.Chance(30) ? -v : v;
                }
            case 4:                                                                     // full random 96 bit mantissa, random scale
                {
                    return new decimal((int)r.Next(), (int)r.Next(), (int)(r.Next() >> (r.Int(33))), r.Chance(40), (byte)r.Int(29));
                }
            case 5:                                                                     // small mantissa, random scale
                {
                    return new decimal((int)(r.Next() >> (r.Int(33))), 0, 0, r.Chance(40), (byte)r.Int(29));
                }
            case 6:                                                                     // around a midpoint
                {
                    int p = 2 + r.Int(5);
                    decimal scale = 1m;
                    for (int i = 0; i < p; i++) scale *= 10m;
                    decimal half = (2 * r.Int(5000) + 1) / (2m * scale);                // exact midpoint at p places
                    decimal eps = r.Int(4) == 0 ? 0m : 1m / (decimal)Math.Pow(10, 8 + r.Int(10));
                    decimal v = half + (r.Chance(50) ? eps : -eps);
                    return r.Chance(50) ? -v : v;
                }
            default:                                                                    // near the limit
                {
                    decimal v = 600000000m + (decimal)(r.Int(2001) - 1000) / 100m;
                    return r.Chance(30) ? -v : v;
                }
        }
    }

    // ---------------------------------------------------------------- suites

    sealed class Suite
    {
        public string Name;
        public long Count;                          // ids 0 .. Count-1
        public Func<long, IEnumerable<Case>> Gen;
        public bool Heavy;                          // skipped for the extra (made-up and user) cultures
        public bool Once;                           // does not depend on the culture: run for the first one only
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

    const int MaxSamples = 12;
    static long _totalCases;
    static long _totalMismatches;
    static readonly object PrintLock = new object();

    static void Worker(Suite s, Stats st, CultureInfo ci, string sep, long[] next, long block, int index)
    {
        Thread.CurrentThread.CurrentCulture = ci;
        Thread.CurrentThread.CurrentUICulture = CultureInfo.InvariantCulture;
        {
            Cli cli = _pool[index];
            cli.Query("sep " + Tok(sep));
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
                    if (exp != got[i])
                    {
                        bad++;
                        lock (st)
                        {
                            if (st.Samples.Count < MaxSamples)
                                st.Samples.Add(new Sample { Line = cases[i].Line, Expected = exp, Actual = got[i], Tag = cases[i].Tag });
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
                if (from >= s.Count) break;
                long to = Math.Min(s.Count, from + block);
                for (long id = from; id < to; id++)
                {
                    foreach (Case c in s.Gen(id))
                    {
                        cases.Add(c);
                        lines.Add(c.Line);
                    }
                    if (cases.Count >= 4096) flush();
                }
            }
            flush();
        }
    }

    static void RunSuite(Suite s, CultureInfo ci, string sep, string cultureName)
    {
        var st = new Stats { Name = s.Name + " [" + cultureName + "]" };
        var sw = Stopwatch.StartNew();
        long block = Math.Max(1, Math.Min(512, s.Count / (_threads * 8)));
        var next = new long[1];
        var threads = new Thread[(int)Math.Min(_threads, Math.Max(1, s.Count / 64))];
        Exception failure = null;
        for (int t = 0; t < threads.Length; t++)
        {
            int index = t;
            threads[t] = new Thread(() =>
            {
                try { Worker(s, st, ci, sep, next, block, index); }
                catch (Exception e) { failure = e; }
            });
            threads[t].Start();
        }
        foreach (Thread t in threads) t.Join();
        if (failure != null) throw new InvalidOperationException("suite " + st.Name + ": " + failure.Message, failure);
        Interlocked.Add(ref _totalCases, st.Cases);
        Interlocked.Add(ref _totalMismatches, st.Mismatches);
        Console.WriteLine(string.Format(Inv, "{0,-44} {1,12:N0} checks {2,6} mismatches {3,7:N1} s", st.Name, st.Cases, st.Mismatches, sw.Elapsed.TotalSeconds));
        if (st.Mismatches > 0) PrintSamples(st, ci, sep);
    }

    // ---------------------------------------------------------------- mismatch report

    static bool Differs(Seq s, Cli cli)
    {
        return Guard(() => CeEval(s)) != cli.Query(CeLine(s));
    }

    static Seq Minimize(Seq s, Cli cli)
    {
        var cur = new Seq { Places = s.Places, Ops = new List<Op>(s.Ops) };
        if (!Differs(cur, cli)) return cur;
        // Cut everything after the first differing step.
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
        while (changed)
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
            sb.Append(op.K == 'd' ? op.A : "\"" + op.A.Replace("\\", "\\\\") + "\"");
            if (op.K != 'd') sb.Append(")");
        }
        return sb.ToString();
    }

    static string Clip(string s)
    {
        return s.Length <= 400 ? s : s.Substring(0, 400) + " ...";
    }

    // The first record (step) in which two ce.run answers differ: its index and both records.
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

    static void PrintSamples(Stats st, CultureInfo ci, string sep)
    {
        lock (PrintLock)
        {
            Cli cli = _pool[0];
            Thread.CurrentThread.CurrentCulture = ci;
            cli.Query("sep " + Tok(sep));
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
                // A calculator sequence: shrink it, then show the step in which the two differ.
                Seq min = Minimize(seq, cli);
                string exp = Guard(() => CeEval(min)), got = cli.Query(CeLine(min));
                string e, g;
                int step = FirstDiff(exp, got, out e, out g);
                Console.WriteLine("  MISMATCH calculator sequence of {0} ops, minimal reproduction of {1} ops:", seq.Ops.Count, min.Ops.Count);
                Console.WriteLine("    {0}", DescribeSeq(min));
                Console.WriteLine("    request: {0}", CeLine(min));
                Console.WriteLine("    first difference after op {0}{1}", step + 1, step >= 0 && step < min.Ops.Count ? " (" + min.Ops[step].Token() + ")" : " (the final record)");
                Console.WriteLine("    record = expression | left | right | caption | error | entry | places | paste result");
                Console.WriteLine("    oracle: {0}", Clip(Pretty(e)));
                Console.WriteLine("    native: {0}", Clip(Pretty(g)));
            }
            if (st.Mismatches > n - 1) Console.WriteLine(string.Format(Inv, "  ... {0} mismatches in all", st.Mismatches));
        }
    }

    // ---------------------------------------------------------------- suite definitions

    const string ConvAlphabet = "0123456789:.,uh x-";

    // The i-th string in length-then-lexicographic order over 'alphabet', lengths 0..maxLen.
    static string StringAt(long i, string alphabet, int maxLen)
    {
        int n = alphabet.Length;
        for (int len = 0; len <= maxLen; len++)
        {
            long count = 1;
            for (int k = 0; k < len; k++) count *= n;
            if (i < count)
            {
                var chars = new char[len];
                for (int k = len - 1; k >= 0; k--) { chars[k] = alphabet[(int)(i % n)]; i /= n; }
                return new string(chars);
            }
            i -= count;
        }
        throw new ArgumentOutOfRangeException("i");
    }

    static long StringCount(string alphabet, int maxLen)
    {
        long total = 0, count = 1;
        for (int len = 0; len <= maxLen; len++) { total += count; count *= alphabet.Length; }
        return total;
    }

    static Suite ConverterExhaustive(string name, string alphabet, int maxLen)
    {
        return new Suite
        {
            Name = name,
            Count = StringCount(alphabet, maxLen),
            Heavy = true,
            Gen = id => new[] { CvAll(StringAt(id, alphabet, maxLen), id) }
        };
    }

    static readonly string[] BoundarySuffixes =
    {
        "", ":", ":3", ":30", ":59", ":60", ":99", ":300", ",", ",5", ",50", ",999999", ",9999999", ".5", ".", "u30", "h5", ":,", ",:", "::", "00", "U30", "H5"
    };

    static Suite ConverterBoundary()
    {
        // 6..9 digit strings over {0,1,9} followed by separators and fractions: the 7 digit limit,
        // the one 8 digit hour count the formatter can produce (10000000) and the 9999999 rounding edge.
        var digits = new List<string>();
        for (int len = 6; len <= 9; len++)
        {
            long total = 1;
            for (int k = 0; k < len; k++) total *= 3;
            for (long v = 0; v < total; v++)
            {
                var chars = new char[len];
                long x = v;
                for (int k = len - 1; k >= 0; k--) { chars[k] = "019"[(int)(x % 3)]; x /= 3; }
                digits.Add(new string(chars));
            }
        }
        // Keep the run short: the interesting prefixes are all there, thinned out for the long ones.
        var picked = new List<string>();
        for (int i = 0; i < digits.Count; i++)
        {
            string d = digits[i];
            if (d.Length <= 7 || (i % 7) == 0 || d.StartsWith("1000000") || d.StartsWith("9999999")) picked.Add(d);
        }
        string[] list = picked.ToArray();
        long perDigit = BoundarySuffixes.Length;
        return new Suite
        {
            Name = "converter boundary",
            Count = list.Length * perDigit,
            Heavy = true,
            Gen = id => new[] { CvAll(list[id / perDigit] + BoundarySuffixes[id % perDigit], id) }
        };
    }

    // The converter tables of SPEC 4 and 6 and the cases of tests\Tests.cs.
    static Suite SpecTables()
    {
        var items = new List<string>();
        items.AddRange(new[]
        {
            "08:30", "8,50", "8,5", "08:20", "8,33", "830", "8.30", "8u30", "8h30", "0:45", "0,1", "123:45", "8,51", "0:90",
            "8", "12", "1230", "8:3", "8:", ":30", "0", "7:59", ",5", "8,", "8,333", "8,999", "123,75", "8.5", "0,01", "0,008",
            "", ":", ",", "abc", "8:30,", "8,058", "9,994", "0,0083", "10000000,65", "10000000:39", "9999999:60", "9999999,999",
            "9999999,999999", "20000000:00", "10000001:01", "-15", " 8 : 30 ", "1:2:3", "1,2,3", "123456789", "123456789:30",
            "0,1234567", "8:3045", "a1b2c", "8U30", "8H30", "\x0663", "\xFF18"
        });
        var list = items.ToArray();
        string[] required =
        {
            "7:30 + 7:20 =", "8 + 0:30 =", "7,5 + 7:20 =", "7:30 * 5 =", "7:30 / 4 =", "7:00 - 8:30 =", "7:30 + 1:00 = =", "0:90 =",
            "1:00 + 2:00 * 2 =", "7:30 / 0 =", "7:30 + =", "7:30 / 4 = * 4 =", "7:30 * 1,5 =", "7:30 * 1:30 =", "7:30 / 1,5 =",
            "7:30 / 1:30 =", "7:30 * 2 = =", "7:30 - 1:00 = =", "7:00 - 8:30 = + 2:00 =", "5 + 3 = + 2 =", "7:30 + 1 = 5 =",
            "1:20 + 1:20 + 1:20 =", "0,1 + 0,1 =", "8:30 - 0,5 =", "12 * 0 =", "7u30 + 7h20 =", "7:30 + 7:20 + 8:00 =",
            "1:00 + 1:00 * 3 + 1:00 =", "99:59 + 0:01 =", "0:30 / 3 * 3 =", "1:00 / 3 * 3 =", "2:00 / 7 * 7 =", "9999999 + 0:59 =",
            "7:30 + 7:20 + 8:00 +", "7:30 +", "7:30 + -", "7:30 + - /", "7:30 + 7:20 = +", "+", "7:30 + 1 = 5", "7:30 + 1 <C>",
            "7:00 - 8:30 = +", "7:30 + 7:2", "7,5", "12", "12345678", "7:300", "1,1234567", "7:3:0", "7,5:", "7:3,", ",5", ":5",
            "07:30", "7u30", "7h30", "7.5", "7:3<BK>", "7:3<BK><BK>", "7<BK>", "7<BK><BK>", "7:30 + 1 = <BK>", "<BK>", "7:3<BK>5",
            "7:3<BK><BK>+", "7:30 + 5 <CE>", "7:30 + 5 <CE> =", "7:30 + 5 <CE> 2 =", "7:30 + 5 <C>", "7:30 + 1 = <CE>", "<CE>",
            "7:30 + 1 = 2", "7:30 + 1 = + 2 =", "7:30 + 1 = :", "7:30 + ,5 =", "=", "0:90 = =", "0:30 - 0:30 =", "0:01 - 0:02 =",
            "7:30 / 4 = + 0:07 =", "7:30 / 0 = + = : ,", "7:30 / 0 = 5", "7:30 / 0 = <C>", "7:30 / 0 = <CE>", "7:30 / 0:00 +",
            "7:30 / 0,0 =", "0 / 0 =", "1:00 / 0,000001 =", "10:00 / 0,000001 =", "9999999 * 9999999 =", "9999999 + 1 =",
            "0 - 9999999 - 1 =", "9999999 + 1 = 5", "9999999 + 1 *", "9999999:60 =", "9999999:99 =", "9999999:60 +", "9999999:99 *",
            "9999999:60 -", "9999999:60 /", "9999999:59 =", "9999999:59 + 0 =", "9999999,999999 =", "0,1 + 0,2 =", "0,7 + 0,1 =",
            "8,33 + 0,01 =", "1:00 / 3 = * 3 =", "0:01 / 2 =", "0:03 / 2 =", "0:00 - 0:01 / 2 =", "0:05 / 6 * 3 =", "0:1 =",
            "1:00 / 3 / 2 * 6 / 3 * 3 =", "8:20 =", "7:3"
        };
        long nl = list.Length;
        return new Suite
        {
            Name = "SPEC and unit test tables",
            Count = nl + required.Length * 7 + 20,
            Gen = id => SpecCases(id, list, required)
        };
    }

    static IEnumerable<Case> SpecCases(long id, string[] list, string[] required)
    {
        if (id < list.Length)
        {
            for (int p = 2; p <= 6; p++)
            {
                // Every table text with every number of places: the full set of variants.
                string t = list[id];
                for (int f = 0; f < 2; f++)
                {
                    yield return CvCompute(f, t);
                    yield return CvComputeP(f, t, p);
                    yield return CvNorm(f, t);
                    yield return CvNormP(f, t, p);
                    yield return CvConvert(f, t, p);
                    yield return CvFilter(f, t);
                    yield return CvFilterField(f, t);
                    yield return CvGetMin(f, Converter.FilterField(f, t));
                }
            }
            yield break;
        }
        id -= list.Length;
        if (id < required.Length * 7)
        {
            string keys = required[id / 7];
            int places = (int)(id % 7) - 1;    // -1 (default ctor), 0 and 1 (clamped to 2), 2..5
            if (places == 0) places = 6;
            yield return CeCase(SeqOfKeys(places, keys));
            // The same keys followed by a decimal places change and by the other end keys.
            var s = SeqOfKeys(places, keys);
            s.Ops.Add(new Op('d', "4"));
            s.Ops.Add(new Op('k', "="));
            s.Ops.Add(new Op('d', "2"));
            yield return CeCase(s);
            yield break;
        }
        id -= required.Length * 7;
        // A few hand made sequences with Paste.
        switch ((int)id)
        {
            case 0: yield return CeCase(PasteSeq("08:30")); break;
            case 1: yield return CeCase(PasteSeq("  1:15 ")); break;
            case 2: yield return CeCase(PasteSeq("1.5", "1,5", "8u30", "8")); break;
            case 3: yield return CeCase(PasteSeq("abc", "", "-1:30", "12345678", "1,2,3", "7:300")); break;
            case 4: yield return CeCase(PasteSeq("9999999:99", "=")); break;
            case 5: yield return CeCase(PasteSeq("9999999:99", "+")); break;
            case 6: yield return CeCase(PasteSeq(",5", ".5", ":30", ":", ",")); break;
            case 7: yield return CeCase(PasteSeq(":30", "=")); break;
            case 8: yield return CeCase(MixedSeq("7:30 + 1 =", "p:2:00", "k:+", "k:1", "k:=")); break;
            case 9: yield return CeCase(MixedSeq("7:30 +", "p:0:30", "k:=")); break;
            case 10: yield return CeCase(MixedSeq("7:30 / 0 =", "p:1:00")); break;
            case 11: yield return CeCase(MixedSeq("7:30 / 4 =", "d:4", "d:2", "d:6")); break;
            case 12: yield return CeCase(MixedSeq("1:00 / 3 =", "d:6", "k:*", "k:3", "k:=")); break;
            case 13: yield return CeCase(MixedSeq("7:3", "d:4")); break;
            case 14: yield return CeCase(MixedSeq("", "d:-1", "d:0", "d:1", "d:7", "d:100", "d:5")); break;
            case 15: yield return CeCase(MixedSeq("garbage")); break;
            case 16: yield return CeCase(MixedSeq("7:30 + 7:20 =", "k:c")); break;
            case 17: yield return CeCase(MixedSeq("7:30 + 7:20 =", "k:Back", "k:BACKSPACE", "k:ce")); break;
            default: break;
        }
    }

    static Seq PasteSeq(params string[] texts)
    {
        var s = new Seq { Places = -1, Ops = new List<Op>() };
        foreach (string t in texts)
        {
            if (t == "=" || t == "+") s.Ops.Add(new Op('k', t));
            else s.Ops.Add(new Op('p', t));
        }
        return s;
    }

    // keys string (as in SeqOfKeys) followed by prefixed ops: k:<key>, p:<text>, d:<n>
    static Seq MixedSeq(string keys, params string[] more)
    {
        Seq s = SeqOfKeys(-1, keys == "garbage" ? "" : keys);
        if (keys == "garbage")
        {
            s.Ops.Add(new Op('k', "garbage")); s.Ops.Add(new Op('k', "")); s.Ops.Add(new Op('k', "12"));
        }
        foreach (string m in more) s.Ops.Add(new Op(m[0], m.Substring(2)));
        return s;
    }

    // ---- calculator: exhaustive

    static readonly string[] FullKeys =
    {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ",", ".", "u", "U", "h", "H",
        "+", "-", "*", "/", "=", "CE", "C", "BACK", "Back", "BACKSPACE", "ce", "c", "x", ""
    };

    static readonly int[] PlacesCycle = { -1, 2, 3, 4, 5, 6, 1, 7 };

    static readonly string[] ReducedKeys = { "0", "1", "9", ":", ",", "+", "-", "*", "/", "=", "CE", "C" };

    static Suite CalcExhaustive(string name, string[] keys, int maxLen)
    {
        long total = 0, count = 1;
        for (int len = 1; len <= maxLen; len++) { count *= keys.Length; total += count; }
        return new Suite
        {
            Name = name,
            Count = total,
            Heavy = true,
            Gen = id => CalcExhaustiveCase(id, keys, maxLen)
        };
    }

    static IEnumerable<Case> CalcExhaustiveCase(long id, string[] keys, int maxLen)
    {
        int n = keys.Length;
        long i = id;
        for (int len = 1; len <= maxLen; len++)
        {
            long count = 1;
            for (int k = 0; k < len; k++) count *= n;
            if (i < count)
            {
                var ks = new string[len];
                long x = i;
                for (int k = len - 1; k >= 0; k--) { ks[k] = keys[(int)(x % n)]; x /= n; }
                // The number of places follows the position: default constructor, 2..6, and two values to clamp.
                yield return CeKeys(PlacesCycle[id % PlacesCycle.Length], ks);
                yield break;
            }
            i -= count;
        }
    }

    // ---- calculator: random

    static readonly string[] Digits = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    static readonly string[] SepKeys = { ":", ",", ".", "u", "U", "h", "H" };
    static readonly string[] OpKeys = { "+", "-", "*", "/" };
    static readonly string[] CtlKeys = { "CE", "C", "BACK", "Back", "BACKSPACE", "ce", "c" };
    static readonly string[] GarbageKeys = { "x", "", "12", "Ce", "++", "=="};
    static readonly string[] NumberPool =
    {
        "9999999", "9999999:99", "9999999:60", "9999999:59", "9999999,999999", "9999999,5", "0,000001", "0:01", "1", "60", "0:59", "99:99",
        "1234567", "7:30", "100", "3", "7", "6", "0", "0,5", "1:30", "2", "4", "0:05", "1,5", "5000000", "5000000:30"
    };
    static readonly string[] PastePool =
    {
        "08:30", "1:15", "1.5", "1,5", "8u30", "8", "  1:15 ", "\t7\n", ":30", ",5", ".5", ":", ",", "7:", "0:90", "9999999:99",
        "9999999:60", "9999999,999999", "0,000001", "123456", "1234567", "12:3", "abc", "", "-1:30", "12345678", "1,2,3", "7:300",
        "1:2:3", "7,1234567", "1.2.3", "1 2", "\x0663", "\x00A07:30\x2003", "7h", "H30", "u", "5.", "5,", "0,0", "00:00", "1e5", "+5"
    };
    static readonly string PasteAlphabet = "0123456789:,.uhUH x-";

    static void TypeNumber(Rng r, List<Op> ops)
    {
        if (r.Chance(45))
        {
            foreach (char c in r.Pick(NumberPool)) ops.Add(new Op('k', c.ToString()));
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

    static string RandomPaste(Rng r)
    {
        if (r.Chance(55)) return r.Pick(PastePool);
        var sb = new StringBuilder();
        int len = r.Int(11);
        for (int i = 0; i < len; i++) sb.Append(PasteAlphabet[r.Int(PasteAlphabet.Length)]);
        return sb.ToString();
    }

    static Seq RandomSeq(Rng r)
    {
        var s = new Seq { Ops = new List<Op>() };
        int pl = r.Int(100);
        s.Places = pl < 55 ? -1 : pl < 95 ? 2 + r.Int(5) : r.Pick(new[] { 0, 1, 7, -3, 100 });
        int len = 1 + r.Int(30);
        while (s.Ops.Count < len)
        {
            int c = r.Int(100);
            if (c < 28) s.Ops.Add(new Op('k', r.Pick(Digits)));
            else if (c < 36) s.Ops.Add(new Op('k', r.Pick(SepKeys)));
            else if (c < 52) s.Ops.Add(new Op('k', r.Pick(OpKeys)));
            else if (c < 62) s.Ops.Add(new Op('k', "="));
            else if (c < 67) s.Ops.Add(new Op('k', r.Pick(CtlKeys)));
            else if (c < 82) TypeNumber(r, s.Ops);
            else if (c < 90) s.Ops.Add(new Op('p', RandomPaste(r)));
            else if (c < 95)
            {
                int d = r.Chance(80) ? 2 + r.Int(5) : r.Pick(new[] { 0, 1, 7, -1, 100 });
                s.Ops.Add(new Op('d', I(d)));
            }
            else if (c < 98) s.Ops.Add(new Op('k', r.Pick(GarbageKeys)));
            else s.Ops.Add(new Op('k', r.Pick(FullKeys)));
        }
        if (s.Ops.Count > 30) s.Ops.RemoveRange(30, s.Ops.Count - 30);
        return s;
    }

    static Suite CalcRandom(long count, ulong seed)
    {
        return new Suite
        {
            Name = "calculator random sequences",
            Count = count,
            Heavy = true,
            Gen = id =>
            {
                var r = new Rng(seed * 1000003UL + (ulong)id * 7919UL + 12345UL);
                return new[] { CeCase(RandomSeq(r)) };
            }
        };
    }

    // ---- TimeMath grid

    static List<decimal> TimeValues()
    {
        var v = new List<decimal>();
        for (int i = -3000; i <= 3000; i++) v.Add(i);
        for (int k = -1600; k <= 1600; k++) v.Add(k / 16m);
        decimal[] eps = { 0m, 0.0000000001m, -0.0000000001m, 0.000000001m, -0.000000001m, 0.00000001m, -0.00000001m, 0.0000000000001m, -0.0000000000001m };
        for (int p = 2; p <= 6; p++)
        {
            decimal scale = 1m;
            for (int i = 0; i < p; i++) scale *= 10m;
            for (int k = 0; k <= 300; k++)
            {
                decimal m = 30m * (2 * k + 1) / scale;      // hours midpoint at p places, in minutes
                foreach (decimal e in eps) { v.Add(m + e); v.Add(-(m + e)); }
            }
        }
        for (int n = -200; n <= 200; n++)
            foreach (decimal e in eps) v.Add(n + 0.5m + e);
        decimal limit = 600000000m;
        decimal[] around = { -60m, -59.5m, -1m, -0.5m, -0.0001m, 0m, 0.0001m, 0.5m, 1m, 59.5m, 60m, -0.000001m, 0.000001m };
        foreach (decimal d in around) { v.Add(limit + d); v.Add(-(limit + d)); }
        decimal top = 9999999m * 60m + 59m;
        decimal[] topAdd = { 0m, 0.4999m, 0.5m, 0.99999m, 1m, -0.5m, -1m };
        foreach (decimal d in topAdd) { v.Add(top + d); v.Add(-(top + d)); }
        v.Add(9999999.999999m * 60m); v.Add(9999999.9999995m * 60m); v.Add(9999999.9999999m * 60m);
        v.Add(1000000000m); v.Add(10000000000m); v.Add(1000000000000m); v.Add(1000000000000000m); v.Add(1000000000000000000m);
        v.Add(9200000000000000000m); v.Add(9223372036854775807m); v.Add(9223372036854775808m); v.Add(-9223372036854775808m);
        v.Add(decimal.MaxValue); v.Add(decimal.MinValue); v.Add(decimal.MaxValue / 60m); v.Add(decimal.MaxValue / 3m);
        v.Add(1m / 3m); v.Add(2m / 3m); v.Add(1m / 7m); v.Add(100m / 7m); v.Add(1m / 60m); v.Add(59m / 60m); v.Add(-1m / 3m);
        v.Add(0.0000000000000000000000000001m); v.Add(-0.0000000000000000000000000001m); v.Add(0.5000000000000000000000000001m);
        v.Add(0.4999999999999999999999999999m); v.Add(7.50m); v.Add(0m); v.Add(0.00m); v.Add(-0.0m);
        var r = new Rng(20261010);
        for (int i = 0; i < 40000; i++) v.Add(RandDecimal(r));
        return v;
    }

    static Suite TimeMathGrid()
    {
        List<decimal> values = TimeValues();
        int[] places = { 2, 3, 4, 5, 6, -1, 0, 1, 7, 100, 4, 6, 2, 3, 5 };
        return new Suite
        {
            Name = "TimeMath and format grid",
            Count = values.Count,
            Gen = id => TimeCases(values[(int)id], (int)id, places)
        };
    }

    static IEnumerable<Case> TimeCases(decimal m, int idx, int[] places)
    {
        int p = places[idx % places.Length];
        int q = 2 + idx % 5;
        yield return TmRoundMin(m);
        yield return TmRoundH(m);
        yield return TmRoundHP(m, p);
        yield return TmHm(m);
        yield return TmDec(m);
        yield return TmDecP(m, p);
        yield return TmDecP(m, q);
        yield return TmWords(m);
        yield return TmFactor(m);
        yield return CvFormat(0, m);
        yield return CvFormat(1, m);
        yield return CvFormatP(0, m, q);
        yield return CvFormatP(1, m, p);
        if (idx % 50 == 0)
        {
            yield return TmClamp(p);
            foreach (Case c in DecSingle(m, idx)) yield return c;
        }
    }

    // TryParseEntry over a pile of entries.
    static Suite EntryGrid()
    {
        string alphabet = "0123456789:,.";
        long count = StringCount(alphabet, 4);
        return new Suite
        {
            Name = "TimeMath.TryParseEntry",
            Once = true,
            Count = count,
            Gen = id => new[] { TmParse(StringAt(id, alphabet, 4)) }
        };
    }

    // The remaining small public functions: IsDigit over every UTF-16 code unit, AllDigits, ClampDecimals,
    // LimitMinutes, the decimal separator and the constants.
    static Suite SmallFunctions()
    {
        string[] texts = { "", "0", "0123456789", "12a", "a12", "1 2", "\x0663", "-1", "1.5", "00", "\x00B2" };
        return new Suite
        {
            Name = "TimeMath small functions",
            Count = 65536 + 101 + texts.Length + 3,
            Gen = id => SmallCases(id, texts)
        };
    }

    static IEnumerable<Case> SmallCases(long id, string[] texts)
    {
        if (id < 65536)
        {
            char c = (char)id;
            yield return new Case("tm.isdigit " + Esc(c.ToString()), () => B(TimeMath.IsDigit(c)));
            yield break;
        }
        id -= 65536;
        if (id < 101) { yield return TmClamp((int)id - 50); yield break; }
        id -= 101;
        if (id < texts.Length)
        {
            string t = texts[id];
            yield return new Case("tm.alldigits " + Tok(t), () => B(TimeMath.AllDigits(t)));
            yield break;
        }
        id -= texts.Length;
        switch ((int)id)
        {
            case 0: yield return new Case("tm.limit", () => D(TimeMath.LimitMinutes)); break;
            case 1: yield return new Case("tm.sep", () => Tok(TimeMath.DecimalSeparator)); break;
            default:
                yield return new Case("tm.consts", () => string.Join(" ", new[] {
                    TimeMath.MaxHourDigits, TimeMath.MaxMinuteDigits, TimeMath.MaxDecimals, TimeMath.DefaultDecimals,
                    TimeMath.MinDisplayDecimals, TimeMath.MaxDisplayDecimals }.Select(x => I(x)).ToArray()));
                break;
        }
    }

    // TryGetMinutes on raw text, not only on text that went through the filter: the Right field guards
    // itself with AllDigits; the Left field does for text with a ':'. A text of anything but digits,
    // '.' and 'x' without a ':' is a FormatException in the reference (and a DecFormat here).
    static Suite RawGetMinutes()
    {
        string alphabet = "0123456789:.x";
        return new Suite
        {
            Name = "Converter.TryGetMinutes (raw text)",
            Count = StringCount(alphabet, 5),
            Heavy = true,
            Gen = id => { string t = StringAt(id, alphabet, 5); return new[] { CvGetMin(0, t), CvGetMin(1, t) }; }
        };
    }

    static Suite EntryList()
    {
        string[] entries =
        {
            "", "7:30", "7,5", "7.5", "8", "0:90", "7:3", "7:", ",", ".", ":", "7x", "7:300", "12345678", "1,2,3", "1234567", "1234567:99",
            "1234567,123456", "1234567,1234567", ":5", ",5", "9999999:99", "9999999,999999", "0,000001", "00000007", "x", " 7", "7 ", "\x0663",
            "1:2:3", "1.2,3", "7:99", "7:100", "0000000", "0000000:00", "0,0000000"
        };
        return new Suite
        {
            Name = "TimeMath.TryParseEntry (list)",
            Once = true,
            Count = entries.Length,
            Gen = id => new[] { TmParse(entries[id]) }
        };
    }

    // ---- System.Decimal arithmetic

    static Suite DecArith(long count)
    {
        return new Suite
        {
            Name = "Dec arithmetic vs System.Decimal",
            Once = true,
            Count = count,
            Gen = id =>
            {
                var r = new Rng(777UL + (ulong)id * 104729UL);
                decimal a = RandDecimal(r), b = RandDecimal(r);
                if (r.Chance(5)) b = 0m;
                if (r.Chance(5)) b = 60m;
                if (r.Chance(5)) { try { a = b * 60m; } catch (OverflowException) { } }
                var list = new List<Case>();
                list.AddRange(DecPair(a, b));
                list.AddRange(DecSingle(a, (int)id));
                // The shapes the calculator uses: acc * 60 / operand and acc * operand / 60.
                decimal ma = a, mb = b;
                list.Add(new Case("dec.mul " + D(ma) + " " + D(mb), () => D(ma * mb)));
                if (b != 0m)
                {
                    decimal t1 = 0, t2 = 0;
                    bool ok1 = true, ok2 = true;
                    try { t1 = a * 60m; } catch (OverflowException) { ok1 = false; }
                    if (ok1) list.AddRange(DecPair(t1, b));
                    try { t2 = a * b; } catch (OverflowException) { ok2 = false; }
                    if (ok2) list.AddRange(DecPair(t2, 60m));
                }
                return list;
            }
        };
    }

    // ---- decimal division with a divisor of more than 64 bits: the rounding of the runtime
    //
    // The runtime rounds such a quotient by comparing twice the remainder with the divisor, both shifted left until the top bit
    // of the divisor is set, limb by limb from the top; it loses the carry out of a low or middle limb that holds exactly
    // 0x80000000 (hc::Dec copies that, see RoundsUpWide in native\core\dec.cpp). Random operands hardly ever get there, so
    // these cases are built: a divisor D of 65 to 96 bits, a remainder R next to D / 2 (a few units either side, and the exact
    // half), the dividend q * D + R with q 0 or 1, at scale 28 so that the rounding decides the last digit. The limbs of the shifted
    // D and R are taken from the values around the carry (0, 1, 2^31 - 1, 2^31, 2^31 + 1, 2^32 - 2, 2^32 - 1) or at random.

    struct U96
    {
        public ulong Lo;
        public uint Hi;
        public U96(uint hi, ulong lo) { Hi = hi; Lo = lo; }
        public static U96 Limbs(uint l2, uint l1, uint l0) { return new U96(l2, ((ulong)l1 << 32) | l0); }
        public static U96 operator +(U96 a, U96 b)
        {
            ulong lo = a.Lo + b.Lo;
            return new U96(a.Hi + b.Hi + (lo < a.Lo ? 1u : 0u), lo);
        }
        public static U96 operator -(U96 a, U96 b)
        {
            ulong lo = a.Lo - b.Lo;
            return new U96(a.Hi - b.Hi - (a.Lo < b.Lo ? 1u : 0u), lo);
        }
        public U96 Shr(int s)        // 0 .. 31
        {
            if (s == 0) return this;
            return new U96(Hi >> s, (Lo >> s) | ((ulong)Hi << (64 - s)));
        }
        public U96 Shl(int s)        // 0 .. 31
        {
            if (s == 0) return this;
            return new U96((Hi << s) | (uint)(Lo >> (64 - s)), Lo << s);
        }
        public bool Less(U96 o) { return Hi != o.Hi ? Hi < o.Hi : Lo < o.Lo; }
        public bool IsZero { get { return Hi == 0 && Lo == 0; } }
        public int Top { get { return Hi == 0 ? (Lo >> 32 == 0 ? 0 : 1) : 2; } }
        public decimal ToDecimal(byte scale, bool negative)
        {
            return new decimal((int)(uint)Lo, (int)(uint)(Lo >> 32), (int)Hi, negative, scale);
        }
    }

    static readonly uint[] LimbSpecials = { 0u, 1u, 2u, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu };

    static uint LimbPick(Rng r)
    {
        return r.Chance(75) ? r.Pick(LimbSpecials) : (uint)r.Next();
    }

    static Suite DecDivWide(long count)
    {
        return new Suite
        {
            Name = "Dec division, divisor over 64 bits, near one half",
            Once = true,
            Count = count,
            Gen = id =>
            {
                var r = new Rng(31337UL + (ulong)id * 7919UL);
                var list = new List<Case>();
                for (int attempt = 0; attempt < 8 && list.Count == 0; attempt++)
                {
                    int shift = r.Int(32);                          // D has 96 - shift bits: 65 .. 96
                    U96 dn, rn;                                      // D and the remainder shifted left by 'shift' bits
                    uint low = shift == 0 ? 0u : (uint)((1UL << shift) - 1);
                    int delta = r.Int(7) - 3;
                    if (r.Chance(15)) delta = r.Int(140001) - 70000;
                    if (r.Chance(60))
                    {
                        // D first, R next to D / 2
                        uint d2 = r.Chance(50) ? (0x80000000u | (uint)r.Next()) : r.Pick(new[] { 0x80000000u, 0x80000001u, 0xC0000000u, 0xFFFFFFFFu, 0xFFFFFFFEu });
                        uint d1 = LimbPick(r), d0 = LimbPick(r);
                        dn = U96.Limbs(d2, d1, d0);
                        dn.Lo &= ~(ulong)low;                       // exact once the shift is undone
                        if (dn.IsZero || !(dn.Hi >= 0x80000000u)) continue;
                        U96 half = new U96(dn.Hi >> 1, (dn.Lo >> 1) | ((ulong)(dn.Hi & 1) << 63));
                        rn = delta >= 0 ? half + U96.Limbs(0, 0, (uint)delta).Shl(shift) : half - U96.Limbs(0, 0, (uint)(-delta)).Shl(shift);
                        rn.Lo &= ~(ulong)low;
                    }
                    else
                    {
                        // R first (limbs from the special values), D twice R plus a little
                        uint r2 = r.Pick(new[] { 0x40000000u, 0x40000001u, 0x3FFFFFFFu, 0x7FFFFFFFu, 0x20000000u, 0x7FFFFFFEu });
                        rn = U96.Limbs(r2, LimbPick(r), LimbPick(r));
                        rn.Lo &= ~(ulong)low;
                        U96 twice = new U96((rn.Hi << 1) | (uint)(rn.Lo >> 63), rn.Lo << 1);
                        dn = delta >= 0 ? twice + U96.Limbs(0, 0, (uint)delta).Shl(shift) : twice - U96.Limbs(0, 0, (uint)(-delta)).Shl(shift);
                        dn.Lo &= ~(ulong)low;
                        if (!(dn.Hi >= 0x80000000u)) continue;
                    }
                    U96 d = dn.Shr(shift), rem = rn.Shr(shift);
                    if (!rem.Less(d) || d.Top < 2) continue;         // R below D, D of more than 64 bits
                    // an odd quotient (1) when q * D + R fits in 96 bits
                    ulong hiSum = (ulong)d.Hi + rem.Hi + ((d.Lo + rem.Lo) < d.Lo ? 1UL : 0UL);
                    bool odd = r.Chance(50) && hiSum <= uint.MaxValue;
                    U96 dividend = odd ? d + rem : rem;
                    bool na = r.Chance(50), nb = r.Chance(50);
                    decimal a = dividend.ToDecimal(28, na), b = d.ToDecimal(0, nb);
                    list.Add(DecBin("dec.div", a, b, () => D(a / b)));
                }
                return list;
            }
        };
    }

    // ---- Settings

    static readonly string[] SettingsFixed =
    {
        "", "DecimalPlaces=3", "DecimalPlaces=4\r\n", "  decimalplaces = 6  ", "; comment\n# another\n[HourCalc]\nDecimalPlaces=5\n",
        "Other=4", "DecimalPlaces=7", "DecimalPlaces=1", "DecimalPlaces=-3", "DecimalPlaces=abc", "DecimalPlaces=", "DecimalPlaces=3.5",
        "DecimalPlaces=3\nDecimalPlaces=5", "DecimalPlaces=2", "DecimalPlaces=02", "DecimalPlaces=000000000000000000000006",
        "DecimalPlaces=99999999999", "DecimalPlaces=+3", "DecimalPlaces=3\0", "DecimalPlaces=3\0\0", "DecimalPlaces=\03", "DecimalPlaces=3 \0",
        "DecimalPlaces=3\0 ", "DecimalPlaces=\x0663", "DecimalPlaces=\xFF13", "DecimalPlaces==3", "=3", " =3", "DecimalPlaces",
        "DecimalPlaces\t=\t4", "DecimalPlaces =4", "Decimal Places=4", "DecimalPlaces=3 ; comment", "DecimalPlaces=3;", "[DecimalPlaces=3]",
        ";DecimalPlaces=3", "#DecimalPlaces=3", "  ;DecimalPlaces=3", "x\nDecimalPlaces=4", "DecimalPlaces=1\nDecimalPlaces=4", "\nDecimalPlaces=5",
        "\r\nDecimalPlaces=5\r\n", "DecimalPlaces=5\r", "DecimalPlaces=5\r\rDecimalPlaces=3", "DecimalPlaces=4\n\n\n", "\xFEFFDecimalPlaces=4",
        "\x00A0DecimalPlaces=4\x00A0", "\x2003DecimalPlaces\x2003=\x20034\x2003", "\x180EDecimalPlaces=4", "\u0085DecimalPlaces=4", "\x2028DecimalPlaces=4",
        "Dec\x0131malPlaces=4", "DEC\x0130MALPLACES=4", "DecimalPlace\x017F=4", "DecimalPlaces=4\x200B", "DecimalPlaces=0x4", "DecimalPlaces=4e0", "DecimalPlaces=4.0",
        "DecimalPlaces=\t4", "DecimalPlaces=4\t", "DECIMALPLACES=2", "dEcImAlPlAcEs=6", "DecimalPlaces = 3 = 4", "DecimalPlaces=3=4",
        "DecimalPlaces=2147483647", "DecimalPlaces=2147483648", "DecimalPlaces=0000000000000000000000000000000000000000000000000000000000004"
    };

    // The theme: the same line rules as DecimalPlaces (first line with the key decides, case-insensitive key and value, trimmed),
    // values auto|light|dark, anything else is auto; and both keys in one text, in either order, repeated, with invalid values.
    static readonly string[] ThemeFixed =
    {
        "Theme=auto", "Theme=light", "Theme=dark", "THEME=DARK", "tHeMe=LiGhT", "theme=AUTO", "  theme = Light  ", "Theme=dark\r\n", "\r\nTheme=light\r\n",
        "Theme=blue", "Theme=", "Theme", "=dark", "Theme==dark", " =dark", "Theme=dark=light", "Theme = dark = light", "Theme=dark light", "Theme=dark;", "Theme=dark ; comment",
        "Theme=dark\nTheme=light", "Theme=light\nTheme=dark", "Theme=blue\nTheme=dark", "Theme=\nTheme=dark", "Theme=dark\nTheme=blue", "x\nTheme=light", "\nTheme=light",
        "Theme=dark\0", "Theme=dark\0\0", "Theme=\0dark", "Theme=dar\0k", "Theme=\0", "Theme=dark \0", "Theme=dark\0 ", "Theme=darker", "Theme=dar", "Theme=lig ht", "Theme=ligh",
        "Theme=l\x0131ght", "Theme=L\x0130GHT", "Theme=light\x0130", "Theme=dar\x212A", "Theme=DAR\x212A", "Theme=\x212Aight", "Theme=\x017Fight", "Them\x017F=dark",
        "Theme=dark\x200B", "\x200BTheme=dark", "Theme=\x2003" + "dark\x2003", "\x2003Theme\x2003=\x2003" + "dark\x2003", "\x00A0Theme=light\x00A0", "\x180ETheme=dark", "\u0085Theme=dark",
        "\x2028Theme=dark", "\xFEFFTheme=dark", "Theme=\xFF24\xFF21\xFF32\xFF2B", "Theme=0", "Theme=1", "Theme=2", "Theme=true", "Theme=Dark Mode", "Theme=\"dark\"", "Theme='dark'",
        ";Theme=dark", "#Theme=dark", "  ;Theme=dark", "[Theme=dark]", "[Theme]\nDark=1", "[Display]\nTheme=dark\n[Other]\nTheme=light", "Themes=dark", "Theme2=dark", "MyTheme=dark",
        "Theme\t=\tdark", "Theme =dark", "The me=dark", "Theme=dark\r", "Theme=dark\r\rTheme=light", "Theme=dark\n\n\n", "Theme=dark\rTheme=light", "THEME=light\nDecimalPlaces=3",
        "DecimalPlaces=4\r\nTheme=dark\r\n", "Theme=light\nDecimalPlaces=5", "Theme=dark\nDecimalPlaces=3\nTheme=light\nDecimalPlaces=6", "DecimalPlaces=3\nTheme=blue",
        "DecimalPlaces=9\nTheme=dark", "DecimalPlaces=2\nTheme=auto\nDecimalPlaces=5\nTheme=dark", "Theme=blue\nDecimalPlaces=4\nTheme=light", "DecimalPlaces=\nTheme=dark",
        "Theme=\nDecimalPlaces=6", "DecimalPlaces=6\r\nTheme=Light\r\n; the rest\r\n", "; HourCalc.ini\r\n[HourCalc]\r\nDecimalPlaces=3\r\nTheme=Dark\r\n", "Theme=dark;DecimalPlaces=3",
        "DecimalPlaces=3 Theme=dark", "DecimalPlaces=3\rTheme=dark", "DecimalPlaces=3\n\rTheme=dark"
    };

    static readonly string[] SettingsFragments =
    {
        "DecimalPlaces", "decimalplaces", "DECIMALPLACES", "=", " ", "\t", "\r", "\n", "\n", "3", "6", "7", "1", "2", "4", "5", "0", "-", "+", ";", "#",
        "[", "]", "\0", "\x00A0", "\x2003", "x", "\x0131", "\x212A", "\xFF13", "\x0663", "\u0085", "\xFEFF", ".", ",", "Decimal", "Places", "DecimalPlaces=",
        "Theme", "theme", "THEME", "Theme=", "\nTheme=", "auto", "light", "dark", "Light", "DARK", "Auto", "\n", "=", " "
    };

    // Every fixed text goes through both parsers, and so does every random one (a random text has both keys in it often enough).
    static Suite SettingsParse(long count)
    {
        string[] fixedTexts = SettingsFixed.Concat(ThemeFixed).ToArray();
        return new Suite
        {
            Name = "Settings.ParseDecimalPlaces and ParseTheme",
            Once = true,
            Count = fixedTexts.Length + count,
            Gen = id =>
            {
                if (id < fixedTexts.Length) return new[] { StParse(fixedTexts[id]), StParseTheme(fixedTexts[id]) };
                var r = new Rng(31337UL + (ulong)id * 15485863UL);
                var sb = new StringBuilder();
                int n = 1 + r.Int(10);
                for (int i = 0; i < n; i++) sb.Append(r.Pick(SettingsFragments));
                string text = sb.ToString();
                return new[] { StParse(text), StParseTheme(text) };
            }
        };
    }

    // ---------------------------------------------------------------- Settings: files and the default path

    static int SettingsFileChecks(CultureInfo ci)
    {
        int mismatches = 0;
        int checks = 0;
        string dir = Path.Combine(Path.GetTempPath(), "HourCalcDiff-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        _dir = dir;
        _ndir = _linux ? _pool[0].Query("fs.mktemp") : dir;
        try
        {
            {
                Cli cli = _pool[0];
                Action<string, string, string> check = (what, exp, got) =>
                {
                    checks++;
                    if (exp != got)
                    {
                        mismatches++;
                        Console.WriteLine("  MISMATCH settings files: {0}\n    oracle: {1}\n    native: {2}", what, exp, got);
                    }
                };
                string pathBase = Path.Combine(dir, "t.ini");
                // A file is read three ways: the places alone, the theme alone, and both in one read.
                Action<string, string> checkFile = (what, path) =>
                {
                    string q = Esc(N(path));
                    check(what + " (places)", I(Settings.LoadDecimalPlaces(path)), cli.Query("st.load " + q));
                    check(what + " (theme)", I((int)Settings.LoadTheme(path)), cli.Query("st.loadtheme " + q));
                    int p;
                    ThemeMode t;
                    Settings.Load(path, out p, out t);
                    check(what + " (both)", I(p) + " " + I((int)t), cli.Query("st.loadall " + q));
                };
                string[] texts =
                {
                    "DecimalPlaces=3", "DecimalPlaces=4\r\n", "[a]\n;x\nDecimalPlaces=6\n", "junk", "DecimalPlaces=9", "", "DecimalPlaces=5\n\x00E9=1",
                    "Theme=dark", "Theme=Light\r\n", "DecimalPlaces=4\r\nTheme=dark\r\n", "Theme=light\nDecimalPlaces=5", "Theme=blue\nDecimalPlaces=3", "THEME=DARK\n\x00E9=1\nTheme=light",
                    "Theme=dark\nTheme=light\nDecimalPlaces=6\nDecimalPlaces=3"
                };
                var encodings = new Encoding[]
                {
                    new UTF8Encoding(false), new UTF8Encoding(true), new UnicodeEncoding(false, true), new UnicodeEncoding(true, true),
                    new UTF32Encoding(false, true), new UTF32Encoding(true, true), Encoding.ASCII
                };
                int n = 0;
                foreach (string text in texts)
                    foreach (Encoding enc in encodings)
                    {
                        string path = pathBase + (n++);
                        byte[] pre = enc.GetPreamble();
                        byte[] body = enc.GetBytes(text);
                        byte[] all = new byte[pre.Length + body.Length];
                        Buffer.BlockCopy(pre, 0, all, 0, pre.Length);
                        Buffer.BlockCopy(body, 0, all, pre.Length, body.Length);
                        WriteBoth(path, all);
                        checkFile("load " + enc.WebName + " " + Esc(text), path);
                    }
                // Raw bytes: invalid UTF-8 around the line, a lone BOM, a file of NULs.
                byte[][] raws =
                {
                    new byte[] { 0xEF, 0xBB, 0xBF }, new byte[] { 0xFF, 0xFE }, new byte[] { 0xFE, 0xFF }, new byte[] { 0, 0 }, new byte[] { 0xC3 },
                    new byte[] { 0xFF, 0xFE, 0x0A, 0x44, 0x65, 0x63, 0x69, 0x6D, 0x61, 0x6C, 0x50, 0x6C, 0x61, 0x63, 0x65, 0x73, 0x3D, 0x33 },
                    new byte[] { 0xFF, 0xFE, 0x44 },
                    new byte[] { 0xFF, 0xFE, 0x0A, 0x54, 0x68, 0x65, 0x6D, 0x65, 0x3D, 0x64, 0x61, 0x72, 0x6B },
                    new byte[] { 0xEF, 0xBB, 0xBF, 0x54, 0x68, 0x65, 0x6D, 0x65, 0x3D, 0x64, 0x61, 0x72, 0x6B, 0xC3 }
                };
                foreach (byte[] raw in raws)
                {
                    string path = pathBase + (n++);
                    WriteBoth(path, raw);
                    checkFile("load raw " + BitConverter.ToString(raw), path);
                }
                // Missing file, a folder, an empty path.
                string missing = Path.Combine(dir, "missing.ini");
                check("load missing", I(Settings.LoadDecimalPlaces(missing)), cli.Query("st.load " + Esc(N(missing))));
                check("load missing (theme)", I((int)Settings.LoadTheme(missing)), cli.Query("st.loadtheme " + Esc(N(missing))));
                check("load missing (both)", "2 0", cli.Query("st.loadall " + Esc(N(missing))));
                check("load a folder", I(Settings.LoadDecimalPlaces(dir)), cli.Query("st.load " + Esc(N(dir))));
                check("load a folder (theme)", I((int)Settings.LoadTheme(dir)), cli.Query("st.loadtheme " + Esc(N(dir))));
                check("load a folder (both)", "2 0", cli.Query("st.loadall " + Esc(N(dir))));
                check("load empty path", I(Settings.LoadDecimalPlaces("")), cli.Query("st.load \\e"));
                check("load empty path (theme)", I((int)Settings.LoadTheme("")), cli.Query("st.loadtheme \\e"));
                check("load empty path (both)", "2 0", cli.Query("st.loadall \\e"));
                // Save (the combined save is the only one): same bytes, same result, for every place count and theme, also those
                // outside the range and outside the enumeration.
                foreach (int places in new[] { -5, 0, 1, 2, 3, 4, 5, 6, 7, 100 })
                    foreach (int theme in new[] { 0, 1, 2, -1, 3, 7, 100 })
                    {
                        string tag = places.ToString(Inv).Replace("-", "m") + "-" + theme.ToString(Inv).Replace("-", "m");
                        string a = Path.Combine(dir, "o" + tag + ".ini");
                        string b = Path.Combine(dir, "n" + tag + ".ini");
                        bool ro = Settings.Save(a, places, (ThemeMode)theme);
                        string rn = cli.Query("st.save " + Esc(N(b)) + " " + places + " " + theme);
                        check("save " + places + " " + theme + " result", B(ro), rn);
                        check("save " + places + " " + theme + " bytes", BitConverter.ToString(File.ReadAllBytes(a)), NBytes(b));
                        // each reads the file that the other one wrote
                        string bHere = LocalCopyOfNative(b);
                        check("save " + places + " " + theme + " load", I(Settings.LoadDecimalPlaces(bHere)) + " " + I((int)Settings.LoadTheme(bHere)), cli.Query("st.loadall " + Esc(N(NativeCopyOfLocal(a)))));
                    }
                foreach (int theme in new[] { 0, 1, 2, -1, 3, 7, 100 })
                    check("theme string " + theme, Settings.ThemeToString((ThemeMode)theme), Unesc(cli.Query("st.themestr " + theme)));
                // Both settings survive a change of one: read both, change one, save both, read both again.
                {
                    string a = Path.Combine(dir, "keep-o.ini");
                    string b = Path.Combine(dir, "keep-n.ini");
                    Settings.Save(a, 5, ThemeMode.Dark);
                    NWrite(b, File.ReadAllBytes(a));                  // the same starting file for the native program
                    for (int step = 0; step < 6; step++)
                    {
                        int pa;
                        ThemeMode ta;
                        Settings.Load(a, out pa, out ta);
                        string[] got = cli.Query("st.loadall " + Esc(N(b))).Split(' ');
                        check("keep both, step " + step + " (read)", I(pa) + " " + I((int)ta), got[0] + " " + got[1]);
                        int pb = int.Parse(got[0], Inv);
                        ThemeMode tb = (ThemeMode)int.Parse(got[1], Inv);
                        if (step % 2 == 0) { pa = 2 + step % 5; pb = pa; } else { ta = (ThemeMode)((step / 2 + 1) % 3); tb = ta; }
                        Settings.Save(a, pa, ta);
                        cli.Query("st.save " + Esc(N(b)) + " " + pb + " " + (int)tb);
                        check("keep both, step " + step + " (bytes)", BitConverter.ToString(File.ReadAllBytes(a)), NBytes(b));
                    }
                }
                // Overwrite a longer file.
                string longFile = Path.Combine(dir, "long.ini");
                File.WriteAllText(longFile, "DecimalPlaces=3\r\nTheme=dark\r\nsome more text that must disappear\r\n");
                string longFile2 = Path.Combine(dir, "long2.ini");
                NWriteText(longFile2, "DecimalPlaces=3\r\nTheme=dark\r\nsome more text that must disappear\r\n");
                Settings.Save(longFile, 4, ThemeMode.Light);
                cli.Query("st.save " + Esc(N(longFile2)) + " 4 1");
                check("save overwrites", BitConverter.ToString(File.ReadAllBytes(longFile)), NBytes(longFile2));
                // The folder is made by the first save, one level (SPEC 6c): the same result and bytes, and a chain of missing folders is not made.
                {
                    string oneA = Path.Combine(dir, "one-o", "HourCalc.ini"), oneB = Path.Combine(dir, "one-n", "HourCalc.ini");
                    check("save in a missing folder (one level)", B(Settings.Save(oneA, 3, ThemeMode.Dark)), cli.Query("st.save " + Esc(N(oneB)) + " 3 2"));
                    check("... the bytes", BitConverter.ToString(File.ReadAllBytes(oneA)), NBytes(oneB));
                    string tA = Path.Combine(dir, "oneT-o", "HourCalc.ini"), tB = Path.Combine(dir, "oneT-n", "HourCalc.ini");
                    check("SaveTheme in a missing folder", B(Settings.SaveTheme(tA, ThemeMode.Light, 4)), cli.Query("st.savetheme " + Esc(N(tB)) + " 1 4"));
                    check("... the bytes", BitConverter.ToString(File.ReadAllBytes(tA)), NBytes(tB));
                    string pA = Path.Combine(dir, "oneP-o", "HourCalc.ini"), pB = Path.Combine(dir, "oneP-n", "HourCalc.ini");
                    check("SavePlaces in a missing folder", B(Settings.SavePlaces(pA, 6, ThemeMode.Dark)), cli.Query("st.saveplaces " + Esc(N(pB)) + " 6 2"));
                    check("... the bytes", BitConverter.ToString(File.ReadAllBytes(pA)), NBytes(pB));
                    string deepA = Path.Combine(dir, "deep-o", "x", "HourCalc.ini"), deepB = Path.Combine(dir, "deep-n", "x", "HourCalc.ini");
                    check("save in a chain of missing folders", B(Settings.Save(deepA, 3, ThemeMode.Dark)), cli.Query("st.save " + Esc(N(deepB)) + " 3 2"));
                    check("... no folder is made", B(Directory.Exists(Path.Combine(dir, "deep-o"))), B(NDirExists(Path.Combine(dir, "deep-n"))));
                    check("SaveTheme in a chain of missing folders", B(Settings.SaveTheme(deepA, ThemeMode.Dark, 3)), cli.Query("st.savetheme " + Esc(N(deepB)) + " 2 3"));
                    check("SavePlaces in a chain of missing folders", B(Settings.SavePlaces(deepA, 3, ThemeMode.Dark)), cli.Query("st.saveplaces " + Esc(N(deepB)) + " 3 2"));
                }
                check("save to a folder", B(Settings.Save(dir, 3, ThemeMode.Dark)), cli.Query("st.save " + Esc(N(dir)) + " 3 2"));
                check("SaveTheme on a folder", B(Settings.SaveTheme(dir, ThemeMode.Dark, 3)), cli.Query("st.savetheme " + Esc(N(dir)) + " 2 3"));
                check("SavePlaces on a folder", B(Settings.SavePlaces(dir, 3, ThemeMode.Dark)), cli.Query("st.saveplaces " + Esc(N(dir)) + " 3 2"));
                check("save to an empty path", B(Settings.Save("", 3, ThemeMode.Dark)), cli.Query("st.save \\e 3 2"));
                check("SaveTheme to an empty path", B(Settings.SaveTheme("", ThemeMode.Dark, 3)), cli.Query("st.savetheme \\e 2 3"));
                check("SavePlaces to an empty path", B(Settings.SavePlaces("", 3, ThemeMode.Dark)), cli.Query("st.saveplaces \\e 3 2"));
                check("save to null", B(Settings.Save(null, 3, ThemeMode.Dark)), B(false));
                check("file name", _linux ? "hourcalc.ini" : Settings.FileName, Unesc(cli.Query("st.file")));
                // A single setting changed: the file is read again for the other one. Every kind of file that can be there, every
                // value (also the invalid ones), the other setting of the caller (used only without a file): same result, same bytes.
                {
                    string[] startTexts =
                    {
                        null, "", "DecimalPlaces=5\r\nTheme=dark\r\n", "DecimalPlaces=6", "Theme=light", "junk", "Theme=blue\nDecimalPlaces=9",
                        "DecimalPlaces=3\nTheme=Dark\nmore text", "Theme=dark\nTheme=light\nDecimalPlaces=4", "\uFEFFTheme=light"
                    };
                    int seq = 0;
                    foreach (string start in startTexts)
                    {
                        foreach (int placesValue in new[] { 2, 3, 6, 7, -1, 100 })
                            foreach (int otherTheme in new[] { 0, 1, 2, 9 })
                            {
                                string a = Path.Combine(dir, "spo" + seq + ".ini"), b = Path.Combine(dir, "spn" + seq + ".ini");
                                seq++;
                                if (start != null) { File.WriteAllText(a, start); NWriteText(b, start); }
                                string what = "SavePlaces " + placesValue + " (theme " + otherTheme + ") over " + (start == null ? "no file" : Esc(start));
                                check(what + " result", B(Settings.SavePlaces(a, placesValue, (ThemeMode)otherTheme)), cli.Query("st.saveplaces " + Esc(N(b)) + " " + placesValue + " " + otherTheme));
                                check(what + " bytes", File.Exists(a) ? BitConverter.ToString(File.ReadAllBytes(a)) : "(none)", NBytes(b));
                            }
                        foreach (int themeValue in new[] { 0, 1, 2, -1, 3, 100 })
                            foreach (int otherPlaces in new[] { 2, 4, 6, 8 })
                            {
                                string a = Path.Combine(dir, "spo" + seq + ".ini"), b = Path.Combine(dir, "spn" + seq + ".ini");
                                seq++;
                                if (start != null) { File.WriteAllText(a, start); NWriteText(b, start); }
                                string what = "SaveTheme " + themeValue + " (places " + otherPlaces + ") over " + (start == null ? "no file" : Esc(start));
                                check(what + " result", B(Settings.SaveTheme(a, (ThemeMode)themeValue, otherPlaces)), cli.Query("st.savetheme " + Esc(N(b)) + " " + themeValue + " " + otherPlaces));
                                check(what + " bytes", File.Exists(a) ? BitConverter.ToString(File.ReadAllBytes(a)) : "(none)", NBytes(b));
                            }
                    }
                }
                // Two HourCalc windows (SPEC 6b: changing one setting never loses the other, across processes): A (stale) picks a
                // theme after B changed the places, and the other way round. The caller's own copy of the other setting is out of date.
                {
                    string a = Path.Combine(dir, "two-o.ini"), b = Path.Combine(dir, "two-n.ini");
                    Func<string, string> bytesOf = p => File.Exists(p) ? BitConverter.ToString(File.ReadAllBytes(p)) : "(none)";
                    check("two windows: A picks Dark", B(Settings.SaveTheme(a, ThemeMode.Dark, 2)), cli.Query("st.savetheme " + Esc(N(b)) + " 2 2"));
                    check("two windows: B picks 4 places (its copy of the theme is Auto)", B(Settings.SavePlaces(a, 4, ThemeMode.Auto)), cli.Query("st.saveplaces " + Esc(N(b)) + " 4 0"));
                    check("two windows: both settings are in the file", bytesOf(a), NBytes(b));
                    check("two windows: ... the Dark of A and the 4 places of B", "4 2", cli.Query("st.loadall " + Esc(N(b))));
                    check("two windows: A (its copy of the places is 2) picks Light", B(Settings.SaveTheme(a, ThemeMode.Light, 2)), cli.Query("st.savetheme " + Esc(N(b)) + " 1 2"));
                    check("two windows: B (its copy of the theme is Auto) picks 6 places", B(Settings.SavePlaces(a, 6, ThemeMode.Auto)), cli.Query("st.saveplaces " + Esc(N(b)) + " 6 0"));
                    check("two windows: ... the Light of A and the 6 places of B", "6 1", cli.Query("st.loadall " + Esc(N(b))));
                    check("two windows: same bytes", bytesOf(a), NBytes(b));
                }
                // A file that is there but cannot be read (locked, as an editor or a virus scanner may do at the moment the program
                // starts): nothing is written by either, the file stays as it is, and afterwards the other setting is still the one on disk.
                // On Linux nobody locks a file against readers; the same situation is a file without any permission bit.
                if (!_linux)
                {
                    string a = Path.Combine(dir, "lock-o.ini"), b = Path.Combine(dir, "lock-n.ini");
                    File.WriteAllText(a, "DecimalPlaces=5\r\nTheme=dark\r\n");
                    File.WriteAllText(b, "DecimalPlaces=5\r\nTheme=dark\r\n");
                    using (new FileStream(a, FileMode.Open, FileAccess.Read, FileShare.None))
                    using (new FileStream(b, FileMode.Open, FileAccess.Read, FileShare.None))
                    {
                        check("locked: load (both)", "2 0", cli.Query("st.loadall " + Esc(b)));
                        check("locked: SaveTheme", B(Settings.SaveTheme(a, ThemeMode.Light, 2)), cli.Query("st.savetheme " + Esc(b) + " 1 2"));
                        check("locked: SavePlaces", B(Settings.SavePlaces(a, 3, ThemeMode.Auto)), cli.Query("st.saveplaces " + Esc(b) + " 3 0"));
                        check("locked: Save", B(Settings.Save(a, 3, ThemeMode.Auto)), cli.Query("st.save " + Esc(b) + " 3 0"));
                    }
                    check("locked: the files are as they were", BitConverter.ToString(File.ReadAllBytes(a)), BitConverter.ToString(File.ReadAllBytes(b)));
                    check("locked: ... both as they were", "DecimalPlaces=5\r\nTheme=dark\r\n", File.ReadAllText(b));
                    check("unlocked: SaveTheme", B(Settings.SaveTheme(a, ThemeMode.Light, 2)), cli.Query("st.savetheme " + Esc(b) + " 1 2"));
                    check("unlocked: ... keeps the 5 places the failed read never saw", BitConverter.ToString(File.ReadAllBytes(a)), BitConverter.ToString(File.ReadAllBytes(b)));
                    check("unlocked: ... in both", "5 1", cli.Query("st.loadall " + Esc(b)));
                    check("locked: no temporary file is left behind", "0", TmpCount(dir).ToString());
                }
                else
                {
                    string b = Path.Combine(dir, "lock-n.ini");
                    string content = "DecimalPlaces=5\r\nTheme=dark\r\n";
                    NWriteText(b, content);
                    cli.Query("fs.chmod " + Esc(N(b)) + " 0");
                    bool root = NRead(b) != null;                // root reads every file
                    if (!root)
                    {
                        check("unreadable: load (both)", "2 0", cli.Query("st.loadall " + Esc(N(b))));
                        check("unreadable: SaveTheme", "0", cli.Query("st.savetheme " + Esc(N(b)) + " 1 2"));
                        check("unreadable: SavePlaces", "0", cli.Query("st.saveplaces " + Esc(N(b)) + " 3 0"));
                        check("unreadable: Save", "0", cli.Query("st.save " + Esc(N(b)) + " 3 0"));
                    }
                    cli.Query("fs.chmod " + Esc(N(b)) + " 644");
                    check("unreadable: the file is as it was", content, Encoding.ASCII.GetString(NRead(b)));
                    check("readable again: SaveTheme", "1", cli.Query("st.savetheme " + Esc(N(b)) + " 1 2"));
                    check("readable again: ... keeps the 5 places the failed read never saw", "5 1", cli.Query("st.loadall " + Esc(N(b))));
                    check("unreadable: no temporary file is left behind", "0", TmpCount(dir).ToString());
                }
                // Attributes of the settings file: hidden and system are replaced (the file is saved), read-only is the user's "do not
                // change this" and is not. On Linux the native file gets permission bits instead (hidden 600, system 640, both 660, read-only
                // 444, archive 644); a file without any write bit is the read-only one.
                foreach (FileAttributes attr in new[] { FileAttributes.Hidden, FileAttributes.System, FileAttributes.Hidden | FileAttributes.System, FileAttributes.ReadOnly, FileAttributes.Archive })
                    foreach (int op in new[] { 0, 1, 2 })
                    {
                        string a = Path.Combine(dir, "at-o.ini"), b = Path.Combine(dir, "at-n.ini");
                        string mode = attr == FileAttributes.ReadOnly ? "444" : attr == FileAttributes.Hidden ? "600" : attr == FileAttributes.System ? "640" : attr == (FileAttributes.Hidden | FileAttributes.System) ? "660" : "644";
                        foreach (string p in new[] { a, b })
                        {
                            if (p == b && _linux)
                            {
                                NWriteText(b, "DecimalPlaces=5\r\nTheme=dark\r\n");
                                cli.Query("fs.chmod " + Esc(N(b)) + " " + mode);
                                continue;
                            }
                            if (File.Exists(p)) File.SetAttributes(p, FileAttributes.Normal);
                            File.WriteAllText(p, "DecimalPlaces=5\r\nTheme=dark\r\n");
                            File.SetAttributes(p, attr);
                        }
                        string ro, rn;
                        if (op == 0) { ro = B(Settings.Save(a, 3, ThemeMode.Light)); rn = cli.Query("st.save " + Esc(N(b)) + " 3 1"); }
                        else if (op == 1) { ro = B(Settings.SaveTheme(a, ThemeMode.Light, 3)); rn = cli.Query("st.savetheme " + Esc(N(b)) + " 1 3"); }
                        else { ro = B(Settings.SavePlaces(a, 3, ThemeMode.Light)); rn = cli.Query("st.saveplaces " + Esc(N(b)) + " 3 1"); }
                        File.SetAttributes(a, FileAttributes.Normal);
                        if (_linux) cli.Query("fs.chmod " + Esc(N(b)) + " 644");
                        else File.SetAttributes(b, FileAttributes.Normal);
                        string what = "attributes " + attr + " op " + op;
                        check(what + " result", ro, rn);
                        check(what + " bytes", BitConverter.ToString(File.ReadAllBytes(a)), NBytes(b));
                        check(what + " saved unless read-only", B(attr != FileAttributes.ReadOnly), rn);
                    }
                check("attributes: no temporary file is left behind", "0", TmpCount(dir).ToString());
                if (!_linux)
                {
                    // The default path (SPEC 6c): %LOCALAPPDATA%\HourCalc\HourCalc.ini, the same in both programs.
                string oracleDefault = Settings.DefaultPath;
                string nativeDefault = Unesc(cli.Query("st.defpath"));
                check("default path (oracle) is in the local application data folder", "True",
                    (oracleDefault != null && string.Equals(oracleDefault, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HourCalc", "HourCalc.ini"), StringComparison.OrdinalIgnoreCase)).ToString());
                check("default path (native) is the same", (oracleDefault ?? "").ToLowerInvariant(), nativeDefault.ToLowerInvariant());
                }
                else
                {
                    // The default path on Linux: $XDG_CONFIG_HOME/hourcalc/hourcalc.ini, or $HOME/.config/hourcalc/hourcalc.ini.
                    cli.Query("env.set XDG_CONFIG_HOME /tmp/xdg-test");
                    check("default path: XDG_CONFIG_HOME", "/tmp/xdg-test/hourcalc/hourcalc.ini", Unesc(cli.Query("st.defpath")));
                    cli.Query("env.set XDG_CONFIG_HOME /tmp/xdg-test//");
                    check("default path: ... a trailing slash does not double", "/tmp/xdg-test/hourcalc/hourcalc.ini", Unesc(cli.Query("st.defpath")));
                    cli.Query("env.set XDG_CONFIG_HOME relative");
                    cli.Query("env.set HOME /home/tester");
                    check("default path: a relative XDG_CONFIG_HOME is ignored", "/home/tester/.config/hourcalc/hourcalc.ini", Unesc(cli.Query("st.defpath")));
                    cli.Query("env.set XDG_CONFIG_HOME \\e");
                    check("default path: HOME/.config", "/home/tester/.config/hourcalc/hourcalc.ini", Unesc(cli.Query("st.defpath")));
                }
                check("the folder name", Settings.FolderName, "HourCalc");
            }
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
            if (_linux) { try { _pool[0].Query("fs.rmrf " + Esc(_ndir)); } catch (Exception) { } }
        }
        Interlocked.Add(ref _totalCases, checks);
        Interlocked.Add(ref _totalMismatches, mismatches);
        Console.WriteLine(string.Format(Inv, "{0,-44} {1,12:N0} checks {2,6} mismatches", "Settings files [" + ci.Name + "]", checks, mismatches));
        return mismatches;
    }

    // ---------------------------------------------------------------- main

    static int Main(string[] args)
    {
        bool threadsGiven = false;
        for (int ai = 0; ai < args.Length; ai++)
        {
            string a = args[ai];
            if (a == "--quick") _quick = true;
            else if (a.StartsWith("--threads=")) { _threads = int.Parse(a.Substring(10), Inv); threadsGiven = true; }
            else if (a.StartsWith("--only=")) _only = a.Substring(7);
            else if (a == "--cli" && ai + 1 < args.Length) _cliCmd = args[++ai];
            else if (a.StartsWith("--cli=")) _cliCmd = a.Substring(6);
            else _cliPath = Path.GetFullPath(a);
        }
        // Through a command line (wsl.exe) the native processes share the CPUs of the virtual machine: fewer workers by default.
        if (_cliCmd != null && !threadsGiven) _threads = Math.Min(_threads, 6);
        if (_cliCmd == null && (_cliPath == null || !File.Exists(_cliPath)))
        {
            Console.WriteLine("usage: differential.exe <path to hc_cli.exe> | --cli \"<command line>\"  [--quick] [--threads=N] [--only=<text>]");
            return 2;
        }
        var total = Stopwatch.StartNew();
        Console.WriteLine("Differential test: C# oracle vs {0} ({1} threads{2})", _cliCmd ?? _cliPath, _threads, _quick ? ", quick" : "");

        // One native process per worker, started in parallel and kept for the whole run; a sanity check
        // of the protocol before the real work.
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
        if (_linux) Console.WriteLine("The native program is the Linux build: its files are reached through fs.* commands, Linux counterparts replace the Windows-only checks.");

        // Cultures: nl-BE (','), en-US ('.'), and a made up separator of two characters. The
        // 'useUserOverride' flag is off so that the machine's regional settings cannot interfere.
        // The last one is the machine's own culture with the separator override of the core cleared,
        // so that the core reads the user's decimal separator itself (GetLocaleInfoEx).
        var userCulture = CultureInfo.CurrentCulture;
        if (_linux)
        {
            // The user of the native program is a Linux user: its decimal separator is the one it says it has (LANG in WSL).
            _pool[0].Query("sep \\e");
            var uc = new CultureInfo("en-US", false);
            uc.NumberFormat.NumberDecimalSeparator = Unesc(_pool[0].Query("tm.sep"));
            userCulture = uc;
        }
        var nl = new CultureInfo("nl-BE", false);
        var en = new CultureInfo("en-US", false);
        var odd = new CultureInfo("en-US", false);
        odd.NumberFormat.NumberDecimalSeparator = "ab";
        var cultures = new[]
        {
            new KeyValuePair<string, CultureInfo>(",", nl),
            new KeyValuePair<string, CultureInfo>(".", en),
            new KeyValuePair<string, CultureInfo>("ab", odd),
            new KeyValuePair<string, CultureInfo>("", userCulture)
        };

        int randomCount = _quick ? 100000 : 1000000;
        var suites = new List<Suite>();
        suites.Add(SpecTables());
        suites.Add(ConverterExhaustive("converter exhaustive (18 symbols)", ConvAlphabet, _quick ? 4 : 5));
        suites.Add(ConverterExhaustive("converter exhaustive (digits, sep)", "019:,.uh", _quick ? 5 : 7));
        suites.Add(ConverterBoundary());
        suites.Add(CalcExhaustive("calculator exhaustive (all keys)", FullKeys, _quick ? 3 : 4));
        suites.Add(CalcExhaustive("calculator exhaustive (12 keys)", ReducedKeys, _quick ? 4 : 5));
        suites.Add(CalcRandom(randomCount, 20261010UL));
        suites.Add(TimeMathGrid());
        suites.Add(EntryList());
        suites.Add(SmallFunctions());
        suites.Add(RawGetMinutes());
        suites.Add(EntryGrid());
        suites.Add(DecArith(_quick ? 40000 : 150000));
        suites.Add(DecDivWide(_quick ? 40000 : 200000));
        suites.Add(SettingsParse(_quick ? 20000 : 150000));

        foreach (var c in cultures)
        {
            bool odd2 = c.Key == "ab" || c.Key == "";
            Console.WriteLine("-- decimal separator {0} ({1})", c.Key.Length == 0 ? "of the user, no override: '" + userCulture.NumberFormat.NumberDecimalSeparator + "'" : "'" + c.Key + "'", c.Value.Name);
            Thread.CurrentThread.CurrentCulture = c.Value;
            foreach (Suite s in suites)
            {
                if (_only.Length > 0 && s.Name.IndexOf(_only, StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (odd2 && s.Heavy) continue;     // the made up separators get the light suites and a slice of the heavy ones
                if (s.Once && c.Key != ",") continue;
                RunSuite(s, c.Value, c.Key, c.Key.Length == 0 ? "user" : c.Key);
            }
            if (odd2)
            {
                string name = c.Key.Length == 0 ? "user" : c.Key;
                string alphabet = c.Key.Length == 0 ? "019:,.uh x" : "09:ab.u";
                RunSuite(ConverterExhaustive("converter exhaustive (" + alphabet.Length + " symbols)", alphabet, 5), c.Value, c.Key, name);
                RunSuite(CalcRandom(_quick ? 20000 : 150000, 99UL), c.Value, c.Key, name);
            }
            if (c.Key == "," && (_only.Length == 0 || "settings files".IndexOf(_only, StringComparison.OrdinalIgnoreCase) >= 0)) SettingsFileChecks(c.Value);
        }

        foreach (Cli c in _pool) c.Dispose();
        Console.WriteLine();
        Console.WriteLine(string.Format(Inv, "Differential: {0:N0} checks, {1:N0} mismatches, {2:N1} s", _totalCases, _totalMismatches, total.Elapsed.TotalSeconds));
        return _totalMismatches == 0 ? 0 : 1;
    }
}
