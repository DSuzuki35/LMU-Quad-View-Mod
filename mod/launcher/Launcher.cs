using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

namespace LmuQuadViewLauncher
{
    static class Theme
    {
        public static readonly Color Background = Color.FromArgb(18, 19, 24);
        public static readonly Color Sidebar = Color.FromArgb(22, 23, 29);
        public static readonly Color Card = Color.FromArgb(29, 30, 38);
        public static readonly Color Field = Color.FromArgb(22, 23, 30);
        public static readonly Color Line = Color.FromArgb(49, 50, 63);
        public static readonly Color Text = Color.FromArgb(237, 236, 245);
        public static readonly Color Muted = Color.FromArgb(153, 154, 174);
        public static readonly Color Accent = Color.FromArgb(137, 111, 246);
        public static readonly Color AccentDark = Color.FromArgb(49, 40, 79);
        public static Label Label(string text, float size, Color color)
        {
            return new Label { Text = text, ForeColor = color, BackColor = Color.Transparent,
                Font = new Font("Segoe UI", size), AutoSize = false, Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0) };
        }
        public static Button Button(string text, bool primary)
        {
            var b = new Button { Text = text, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand,
                BackColor = primary ? Accent : Card, ForeColor = Text, Height = 42,
                Font = new Font("Segoe UI", 10F, FontStyle.Bold), UseVisualStyleBackColor = false };
            b.FlatAppearance.BorderColor = primary ? Accent : Line;
            b.FlatAppearance.MouseOverBackColor = primary ? Color.FromArgb(155, 131, 255) : AccentDark;
            b.FlatAppearance.MouseDownBackColor = AccentDark;
            return b;
        }
    }

    sealed class Surface : Panel
    {
        public Surface() { BackColor = Theme.Card; Padding = new Padding(18); DoubleBuffered = true; }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var p = new Pen(Theme.Line)) e.Graphics.DrawRectangle(p, 0, 0, Width - 1, Height - 1);
        }
    }

    // A real scrollable viewport without the OS's bright non-client scroll bars.
    sealed class DarkScrollBar : Control
    {
        int position, maximum, viewport;
        int dragY, dragValue;
        bool dragging;
        public event EventHandler ValueChanged;
        public int Maximum { get { return maximum; } set { maximum = Math.Max(0, value); Value = position; Invalidate(); } }
        public int Viewport { get { return viewport; } set { viewport = Math.Max(1, value); Invalidate(); } }
        public int Value { get { return position; } set { int next = Math.Max(0, Math.Min(maximum, value)); if (next == position) return;
            position = next; Invalidate(); if (ValueChanged != null) ValueChanged(this, EventArgs.Empty); } }
        int ThumbHeight { get { return Math.Min(Height, Math.Max(Font.Height * 2, Height * viewport / Math.Max(1, maximum + viewport))); } }
        int ThumbTop { get { return maximum == 0 ? 0 : (int)((long)(Height - ThumbHeight) * position / maximum); } }
        public DarkScrollBar()
        {
            Name = "PageScrollBar"; Width = 12; Dock = DockStyle.Right; TabStop = true; BackColor = Theme.Background;
            AccessibleRole = AccessibleRole.ScrollBar; AccessibleName = "Page scroll"; Cursor = Cursors.Hand;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }
        public void ScrollWheel(int delta) { Value -= delta * Math.Max(1, SystemInformation.MouseWheelScrollLines) * Font.Height / 120; }
        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Up || keyData == Keys.Down || keyData == Keys.PageUp || keyData == Keys.PageDown ||
                keyData == Keys.Home || keyData == Keys.End || base.IsInputKey(keyData);
        }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Up) Value -= Font.Height; else if (e.KeyCode == Keys.Down) Value += Font.Height;
            else if (e.KeyCode == Keys.PageUp) Value -= viewport; else if (e.KeyCode == Keys.PageDown) Value += viewport;
            else if (e.KeyCode == Keys.Home) Value = 0; else if (e.KeyCode == Keys.End) Value = maximum;
        }
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; Focus();
            if (e.Y >= ThumbTop && e.Y < ThumbTop + ThumbHeight) { dragging = true; dragY = e.Y; dragValue = position; Capture = true; }
            else Value += e.Y < ThumbTop ? -viewport : viewport;
        }
        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e); if (dragging) Value = dragValue + (int)((long)(e.Y - dragY) * maximum / Math.Max(1, Height - ThumbHeight));
        }
        protected override void OnMouseUp(MouseEventArgs e) { dragging = false; Capture = false; base.OnMouseUp(e); }
        protected override void OnMouseCaptureChanged(EventArgs e) { if (!Capture) dragging = false; base.OnMouseCaptureChanged(e); }
        protected override void OnMouseWheel(MouseEventArgs e) { ScrollWheel(e.Delta); base.OnMouseWheel(e); }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e); if (maximum == 0) return;
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            float thickness = Math.Max(3, Width * 0.38F);
            using (var track = new Pen(Theme.Line, Math.Max(1, thickness / 3))) e.Graphics.DrawLine(track, Width / 2F, 0, Width / 2F, Height);
            using (var thumb = new Pen(Focused || dragging ? Theme.Accent : Theme.Muted, thickness)) {
                thumb.StartCap = thumb.EndCap = LineCap.Round;
                e.Graphics.DrawLine(thumb, Width / 2F, ThumbTop + thickness / 2, Width / 2F, ThumbTop + Math.Max(thickness / 2, ThumbHeight - thickness / 2));
            }
        }
    }

    sealed class DarkScrollPage : Panel
    {
        readonly Panel viewport = new Panel { Dock = DockStyle.Fill, AutoScroll = false, Margin = new Padding(0), BackColor = Theme.Background };
        readonly DarkScrollBar bar = new DarkScrollBar();
        Control content;
        bool arranging;
        public DarkScrollPage()
        {
            Dock = DockStyle.Fill; Margin = new Padding(0); BackColor = Theme.Background; AutoScroll = false;
            Controls.Add(viewport); Controls.Add(bar); bar.Visible = false;
            viewport.SizeChanged += delegate { ArrangeContent(); };
            bar.ValueChanged += delegate { if (content != null) content.Top = -bar.Value; };
            MouseWheel += Wheel; viewport.MouseWheel += Wheel;
        }
        public void SetContent(Control value)
        {
            content = value; value.AutoSize = false; value.Dock = DockStyle.None; value.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            viewport.Controls.Add(value); Watch(value); value.SizeChanged += delegate { ArrangeContent(); }; ArrangeContent();
        }
        void Watch(Control control)
        {
            control.MouseWheel += Wheel;
            control.Enter += delegate { Reveal(control); };
            control.ControlAdded += delegate(object sender, ControlEventArgs e) { Watch(e.Control); ArrangeContent(); };
            foreach (Control child in control.Controls) Watch(child);
        }
        void Wheel(object sender, MouseEventArgs e)
        {
            if (bar.Maximum == 0) return; bar.ScrollWheel(e.Delta);
            var handled = e as HandledMouseEventArgs; if (handled != null) handled.Handled = true;
        }
        void Reveal(Control control)
        {
            if (!Visible || content == null || !control.IsHandleCreated || bar.Maximum == 0) return;
            var point = viewport.PointToClient(control.PointToScreen(Point.Empty));
            if (point.Y < 0) bar.Value += point.Y - Font.Height;
            else if (point.Y + control.Height > viewport.ClientSize.Height) bar.Value += point.Y + control.Height - viewport.ClientSize.Height + Font.Height;
        }
        protected override void OnLayout(LayoutEventArgs e) { base.OnLayout(e); ArrangeContent(); }
        void ArrangeContent()
        {
            if (arranging || content == null || ClientSize.Width <= 0 || ClientSize.Height <= 0) return;
            arranging = true;
            try {
                int width = ClientSize.Width;
                int height = content.GetPreferredSize(new Size(width, 0)).Height;
                bar.Visible = height > ClientSize.Height;
                width = Math.Max(1, ClientSize.Width - (bar.Visible ? bar.Width : 0));
                height = content.GetPreferredSize(new Size(width, 0)).Height;
                content.SetBounds(0, -bar.Value, width, height);
                bar.Viewport = ClientSize.Height; bar.Maximum = height - ClientSize.Height; content.Top = -bar.Value;
            } finally { arranging = false; }
        }
    }

    sealed class PurpleSlider : Control
    {
        int current;
        public int Minimum { get; set; }
        public int Maximum { get; set; }
        public int Value
        {
            get { return current; }
            set { int next = Math.Max(Minimum, Math.Min(Maximum, value)); if (current == next) return;
                current = next; Invalidate(); if (ValueChanged != null) ValueChanged(this, EventArgs.Empty); }
        }
        public event EventHandler ValueChanged;
        public PurpleSlider()
        {
            Minimum = 1; Maximum = 400; Height = 32; TabStop = true; Cursor = Cursors.Hand;
            AccessibleRole = AccessibleRole.Slider;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.UserPaint | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }
        float DpiScale { get { return DeviceDpi / 96F; } }
        void SetFromMouse(int x) { Value = Minimum + (int)Math.Round((Maximum - Minimum) * Math.Max(0, Math.Min(1, (x - 9.0 * DpiScale) / Math.Max(1, Width - 18 * DpiScale)))); }
        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; Focus(); Capture = true; SetFromMouse(e.X); }
        protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (Capture && e.Button == MouseButtons.Left) SetFromMouse(e.X); }
        protected override void OnMouseUp(MouseEventArgs e) { Capture = false; base.OnMouseUp(e); }
        protected override bool IsInputKey(Keys keyData) { return keyData == Keys.Left || keyData == Keys.Right || keyData == Keys.Home || keyData == Keys.End || base.IsInputKey(keyData); }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Left) Value--; else if (e.KeyCode == Keys.Right) Value++;
            else if (e.KeyCode == Keys.Home) Value = Minimum; else if (e.KeyCode == Keys.End) Value = Maximum;
        }
        protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
            float scale = DpiScale, inset = 9 * scale, radius = 6 * scale;
            int y = Height / 2; float end = inset + (Width - 2 * inset) * (Value - Minimum) / (float)Math.Max(1, Maximum - Minimum);
            using (var track = new Pen(Theme.Line, 4 * scale)) g.DrawLine(track, inset, y, Width - inset, y);
            using (var active = new Pen(Enabled ? Theme.Accent : Theme.Muted, 4 * scale)) g.DrawLine(active, inset, y, end, y);
            using (var dot = new SolidBrush(Enabled ? Theme.Accent : Theme.Muted)) g.FillEllipse(dot, end - radius, y - radius, radius * 2, radius * 2);
            if (Focused) using (var ring = new Pen(Theme.Text)) g.DrawEllipse(ring, end - inset, y - inset, inset * 2, inset * 2);
        }
    }

    sealed class PercentSetting : UserControl
    {
        readonly NumericUpDown number = new NumericUpDown();
        readonly PurpleSlider slider = new PurpleSlider();
        public decimal Value { get { return number.Value; } set { number.Value = Math.Max(number.Minimum, Math.Min(number.Maximum, value)); } }
        public PercentSetting(string title, int min, int max)
        {
            AutoScaleMode = AutoScaleMode.Inherit;
            Height = 78; Dock = DockStyle.Fill; BackColor = Theme.Card; Margin = new Padding(0, 3, 0, 0);
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 2, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 22));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 32)); grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            grid.Controls.Add(Theme.Label(title, 10F, Theme.Text), 0, 0);
            number.Minimum = min; number.Maximum = max; number.Value = min; number.DecimalPlaces = 1;
            number.Font = new Font("Segoe UI", 10F); number.BackColor = Theme.Field; number.ForeColor = Theme.Text;
            number.BorderStyle = BorderStyle.FixedSingle; number.Dock = DockStyle.Fill; number.TextAlign = HorizontalAlignment.Right;
            number.AccessibleName = title + " percent"; number.Margin = new Padding(0, 3, 0, 0);
            grid.Controls.Add(number, 1, 0); grid.Controls.Add(Theme.Label(" %", 10F, Theme.Muted), 2, 0);
            slider.Minimum = min; slider.Maximum = max; slider.Value = min; slider.AccessibleName = title;
            slider.Dock = DockStyle.Fill; grid.Controls.Add(slider, 0, 1); grid.SetColumnSpan(slider, 3);
            bool synchronizing = false;
            number.ValueChanged += delegate { if (synchronizing) return; synchronizing = true; slider.Value = (int)Math.Round(number.Value); synchronizing = false; };
            slider.ValueChanged += delegate { if (synchronizing) return; synchronizing = true; number.Value = slider.Value; synchronizing = false; };
            Controls.Add(grid);
        }
    }

    sealed class Launcher : Form
    {
        readonly string root = AppDomain.CurrentDomain.BaseDirectory;
        readonly ComboBox backend = Choice(new[] { "Native OpenXR", "Quad-Views-Foveated" });
        readonly ComboBox mode = Choice(new[] { "Auto / eye tracking", "Fixed foveation" });
        readonly TextBox game = Field();
        readonly TextBox manifest = Field();
        readonly CheckBox outline = Toggle("Show green focus outline");
        readonly CheckBox nativeOuter = Toggle("Use smaller native outer render targets");
        readonly PercentSetting outer = new PercentSetting("Outer resolution", 10, 400);
        readonly PercentSetting inner = new PercentSetting("Inner resolution", 25, 400);
        readonly PercentSetting focusWidth = new PercentSetting("Width", 10, 90);
        readonly PercentSetting focusHeight = new PercentSetting("Height", 10, 90);
        readonly NumericUpDown outerBlur = new NumericUpDown { Minimum = 0, Maximum = 3, DecimalPlaces = 2,
            Increment = 0.25M, BackColor = Theme.Field, ForeColor = Theme.Text, BorderStyle = BorderStyle.FixedSingle,
            Dock = DockStyle.Fill, TextAlign = HorizontalAlignment.Right, AccessibleName = "Outer smoothing in pixels" };
        readonly Label status = Theme.Label("Ready. Close LMU before starting a new session.", 9F, Theme.Muted);
        readonly Label header = Theme.Label("Rendering", 25F, Theme.Text);
        readonly Label subheader = Theme.Label("Four views. One sharper racing experience.", 10F, Theme.Muted);
        readonly Label providerHint = Theme.Label("", 9F, Theme.Muted);
        readonly Label resolutionHint = Theme.Label("", 9F, Theme.Muted);
        readonly Button launch = Theme.Button("Launch LMU", true);
        readonly Button save = Theme.Button("Save settings", false);
        readonly Button renderingNav = Theme.Button("  Rendering", false);
        readonly Button setupNav = Theme.Button("  Setup", false);
        readonly Button browseManifest = Theme.Button("Browse", false);
        readonly DarkScrollPage renderingPage = new DarkScrollPage { Name = "RenderingPage" };
        readonly DarkScrollPage setupPage = new DarkScrollPage { Name = "SetupPage" };
        bool busy;
        string SettingsPath { get { return Path.Combine(root, "launcher-settings.xml"); } }

        static ComboBox Choice(string[] choices)
        {
            var c = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, FlatStyle = FlatStyle.Flat,
                BackColor = Theme.Field, ForeColor = Theme.Text, Dock = DockStyle.Fill, Font = new Font("Segoe UI", 10F) };
            c.Items.AddRange(choices); c.SelectedIndex = 0; return c;
        }
        static TextBox Field() { return new TextBox { Dock = DockStyle.Fill, BackColor = Theme.Field, ForeColor = Theme.Text,
            BorderStyle = BorderStyle.FixedSingle, Font = new Font("Segoe UI", 10F), Margin = new Padding(0, 5, 10, 0) }; }
        static CheckBox Toggle(string text) { return new CheckBox { Text = text, AutoSize = false, Dock = DockStyle.Fill,
            ForeColor = Theme.Text, Font = new Font("Segoe UI", 10F), Cursor = Cursors.Hand, Margin = new Padding(0) }; }
        static TableLayoutPanel Stack(int rows)
        {
            var stack = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = rows,
                Margin = new Padding(0), Padding = new Padding(0), BackColor = Color.Transparent };
            stack.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); return stack;
        }
        static Label Caption(string text) { return Theme.Label(text, 9F, Theme.Muted); }
        static void Link(Control parent, string text, string address, int row)
        {
            var link = new LinkLabel { Text = text, Dock = DockStyle.Fill, LinkColor = Theme.Muted,
                ActiveLinkColor = Theme.Text, VisitedLinkColor = Theme.Muted, Font = new Font("Segoe UI", 9F),
                TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0), LinkBehavior = LinkBehavior.HoverUnderline };
            link.LinkClicked += delegate { try { Process.Start(new ProcessStartInfo(address) { UseShellExecute = true }); }
                catch (Exception ex) { MessageBox.Show(parent.FindForm(), ex.Message, "Could not open link"); } };
            ((TableLayoutPanel)parent).Controls.Add(link, 0, row);
        }
        public Launcher()
        {
            SuspendLayout();
            Text = "LMU Quad View"; BackColor = Theme.Background; ForeColor = Theme.Text;
            Font = new Font("Segoe UI", 10F); AutoScaleDimensions = new SizeF(96F, 96F); AutoScaleMode = AutoScaleMode.Dpi;
            try {
                using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("LmuQuadView.Icon")) {
                    if (stream != null) using (var icon = new Icon(stream)) Icon = (Icon)icon.Clone();
                    else Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                }
            } catch (ArgumentException) { }
            renderingNav.Name = "RenderingNav"; setupNav.Name = "SetupNav";
            save.Name = "SaveButton"; launch.Name = "LaunchButton";
            ClientSize = new Size(1050, 780); MinimumSize = new Size(900, 700); StartPosition = FormStartPosition.CenterScreen;
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
            frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            frame.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 204)); frame.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            Controls.Add(frame); frame.Controls.Add(BuildSidebar(), 0, 0);
            var main = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3,
                Padding = new Padding(28, 14, 28, 14), Margin = new Padding(0) };
            main.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            main.RowStyles.Add(new RowStyle(SizeType.Absolute, 91)); main.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); main.RowStyles.Add(new RowStyle(SizeType.Absolute, 106));
            frame.Controls.Add(main, 1, 0);
            var heading = Stack(2); heading.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); heading.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            heading.Controls.Add(header, 0, 0); heading.Controls.Add(subheader, 0, 1); main.Controls.Add(heading, 0, 0);
            var pages = new Panel { Dock = DockStyle.Fill, Margin = new Padding(0) };
            pages.Controls.Add(setupPage); pages.Controls.Add(renderingPage); main.Controls.Add(pages, 0, 1);
            BuildRendering(); BuildSetup(); main.Controls.Add(BuildFooter(), 0, 2);
            backend.SelectedIndexChanged += delegate { UpdateProvider(); };
            renderingNav.Click += delegate { ShowPage(false); }; setupNav.Click += delegate { ShowPage(true); };
            save.Click += delegate { SaveSettings(); }; launch.Click += delegate { StartGame(); };
            AcceptButton = launch;
            try { Fill(QvfrSettings.Load(SettingsPath)); }
            catch (Exception ex) { status.Text = "Settings could not be loaded: " + ex.Message; }
            UpdateProvider(); ShowPage(false);
            ResumeLayout(true);
        }
        Control BuildSidebar()
        {
            var side = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 7,
                BackColor = Theme.Sidebar, Padding = new Padding(16, 26, 16, 14), Margin = new Padding(0) };
            side.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            side.RowStyles.Add(new RowStyle(SizeType.AutoSize)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 49));
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 49)); side.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 26)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 89)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 43));
            var brand = Stack(3); brand.AutoSize = true; brand.Dock = DockStyle.Top; brand.Margin = new Padding(0, 0, 0, 24);
            brand.RowStyles.Add(new RowStyle(SizeType.AutoSize)); brand.RowStyles.Add(new RowStyle(SizeType.AutoSize)); brand.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var lmu = Theme.Label("LMU", 25F, Theme.Text); lmu.Name = "BrandTitle"; lmu.Font = new Font("Segoe UI", 25F, FontStyle.Bold);
            brand.Controls.Add(lmu, 0, 0); brand.Controls.Add(Theme.Label("QUAD VIEW", 12F, Theme.Accent), 0, 1); brand.Controls.Add(Caption("OPENXR RENDERING"), 0, 2); side.Controls.Add(brand, 0, 0);
            foreach (Control label in brand.Controls) { ((Label)label).AutoSize = true; label.Dock = DockStyle.Top; label.Margin = new Padding(0, 0, 0, 5); }
            foreach (var nav in new[] { renderingNav, setupNav }) { nav.Dock = DockStyle.Fill; nav.TextAlign = ContentAlignment.MiddleLeft; nav.Margin = new Padding(0, 3, 0, 5); nav.FlatAppearance.BorderSize = 0; }
            side.Controls.Add(renderingNav, 0, 1); side.Controls.Add(setupNav, 0, 2);
            side.Controls.Add(Caption("SUPPORT THE PROJECT"), 0, 4);
            var support = Stack(2); support.RowStyles.Add(new RowStyle(SizeType.Absolute, 41)); support.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            var coffee = Theme.Button("Buy me a coffee", false); coffee.Name = "CoffeeButton"; coffee.Dock = DockStyle.Fill; coffee.Font = new Font("Segoe UI", 9F); coffee.Margin = new Padding(0, 0, 0, 4);
            coffee.Click += delegate { OpenLink("https://buymeacoffee.com/dansuzuki"); }; support.Controls.Add(coffee, 0, 0);
            Link(support, "Shop via affiliate links", "https://dansuzuki.com/discounts/", 1); side.Controls.Add(support, 0, 5);
            var tag = Theme.Label("OFFLINE EXPERIMENTAL", 8F, Theme.Muted); side.Controls.Add(tag, 0, 6); return side;
        }
        void BuildRendering()
        {
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, RowCount = 4, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 136)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 231));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 128)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 54)); renderingPage.SetContent(grid);
            var provider = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14) };
            var p = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3, Margin = new Padding(0) };
            p.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); p.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            p.RowStyles.Add(new RowStyle(SizeType.Absolute, 25)); p.RowStyles.Add(new RowStyle(SizeType.Absolute, 32)); p.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            p.Controls.Add(Caption("RENDERING PROVIDER"), 0, 0); p.Controls.Add(Caption("FOVEATION MODE"), 1, 0);
            backend.Margin = new Padding(0, 0, 16, 0); mode.Margin = new Padding(0); p.Controls.Add(backend, 0, 1); p.Controls.Add(mode, 1, 1);
            p.Controls.Add(providerHint, 0, 2); p.SetColumnSpan(providerHint, 2); provider.Controls.Add(p); grid.Controls.Add(provider, 0, 0); grid.SetColumnSpan(provider, 2);
            grid.Controls.Add(SettingCard("Resolution", outer, inner, "Linear scale per dimension.", new Padding(0, 0, 7, 14)), 0, 1);
            grid.Controls.Add(SettingCard("Focus region", focusWidth, focusHeight, "Fraction of the full view. QVFR allows 10–90%.", new Padding(7, 0, 0, 14)), 1, 1);
            var preview = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 12), Padding = new Padding(18, 10, 18, 10) };
            var info = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Margin = new Padding(0) };
            info.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); info.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            info.RowStyles.Add(new RowStyle(SizeType.Absolute, 35)); info.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            outline.Margin = new Padding(0, 0, 12, 0); info.Controls.Add(outline, 0, 0);
            var outlineHint = Caption("Highlights the focus region. Auto follows available eye tracking."); outlineHint.Margin = new Padding(0, 0, 16, 0); info.Controls.Add(outlineHint, 0, 1);
            var blurRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
            blurRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); blurRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 69));
            blurRow.Controls.Add(Theme.Label("Outer smoothing (pixels)", 10F, Theme.Text), 0, 0);
            outerBlur.Margin = new Padding(2, 4, 0, 0); blurRow.Controls.Add(outerBlur, 1, 0); info.Controls.Add(blurRow, 1, 0);
            info.Controls.Add(Caption("Softens peripheral shimmer; adds a small GPU pass. Focus stays sharp. 0 = off."), 1, 1);
            preview.Controls.Add(info); grid.Controls.Add(preview, 0, 2); grid.SetColumnSpan(preview, 2);
            grid.Controls.Add(resolutionHint, 0, 3); grid.SetColumnSpan(resolutionHint, 2);
        }
        Control SettingCard(string title, PercentSetting first, PercentSetting second, string hint, Padding margin)
        {
            var card = new Surface { Dock = DockStyle.Fill, Margin = margin, Padding = new Padding(17, 12, 17, 10) };
            var content = Stack(4); content.RowStyles.Add(new RowStyle(SizeType.Absolute, 29)); content.RowStyles.Add(new RowStyle(SizeType.Absolute, 65));
            content.RowStyles.Add(new RowStyle(SizeType.Absolute, 65)); content.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            var caption = Theme.Label(title, 13F, Theme.Text); caption.Font = new Font("Segoe UI", 13F, FontStyle.Bold);
            content.Controls.Add(caption, 0, 0); content.Controls.Add(first, 0, 1); content.Controls.Add(second, 0, 2); content.Controls.Add(Caption(hint), 0, 3);
            card.Controls.Add(content); return card;
        }
        void BuildSetup()
        {
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, RowCount = 4, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 153)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 165));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 125)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); setupPage.SetContent(grid);
            var browseGame = Theme.Button("Browse", false); browseGame.Click += delegate {
                using (var d = new FolderBrowserDialog { Description = "Select the folder containing Le Mans Ultimate.exe", ShowNewFolderButton = false }) {
                    if (Directory.Exists(game.Text)) d.SelectedPath = game.Text;
                    if (d.ShowDialog(this) == DialogResult.OK) game.Text = d.SelectedPath;
                } };
            grid.Controls.Add(PathCard("Game installation", game, browseGame, "Choose your LMU folder. The launcher checks the supported game build before starting.", false), 0, 0);
            browseManifest.Click += delegate { using (var d = new OpenFileDialog { Filter = "OpenXR layer manifest (*.json)|*.json", CheckFileExists = true }) { if (d.ShowDialog(this) == DialogResult.OK) manifest.Text = d.FileName; } };
            grid.Controls.Add(PathCard("Quad-Views-Foveated", manifest, browseManifest, "Manifest JSON: leave blank to auto-detect an installed copy.", true), 0, 1);
            var options = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14) };
            var stack = Stack(3); stack.RowStyles.Add(new RowStyle(SizeType.Absolute, 23)); stack.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); stack.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            stack.Controls.Add(Caption("ADVANCED"), 0, 0); stack.Controls.Add(nativeOuter, 0, 1);
            stack.Controls.Add(Caption("Experimental GPU savings. Disable to test full-size outer rendering with resampling."), 0, 2); options.Controls.Add(stack); grid.Controls.Add(options, 0, 2);
            grid.Controls.Add(Caption("Select your headset's OpenXR runtime before launch. SteamVR + Sboys3 uses the QVFR provider."), 0, 3);
        }
        Control PathCard(string title, TextBox field, Button browse, string hint, bool link)
        {
            var card = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14), Padding = new Padding(17, 12, 17, 12) };
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = link ? 4 : 3, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 87));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 29)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); if (link) grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
            grid.Controls.Add(Theme.Label(title, 13F, Theme.Text), 0, 0); grid.SetColumnSpan(grid.GetControlFromPosition(0, 0), 2);
            browse.Dock = DockStyle.Fill; browse.Margin = new Padding(0); browse.Font = new Font("Segoe UI", 9F);
            grid.Controls.Add(field, 0, 1); grid.Controls.Add(browse, 1, 1); grid.Controls.Add(Caption(hint), 0, 2); grid.SetColumnSpan(grid.GetControlFromPosition(0, 2), 2);
            if (link) { Link(grid, "Installation guide and download", "https://github.com/mbucchia/Quad-Views-Foveated/wiki", 3); grid.SetColumnSpan(grid.GetControlFromPosition(0, 3), 2); }
            card.Controls.Add(grid); return card;
        }
        Control BuildFooter()
        {
            var footer = Stack(2); footer.RowStyles.Add(new RowStyle(SizeType.Absolute, 49)); footer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            footer.Controls.Add(status, 0, 0);
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, WrapContents = false, Margin = new Padding(0) };
            launch.Width = 180; save.Width = 146; launch.Margin = new Padding(10, 0, 0, 0); save.Margin = new Padding(0);
            buttons.Controls.Add(launch); buttons.Controls.Add(save); footer.Controls.Add(buttons, 0, 1); return footer;
        }
        void ShowPage(bool setup)
        {
            setupPage.Visible = setup; renderingPage.Visible = !setup;
            header.Text = setup ? "Setup" : "Rendering";
            subheader.Text = setup ? "Connect the game, the provider, and your headset." : "Fine-tune image quality where it matters.";
            renderingNav.BackColor = setup ? Theme.Sidebar : Theme.AccentDark; setupNav.BackColor = setup ? Theme.AccentDark : Theme.Sidebar;
            renderingNav.ForeColor = setup ? Theme.Muted : Theme.Text; setupNav.ForeColor = setup ? Theme.Text : Theme.Muted;
        }
        void UpdateProvider()
        {
            bool qvfr = backend.SelectedIndex == 1;
            outer.Enabled = inner.Enabled = focusWidth.Enabled = focusHeight.Enabled = qvfr;
            manifest.Enabled = browseManifest.Enabled = qvfr;
            providerHint.Text = qvfr ? "Uses Quad-Views-Foveated with your active OpenXR runtime." : "Use your runtime's quad-view settings, for example Pimax EVO.";
            resolutionHint.Text = qvfr ? "200% resolution means 2x width and 2x height (4x pixels). Save updates only LMU's QVFR profile and backs up existing settings. Restart LMU after changes."
                : "Resolution and focus size are controlled by your native runtime. Switch to Quad-Views-Foveated to adjust them here. Restart LMU after changes.";
        }
        void Fill(LauncherSettings s)
        {
            game.Text = s.GameDirectory ?? ""; manifest.Text = s.QuadViewsManifest ?? "";
            backend.SelectedIndex = s.Backend == "QVFR" ? 1 : 0; mode.SelectedIndex = s.FoveationMode == "fixed" ? 1 : 0;
            outline.Checked = s.DebugFocus; nativeOuter.Checked = s.NativeOuterResolution;
            outer.Value = s.OuterPercent; inner.Value = s.InnerPercent; focusWidth.Value = s.FocusWidthPercent; focusHeight.Value = s.FocusHeightPercent;
            outerBlur.Value = Math.Max(outerBlur.Minimum, Math.Min(outerBlur.Maximum, s.OuterBlur));
        }
        LauncherSettings Values()
        {
            var s = new LauncherSettings { GameDirectory = game.Text.Trim(), QuadViewsManifest = manifest.Text.Trim(),
                Backend = backend.SelectedIndex == 1 ? "QVFR" : "Native", FoveationMode = mode.SelectedIndex == 1 ? "fixed" : "auto",
                DebugFocus = outline.Checked, NativeOuterResolution = nativeOuter.Checked,
                OuterPercent = outer.Value, InnerPercent = inner.Value, FocusWidthPercent = focusWidth.Value, FocusHeightPercent = focusHeight.Value, OuterBlur = outerBlur.Value };
            QvfrSettings.Validate(s); return s;
        }
        void SaveSettings()
        {
            if (busy) return;
            try { var s = Values(); if (s.Backend == "QVFR") QvfrSettings.SaveProfile(s); QvfrSettings.Save(SettingsPath, s);
                status.Text = s.Backend == "QVFR" ? "Saved. LMU's QVFR profile is applied; existing settings are backed up when changed." : "Saved. Native resolution and focus settings stay in your runtime.";
                status.ForeColor = Theme.Muted; }
            catch (Exception ex) { ShowError(ex.Message); }
        }
        void SetBusy(bool value)
        {
            busy = value; save.Enabled = launch.Enabled = !value; renderingPage.Enabled = setupPage.Enabled = !value;
            launch.Text = value ? "Starting..." : "Launch LMU"; UseWaitCursor = value;
        }
        void StartGame()
        {
            if (busy) return;
            LauncherSettings settings;
            try { settings = Values(); QvfrSettings.Save(SettingsPath, settings); }
            catch (Exception ex) { ShowError(ex.Message); return; }
            SetBusy(true); status.ForeColor = Theme.Muted; status.Text = "Checking the game build and preparing OpenXR...";
            var worker = new BackgroundWorker();
            worker.DoWork += delegate(object sender, DoWorkEventArgs e) { e.Result = LaunchService.Launch(root, settings); };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
                worker.Dispose(); if (IsDisposed || Disposing) return; SetBusy(false);
                if (e.Error != null) { ShowError(e.Error.Message); return; }
                status.ForeColor = Color.FromArgb(149, 207, 169); status.Text = "LMU started (PID " + e.Result + "). Changes apply on your next launch.";
            };
            worker.RunWorkerAsync();
        }
        void OpenLink(string url) { try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch (Exception ex) { ShowError(ex.Message); } }
        void ShowError(string message)
        {
            status.ForeColor = Color.FromArgb(241, 174, 159);
            status.Text = message.Length > 190 ? message.Substring(0, 190) + "..." : message;
            MessageBox.Show(this, message, "LMU Quad View", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Launcher());
        }
    }
}
