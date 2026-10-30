using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace LmuQuadViewLauncher
{
    public sealed class LauncherSettings
    {
        public string GameDirectory { get; set; }
        public string Backend { get; set; }
        public string FoveationMode { get; set; }
        public string QuadViewsManifest { get; set; }
        public string VectorXrManifest { get; set; }
        public bool DebugFocus { get; set; }
        public bool RenderProfiling { get; set; }
        public bool DiagnosticsEnabled { get; set; }
        public int RenderProfileSeconds { get; set; }
        public bool NativeOuterResolution { get; set; }
        public decimal OuterBlur { get; set; }
        public decimal OuterPercent { get; set; }
        public decimal InnerPercent { get; set; }
        public decimal FocusWidthPercent { get; set; }
        public decimal FocusHeightPercent { get; set; }
        // Provider percentages retain their old meaning; this is UI calibration only.
        public decimal FocusAspectRatio { get; set; }
        public LauncherSettings()
        {
            GameDirectory = @"D:\SteamLibrary\steamapps\common\Le Mans Ultimate";
            Backend = "Native"; FoveationMode = "auto"; QuadViewsManifest = ""; VectorXrManifest = "";
            NativeOuterResolution = true; OuterPercent = 25; InnerPercent = 100;
            RenderProfileSeconds=120;
            DiagnosticsEnabled=true;
            FocusWidthPercent = FocusHeightPercent = 50;
        }
    }
    public static class QvfrSettings
    {
        const string Begin = "# BEGIN LMU QUAD VIEW MOD PROFILE";
        const string End = "# END LMU QUAD VIEW MOD PROFILE";
        public static void Validate(LauncherSettings s)
        {
            if (s == null) throw new ArgumentNullException("settings");
            if(s.RenderProfileSeconds<30||s.RenderProfileSeconds>300)throw new ArgumentException("Capture length must be between 30 and 300 seconds.");
            if(s.FocusAspectRatio!=0&&(s.FocusAspectRatio<0.1m||s.FocusAspectRatio>10m)) throw new ArgumentException("Invalid measured focus aspect ratio.");
            if (String.IsNullOrWhiteSpace(s.GameDirectory)) throw new ArgumentException("Select the LMU game folder.");
            if (s.Backend != "Native" && s.Backend != "QVFR" && s.Backend != "VectorXR") throw new ArgumentException("Choose Native, QVFR or VectorXR.");
            if (s.FoveationMode != "auto" && s.FoveationMode != "fixed") throw new ArgumentException("Choose Auto or Fixed foveation.");
            if (s.OuterBlur < 0 || s.OuterBlur > 3) throw new ArgumentException("Outer smoothing must be between 0 and 3 pixels.");
            if (s.OuterPercent < 10 || s.OuterPercent > 400 || s.InnerPercent < 25 || s.InnerPercent > 400 ||
                s.FocusWidthPercent < 10 || s.FocusWidthPercent > 90 || s.FocusHeightPercent < 10 || s.FocusHeightPercent > 90)
                throw new ArgumentException("Resolution or focus size is outside the supported range.");
            if (s.Backend == "QVFR" && s.OuterPercent > s.InnerPercent * Math.Min(s.FocusWidthPercent, s.FocusHeightPercent) / 100m)
                throw new ArgumentException("The outer image must fit within the focus buffer. Reduce outer resolution, or increase inner resolution / focus width and height.");
        }
        static string Value(XElement root, string name, string fallback) { return (string)root.Element(name) ?? fallback; }
        public static LauncherSettings Load(string path)
        {
            var s = new LauncherSettings();
            if (!File.Exists(path)) return s;
            var options = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null };
            XElement x;
            using (var reader = XmlReader.Create(path, options)) x = XElement.Load(reader);
            if (x.Name != "QuadView") throw new InvalidDataException("Unrecognized launcher settings.");
            s.GameDirectory = Value(x, "GameDirectory", s.GameDirectory);
            s.Backend = Value(x, "Backend", s.Backend); s.FoveationMode = Value(x, "FoveationMode", s.FoveationMode);
            s.QuadViewsManifest = Value(x, "QuadViewsManifest", "");
            s.VectorXrManifest = Value(x, "VectorXrManifest", "");
            s.DebugFocus = Boolean.Parse(Value(x, "DebugFocus", "false"));
            s.RenderProfiling = Boolean.Parse(Value(x, "RenderProfiling", "false"));
            s.DiagnosticsEnabled=Boolean.Parse(Value(x,"DiagnosticsEnabled","true"));
            s.RenderProfileSeconds=Int32.Parse(Value(x,"RenderProfileSeconds","120"),CultureInfo.InvariantCulture);
            s.NativeOuterResolution = Boolean.Parse(Value(x, "NativeOuterResolution", "true"));
            s.OuterBlur = Decimal.Parse(Value(x, "OuterBlur", "0"), CultureInfo.InvariantCulture);
            s.OuterPercent = Decimal.Parse(Value(x, "OuterPercent", "25"), CultureInfo.InvariantCulture);
            s.InnerPercent = Decimal.Parse(Value(x, "InnerPercent", "100"), CultureInfo.InvariantCulture);
            s.FocusWidthPercent = Decimal.Parse(Value(x, "FocusWidthPercent", "50"), CultureInfo.InvariantCulture);
            s.FocusHeightPercent = Decimal.Parse(Value(x, "FocusHeightPercent", "50"), CultureInfo.InvariantCulture);
            s.FocusAspectRatio = Decimal.Parse(Value(x, "FocusAspectRatio", "0"), CultureInfo.InvariantCulture);
            Validate(s); return s;
        }
        public static void Save(string path, LauncherSettings s)
        {
            Validate(s);
            var xml = new XElement("QuadView", new XElement("GameDirectory", s.GameDirectory),
                new XElement("Backend", s.Backend), new XElement("FoveationMode", s.FoveationMode),
                new XElement("QuadViewsManifest", s.QuadViewsManifest ?? ""), new XElement("VectorXrManifest", s.VectorXrManifest ?? ""), new XElement("DebugFocus", s.DebugFocus),
                new XElement("RenderProfiling", s.RenderProfiling),
                new XElement("DiagnosticsEnabled",s.DiagnosticsEnabled),
                new XElement("RenderProfileSeconds",s.RenderProfileSeconds),
                new XElement("NativeOuterResolution", s.NativeOuterResolution), new XElement("UseRuntimeSettings", true),
                new XElement("OuterBlur", s.OuterBlur),
                new XElement("OuterPercent", s.OuterPercent), new XElement("InnerPercent", s.InnerPercent),
                new XElement("FocusWidthPercent", s.FocusWidthPercent), new XElement("FocusHeightPercent", s.FocusHeightPercent),
                new XElement("FocusAspectRatio", s.FocusAspectRatio));
            WriteAtomic(path, xml.ToString(), false);
        }
        public static string ProfileText(string existing, LauncherSettings s)
        {
            Validate(s); existing = existing ?? "";
            int starts = Regex.Matches(existing, Regex.Escape(Begin)).Count;
            int ends = Regex.Matches(existing, Regex.Escape(End)).Count;
            if (starts != ends || starts > 1) throw new InvalidDataException("Malformed LMU profile markers. QVFR settings were not changed.");
            string clean = existing;
            if (starts == 1)
            {
                string pattern = "(?ms)^" + Regex.Escape(Begin) + "\\r?$.*?^" + Regex.Escape(End) + "\\r?$(?:\\n)?";
                var match = Regex.Match(existing, pattern);
                bool suffixNeedsSection = false;
                if (match.Success)
                {
                    foreach (string line in existing.Substring(match.Index + match.Length).Split('\n'))
                    {
                        string trimmed = line.Trim();
                        if (trimmed.StartsWith("[", StringComparison.Ordinal)) break;
                        if (trimmed.Length == 0 || trimmed.StartsWith("#", StringComparison.Ordinal) || trimmed.StartsWith(";", StringComparison.Ordinal)) continue;
                        suffixNeedsSection = true; break;
                    }
                }
                // The removed block selected the LMU section. Keep that scope for
                // user-added keys following it, before moving our overrides last.
                clean = Regex.Replace(existing, pattern, suffixNeedsSection ? "[exe:Le Mans Ultimate.exe]\n" : "");
                if (clean == existing) throw new InvalidDataException("Malformed LMU profile block. QVFR settings were not changed.");
            }
            Func<decimal, string> fraction = delegate(decimal n) { return (n / 100m).ToString("0.####", CultureInfo.InvariantCulture); };
            return clean.TrimEnd('\r', '\n') + "\n\n" + Begin + "\n[exe:Le Mans Ultimate.exe]\n" +
                "prefer_foveated_rendering=0\nforce_no_eye_tracking=0\n" +
                "peripheral_multiplier=" + fraction(s.OuterPercent) + "\nfocus_multiplier=" + fraction(s.InnerPercent) +
                "\nhorizontal_focus_section=" + fraction(s.FocusWidthPercent) + "\nvertical_focus_section=" + fraction(s.FocusHeightPercent) +
                "\nhorizontal_fixed_section=" + fraction(s.FocusWidthPercent) + "\nvertical_fixed_section=" + fraction(s.FocusHeightPercent) +
                "\nturbo_mode=0\n" + End + "\n";
        }
        public static void SaveProfile(LauncherSettings s)
        {
            SaveProfile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Quad-Views-Foveated", "settings.cfg"), s);
        }
        public static void SaveProfile(string path, LauncherSettings s)
        {
            Validate(s); if (s.Backend != "QVFR") return;
            string old = File.Exists(path) ? File.ReadAllText(path) : "";
            string updated = ProfileText(old, s);
            if (updated != old) WriteAtomic(path, updated, true);
        }
        static void WriteAtomic(string path, string text, bool backup)
        {
            path = Path.GetFullPath(path); Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(temp, text, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, backup ? path + ".lmu-backup-" + Guid.NewGuid().ToString("N") : null);
                else File.Move(temp, path);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
    }
}
