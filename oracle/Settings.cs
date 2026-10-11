// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the settings, kept in HourCalc.ini in %LOCALAPPDATA%\HourCalc. No WPF references.
//
// The file holds two lines, "DecimalPlaces=<n>" (2..6) and "Theme=<auto|light|dark>". It is read at
// startup and written only when the user changes a setting; whenever it is written it holds both, in
// this order. A changed setting is written with SavePlaces or SaveTheme, which read the file again and
// keep the other setting as it is on disk, so changing one setting never loses the other, not even when
// a second HourCalc has changed it in the meantime. The file is replaced in one step (a temporary file,
// then a rename), so a reader never sees half a file. A missing or invalid file (or key, or value) means
// the defaults (2 places, theme auto), and a folder that cannot be written to is not an error: the
// setting then only lasts for the current session. Nothing else is ever written to disk.

using System;
using System.IO;
using System.Text;

namespace HourCalc
{
    /// <summary>Which theme the window uses: follow the Windows app theme, or force one.</summary>
    public enum ThemeMode
    {
        Auto = 0,
        Light = 1,
        Dark = 2
    }

    public static class Settings
    {
        public const string FileName = "HourCalc.ini";
        const string Key = "DecimalPlaces";
        const string ThemeKey = "Theme";
        public const ThemeMode DefaultTheme = ThemeMode.Auto;

        /// <summary>The folder in %LOCALAPPDATA% that holds the settings file.</summary>
        public const string FolderName = "HourCalc";

        /// <summary>
        /// Where HourCalc.ini lives: %LOCALAPPDATA%\HourCalc\HourCalc.ini (the folder is created by the first save). Null when
        /// the folder of the user's local application data cannot be determined.
        /// </summary>
        public static string DefaultPath
        {
            get
            {
                try
                {
                    string dir = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                    return string.IsNullOrEmpty(dir) ? null : Path.Combine(dir, FolderName, FileName);
                }
                catch (Exception)
                {
                    return null;
                }
            }
        }

        /// <summary>
        /// The value of the first line "key=value" of an ini text (the key is not case sensitive, key and value are
        /// trimmed). Blank lines, ';' and '#' comments and [sections] are ignored. False when no line has the key.
        /// </summary>
        static bool TryFindValue(string text, string key, out string value)
        {
            value = null;
            if (string.IsNullOrEmpty(text)) return false;
            foreach (string raw in text.Split('\n'))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                if (!string.Equals(line.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase)) continue;
                value = line.Substring(eq + 1).Trim();
                return true;
            }
            return false;
        }

        /// <summary>
        /// The decimal places in the text of an ini file: the value of "DecimalPlaces=" when it is a
        /// whole number from 2 to 6, otherwise the default. Blank lines, ';' and '#' comments and
        /// [sections] are ignored; the key is not case sensitive. The first line with the key decides.
        /// </summary>
        public static int ParseDecimalPlaces(string text)
        {
            string value;
            if (!TryFindValue(text, Key, out value)) return TimeMath.DefaultDecimals;
            int n;
            if (int.TryParse(value, System.Globalization.NumberStyles.None,
                    System.Globalization.CultureInfo.InvariantCulture, out n) &&
                n >= TimeMath.MinDisplayDecimals && n <= TimeMath.MaxDisplayDecimals)
                return n;
            return TimeMath.DefaultDecimals;
        }

        /// <summary>
        /// The theme in the text of an ini file: the value of "Theme=" when it is auto, light or dark (not case
        /// sensitive), otherwise auto. Same line rules as the decimal places; the first line with the key decides.
        /// </summary>
        public static ThemeMode ParseTheme(string text)
        {
            string value;
            if (!TryFindValue(text, ThemeKey, out value)) return DefaultTheme;
            if (string.Equals(value, "light", StringComparison.OrdinalIgnoreCase)) return ThemeMode.Light;
            if (string.Equals(value, "dark", StringComparison.OrdinalIgnoreCase)) return ThemeMode.Dark;
            return ThemeMode.Auto;
        }

        /// <summary>How reading the file went: the text was read, there is no such file, or something is there that cannot be read.</summary>
        enum ReadResult
        {
            Read,
            Absent,
            Failed
        }

        /// <summary>The text of the ini file. Failed (locked, no access) is not Absent: saving must not overwrite what it cannot read.</summary>
        static ReadResult TryReadText(string path, out string text)
        {
            text = null;
            try
            {
                if (string.IsNullOrEmpty(path) || !File.Exists(path)) return ReadResult.Absent;
                text = File.ReadAllText(path);
                return ReadResult.Read;
            }
            catch (Exception)
            {
                return ReadResult.Failed;
            }
        }

        /// <summary>The text of the ini file, or null when there is none or it cannot be read.</summary>
        static string ReadText(string path)
        {
            string text;
            return TryReadText(path, out text) == ReadResult.Read ? text : null;
        }

        /// <summary>Reads the decimal places from an ini file; any problem gives the default.</summary>
        public static int LoadDecimalPlaces(string path)
        {
            return ParseDecimalPlaces(ReadText(path));
        }

        /// <summary>Reads the theme from an ini file; any problem gives auto.</summary>
        public static ThemeMode LoadTheme(string path)
        {
            return ParseTheme(ReadText(path));
        }

        /// <summary>Reads both settings from an ini file (one read); any problem gives the defaults.</summary>
        public static void Load(string path, out int places, out ThemeMode theme)
        {
            string text = ReadText(path);
            places = ParseDecimalPlaces(text);
            theme = ParseTheme(text);
        }

        /// <summary>The value written for a theme: "auto", "light" or "dark" (anything else is written as auto).</summary>
        public static string ThemeToString(ThemeMode theme)
        {
            if (theme == ThemeMode.Light) return "light";
            if (theme == ThemeMode.Dark) return "dark";
            return "auto";
        }

        /// <summary>
        /// Writes the ini file, always both settings: "DecimalPlaces=&lt;n&gt;" and "Theme=&lt;value&gt;", each followed by
        /// CR LF (a place count outside 2..6 is written as the default, 2). The contents are written to a file of their own
        /// next to it, which then replaces the settings file in one step: a reader sees the old file or the new one, never an
        /// empty or half written one. The folder is created when it does not exist (one level; its parent must exist). False
        /// (and no exception) when the file cannot be written: a read-only file, a path that is a folder, a folder that cannot
        /// be made. This is the primitive; the UI changes a single setting with SavePlaces and SaveTheme.
        /// </summary>
        public static bool Save(string path, int places, ThemeMode theme)
        {
            if (string.IsNullOrEmpty(path)) return false;
            string tmp = null;
            try
            {
                if (Directory.Exists(path)) return false;
                if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReadOnly) != 0) return false;
                string dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    string parent = Path.GetDirectoryName(dir);
                    if (string.IsNullOrEmpty(parent) || !Directory.Exists(parent)) return false;
                    Directory.CreateDirectory(dir);
                }
                byte[] bytes = Encoding.ASCII.GetBytes(
                    Key + "=" + TimeMath.ClampDecimals(places).ToString(System.Globalization.CultureInfo.InvariantCulture) + "\r\n" +
                    ThemeKey + "=" + ThemeToString(theme) + "\r\n");
                tmp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                using (FileStream fs = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    fs.Write(bytes, 0, bytes.Length);
                    fs.Flush(true);
                }
                if (File.Exists(path)) File.Replace(tmp, path, null);
                else File.Move(tmp, path);
                tmp = null;
                return true;
            }
            catch (Exception)
            {
                return false;
            }
            finally
            {
                if (tmp != null)
                {
                    try { File.Delete(tmp); } catch (Exception) { }
                }
            }
        }

        /// <summary>
        /// A single setting changed: reads the file again and writes it with the new place count and the theme as it is in the
        /// file now (what another HourCalc may have saved since this one started). 'currentTheme' is used only when there is no
        /// file. When the file exists but cannot be read (locked by an editor or a virus scanner) nothing is written and the
        /// result is false: the defaults of a failed read must never replace what is on disk.
        /// </summary>
        public static bool SavePlaces(string path, int places, ThemeMode currentTheme)
        {
            return SaveChanged(path, true, places, currentTheme);
        }

        /// <summary>The same for a changed theme; 'currentPlaces' is used only when there is no file.</summary>
        public static bool SaveTheme(string path, ThemeMode theme, int currentPlaces)
        {
            return SaveChanged(path, false, currentPlaces, theme);
        }

        static bool SaveChanged(string path, bool placesChanged, int places, ThemeMode theme)
        {
            if (string.IsNullOrEmpty(path)) return false;
            string text;
            ReadResult r = TryReadText(path, out text);
            if (r == ReadResult.Failed) return false;
            if (r == ReadResult.Read)
            {
                if (placesChanged) theme = ParseTheme(text);
                else places = ParseDecimalPlaces(text);
            }
            return Save(path, places, theme);
        }
    }
}
