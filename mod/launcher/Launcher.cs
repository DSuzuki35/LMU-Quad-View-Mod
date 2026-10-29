using System;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Forms;
using System.Xml.Linq;

namespace LmuQuadViewLauncher
{
    public static class LaunchCommand
    {
        static string Literal(string text) { return "'" + text.Replace("'", "''") + "'"; }
        public static string Invocation(string script, string game, bool outline)
        {
            if (String.IsNullOrWhiteSpace(game)) throw new ArgumentException("Select the LMU game folder.");
            return "& " + Literal(script) + " -Mode quad -NativeOuterResolution -UseRuntimeSettings -GameDirectory " + Literal(game) +
                (outline ? " -DebugFocus" : "");
        }
        public static string Script(string invocation, string output)
        {
            return "$ErrorActionPreference='Stop'; try { " + invocation +
                " | Out-File -LiteralPath " + Literal(output) + " -Encoding utf8; exit 0 } catch { " +
                "[IO.File]::WriteAllText(" + Literal(output) + ", $_.Exception.Message); exit 1 }";
        }
    }

    sealed class Launcher : Form
    {
        readonly string root = AppDomain.CurrentDomain.BaseDirectory;
        readonly TextBox game = new TextBox();
        readonly CheckBox outline = new CheckBox();
        readonly Label status = new Label();
        readonly Button launch = new Button();
        string SettingsPath { get { return Path.Combine(root, "launcher-settings.xml"); } }

        static Label TextLabel(string text)
        {
            return new Label { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 7, 8, 7) };
        }
        public Launcher()
        {
            Text = "LMU QuadView Launcher";
            Font = new Font("Segoe UI", 10F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(640, 430);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 2, RowCount = 7 };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 66));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 34));
            Controls.Add(layout);
            var heading = TextLabel("QuadView - use Pimax / OpenXR settings");
            heading.Font = new Font(Font, FontStyle.Bold);
            layout.Controls.Add(heading, 0, 0); layout.SetColumnSpan(heading, 2);
            layout.Controls.Add(TextLabel("LMU game folder"), 0, 1);
            var pathRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Height = 34, Margin = new Padding(0) };
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 94));
            game.Dock = DockStyle.Fill;
            game.Text = @"D:\SteamLibrary\steamapps\common\Le Mans Ultimate";
            var browse = new Button { Text = "Browse...", Dock = DockStyle.Fill };
            browse.Click += delegate { Browse(); };
            pathRow.Controls.Add(game, 0, 0); pathRow.Controls.Add(browse, 1, 0);
            layout.Controls.Add(pathRow, 0, 2); layout.SetColumnSpan(pathRow, 2);
            outline.Text = "Show green focus outline (eye-tracking check)";
            outline.AutoSize = true; outline.Margin = new Padding(0, 12, 0, 10);
            layout.Controls.Add(outline, 0, 3); layout.SetColumnSpan(outline, 2);
            var help = TextLabel("Set center / edge resolution and horizontal / vertical FOV\nin Pimax EVO. QuadView Foveated Rendering must be ON.\n\nThis launcher uses the runtime's FOV and resolutions directly.\nNo extra size or resolution multipliers. Restart LMU after changes.");
            help.ForeColor = Color.DimGray;
            layout.Controls.Add(help, 0, 4); layout.SetColumnSpan(help, 2);
            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, Margin = new Padding(0, 10, 0, 8) };
            var save = new Button { Text = "Save settings", AutoSize = true };
            save.Click += delegate { try { Save(); status.Text = "Saved. These settings apply on your next launch."; } catch (Exception ex) { ShowError(ex.Message); } };
            launch.Text = "Launch LMU"; launch.AutoSize = true;
            launch.Click += delegate { Launch(); };
            buttons.Controls.Add(save); buttons.Controls.Add(launch);
            layout.Controls.Add(buttons, 0, 5); layout.SetColumnSpan(buttons, 2);
            status.AutoSize = true; status.MaximumSize = new Size(590, 0); status.Text = "Offline prototype. PowerShell 7 is required.";
            layout.Controls.Add(status, 0, 6); layout.SetColumnSpan(status, 2);
            AcceptButton = launch;
            LoadSettings();
        }
        void Browse()
        {
            using (var dialog = new FolderBrowserDialog { Description = "Select the folder containing Le Mans Ultimate.exe", ShowNewFolderButton = false })
            {
                if (Directory.Exists(game.Text)) dialog.SelectedPath = game.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK) game.Text = dialog.SelectedPath;
            }
        }
        string Invocation()
        {
            return LaunchCommand.Invocation(Path.Combine(root, "launch.ps1"), game.Text.Trim(), outline.Checked);
        }
        void Save()
        {
            Invocation(); // Validate before writing preferences.
            new XElement("QuadView", new XElement("GameDirectory", game.Text.Trim()),
                new XElement("UseRuntimeSettings", true),
                new XElement("DebugFocus", outline.Checked)).Save(SettingsPath);
        }
        void LoadSettings()
        {
            if (!File.Exists(SettingsPath)) return;
            try
            {
                var x = XElement.Load(SettingsPath);
                var g = (string)x.Element("GameDirectory");
                var d = bool.Parse((string)x.Element("DebugFocus"));
                LaunchCommand.Invocation("launch.ps1", g, d);
                game.Text = g; outline.Checked = d;
                status.Text = "Saved settings loaded. Changes apply on launch.";
            }
            catch { status.Text = "Could not read saved settings; defaults loaded."; }
        }
        static string FindPowerShell()
        {
            var standard = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), @"PowerShell\7\pwsh.exe");
            if (File.Exists(standard)) return standard;
            foreach (var entry in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
            {
                try { var candidate = Path.Combine(entry.Trim().Trim('"'), "pwsh.exe"); if (File.Exists(candidate)) return candidate; }
                catch (ArgumentException) { }
            }
            throw new FileNotFoundException("PowerShell 7 (pwsh.exe) was not found. Install it before launching LMU.");
        }
        void Launch()
        {
            try
            {
                var invocation = Invocation();
                if (!File.Exists(Path.Combine(game.Text.Trim(), "Le Mans Ultimate.exe"))) throw new FileNotFoundException("Choose the folder containing Le Mans Ultimate.exe.");
                foreach (var file in new[] { "launch.ps1", "XR_APILAYER_LMU_quadview.json", @"build\lmu_quadview.dll" })
                    if (!File.Exists(Path.Combine(root, file))) throw new FileNotFoundException("The package is incomplete: " + file);
                var pwsh = FindPowerShell();
                Save();
                var logs = Path.Combine(root, "logs"); Directory.CreateDirectory(logs);
                var output = Path.Combine(logs, "launcher-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 6) + ".txt");
                var encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(LaunchCommand.Script(invocation, output)));
                var process = new Process { StartInfo = new ProcessStartInfo { FileName = pwsh,
                    Arguments = "-NoProfile -NonInteractive -EncodedCommand " + encoded,
                    WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden }, EnableRaisingEvents = true };
                process.Exited += delegate
                {
                    int exitCode = process.ExitCode; process.Dispose();
                    if (IsDisposed || !IsHandleCreated) return;
                    try { BeginInvoke((Action)delegate
                    {
                        launch.Enabled = true;
                        if (exitCode == 0) status.Text = "LMU started. Changes apply to the next launch.";
                        else { string error = File.Exists(output) ? File.ReadAllText(output) : "Launcher exited with code " + exitCode;
                            ShowError(error.Length > 3000 ? error.Substring(0, 3000) : error); }
                    }); } catch (InvalidOperationException) { }
                };
                launch.Enabled = false; status.Text = "Checking the game build and starting LMU...";
                try { if (!process.Start()) throw new InvalidOperationException("Could not start PowerShell."); }
                catch { process.Dispose(); launch.Enabled = true; throw; }
            }
            catch (Exception ex) { ShowError(ex.Message); }
        }
        void ShowError(string message)
        {
            status.Text = "Could not complete: " + (message.Length > 160 ? message.Substring(0, 160) + "..." : message);
            MessageBox.Show(this, message, "LMU QuadView", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Launcher());
        }
    }
}
