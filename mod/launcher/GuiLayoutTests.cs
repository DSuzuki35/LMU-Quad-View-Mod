// Hidden-window smoke tests. Manual geometry/font scaling is a simulation,
// not a substitute for moving the launcher between physical DPI monitors.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Windows.Forms;
using LmuQuadViewLauncher;

class GuiLayoutTests {
    [DllImport("user32.dll")] static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool AreDpiAwarenessContextsEqual(IntPtr first,IntPtr second);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll",EntryPoint="GetWindowLongW")] static extern int GetWindowLong(IntPtr window,int index);
    static void Check(bool good,string message) { if(!good) throw new InvalidOperationException(message); }
    static List<Control> Descendants(Control root) {
        var controls=new List<Control>();controls.Add(root);
        foreach(Control child in root.Controls) controls.AddRange(Descendants(child));
        return controls;
    }
    static Control Named(Control root,string name) {
        var found=root.Controls.Find(name,true);Check(found.Length==1,"Expected exactly one named control: "+name);return found[0];
    }
    static void Layout(Control root) {
        foreach(Control child in root.Controls) Layout(child);
        root.PerformLayout();
    }
    static void Settle(Form form) { for(int i=0;i<3;++i) { Layout(form);Application.DoEvents(); } }
    static void TextFits(Control control) {
        var flags=TextFormatFlags.SingleLine;
        using(var graphics=control.CreateGraphics()) {
            var measured=TextRenderer.MeasureText(graphics,control.Text,control.Font,new Size(Int32.MaxValue,Int32.MaxValue),flags);
            int reserve=control is Button?6:0;
            Check(measured.Width<=control.ClientSize.Width-control.Padding.Horizontal-reserve,
                control.Name+" text width "+measured.Width+" exceeds client "+control.ClientSize.Width);
            Check(measured.Height<=control.ClientSize.Height-control.Padding.Vertical,
                control.Name+" text height "+measured.Height+" exceeds client "+control.ClientSize.Height);
        }
    }
    static void FullyVisible(Control control) {
        Check(control.Visible,control.Name+" is hidden");
        var rectangle=control.RectangleToScreen(control.ClientRectangle);
        for(var parent=control.Parent;parent!=null;parent=parent.Parent) {
            var bounds=parent.RectangleToScreen(parent.ClientRectangle);
            bounds.Inflate(1,1);
            Check(bounds.Contains(rectangle),control.Name+" is clipped by "+parent.GetType().Name+" "+parent.Name);
        }
    }
    static void NoNativeScrollBars(Form form) {
        foreach(var control in Descendants(form)) {
            var scroll=control as ScrollableControl;
            if(scroll==null) continue;
            Check(!scroll.AutoScroll,control.GetType().Name+" unexpectedly enables native AutoScroll");
            if(control.IsHandleCreated) Check((GetWindowLong(control.Handle,-16)&0x00300000)==0,
                control.GetType().Name+" has a native horizontal/vertical scrollbar");
        }
    }
    static void SimulateScale(Form form,float factor) {
        if(factor==1) return;
        var controls=Descendants(form);var fonts=new Dictionary<Control,Font>();
        foreach(var control in controls) { fonts.Add(control,control.Font);control.SuspendLayout();
            var container=control as ContainerControl;if(container!=null)container.AutoScaleMode=AutoScaleMode.None; }
        form.Scale(new SizeF(factor,factor));
        // Snapshot first so inherited fonts are not multiplied repeatedly.
        foreach(var control in controls) {
            var original=fonts[control];control.Font=new Font(original.FontFamily,original.SizeInPoints*factor,original.Style,GraphicsUnit.Point);
        }
        for(int i=controls.Count-1;i>=0;--i)controls[i].ResumeLayout(true);
        Settle(form);
    }
    static void Screenshot(Form form,string path) {
        using(var bitmap=new Bitmap(form.Width,form.Height)) { form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,form.Size));bitmap.Save(path,ImageFormat.Png); }
    }
    static void Page(Form form,string pageName,string navigation,string output,string stem) {
        ((Button)Named(form,navigation)).PerformClick();Settle(form);
        var page=(DarkScrollPage)Named(form,pageName);
        Check(page.Visible,pageName+" did not become active");
        var scroll=(DarkScrollBar)Named(page,"PageScrollBar");
        scroll.Value=0;Settle(form);
        foreach(var name in new[]{"BrandTitle","CoffeeButton","SaveButton","LaunchButton"}) {
            var control=Named(form,name);TextFits(control);FullyVisible(control);
        }
        if(pageName=="RenderingPage") {
            var provider=(ComboBox)Named(form,"ProviderSelector");
            foreach(var item in provider.Items)Check(TextRenderer.MeasureText(item.ToString(),provider.Font).Width+SystemInformation.VerticalScrollBarWidth<provider.Width,"Provider name is clipped");
        }
        NoNativeScrollBars(form);
        var freeNotice=Named(form,"FreeNotice");
        foreach(var line in freeNotice.Text.Split('\n'))Check(TextRenderer.MeasureText(line,freeNotice.Font).Width<=freeNotice.Width,"Optional donation notice is clipped");
        Check(freeNotice.Height>=TextRenderer.MeasureText("Text",freeNotice.Font).Height*3,"Optional donation notice height is clipped");
        Screenshot(form,Path.Combine(output,stem+"-"+pageName+"-top.png"));
        Check(scroll.TabStop&&scroll.AccessibleRole==AccessibleRole.ScrollBar,"Custom scrollbar is not keyboard/accessibility reachable");
        if(scroll.Maximum>0) {
            typeof(DarkScrollBar).GetMethod("OnMouseWheel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(scroll,
                new object[]{new MouseEventArgs(MouseButtons.None,0,0,0,-120)});
            Check(scroll.Value>0,pageName+" mouse wheel did not scroll");
        }
        typeof(DarkScrollBar).GetMethod("OnKeyDown",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(scroll,
            new object[]{new KeyEventArgs(Keys.End)});
        Check(scroll.Value==scroll.Maximum,pageName+" End key did not reach the bottom");Settle(form);
        Panel viewport=null;foreach(Control child in page.Controls)if(child is Panel)viewport=(Panel)child;
        Check(viewport!=null&&viewport.Controls.Count==1,pageName+" viewport content missing");
        var content=viewport.Controls[0];
        Check(content.Width<=viewport.ClientSize.Width+1,pageName+" content has horizontal overflow");
        Check(content.Bottom<=viewport.ClientSize.Height+1,pageName+" last content is unreachable at scroll bottom");
        foreach(var name in new[]{"SaveButton","LaunchButton","CoffeeButton"})FullyVisible(Named(form,name));
        NoNativeScrollBars(form);
        Screenshot(form,Path.Combine(output,stem+"-"+pageName+"-bottom.png"));
        Console.WriteLine(stem+" "+pageName+": text/ancestor bounds, footer reachability, dark scrolling PASS; scroll="+scroll.Maximum);
    }
    [STAThread]
    static int Main(string[] args) {
        try {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            string output=args.Length>0?Path.GetFullPath(args[0]):Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"gui-qa");
            Directory.CreateDirectory(output);
            using(var form=new Launcher()) {
                var flags=BindingFlags.Instance|BindingFlags.NonPublic;
                var original=new LauncherSettings {Backend="QVFR",OuterPercent=10,InnerPercent=208,FocusWidthPercent=23,FocusHeightPercent=50,FocusAspectRatio=2.19283709218806m};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{original});
                var width=(PercentSetting)typeof(Launcher).GetField("focusWidth",flags).GetValue(form);
                var height=(PercentSetting)typeof(Launcher).GetField("focusHeight",flags).GetValue(form);
                Check(Math.Abs(width.Value-50.435253m)<.00001m,"Legacy width was not displayed in equal-axis units");
                var unchanged=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(Math.Abs(unchanged.FocusWidthPercent-23)<.000001m,"Opening settings changed the existing box");
                width.Value=height.Value=50;
                var normalized=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(Math.Abs(normalized.FocusWidthPercent-22.801511m)<.00001m&&normalized.FocusHeightPercent==50,"Equal UI axes did not reach the provider correctly");
                Check(normalized.FocusAspectRatio==original.FocusAspectRatio,"Calibration lost on save");
                var vector=new LauncherSettings {Backend="VectorXR",FoveationMode="fixed",RenderProfiling=true,RenderProfileSeconds=180,VectorXrManifest=@"E:\VectorXR\layer.json",QuadViewsManifest=@"E:\QVFR\layer.json"};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{vector});
                var vectorValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(vectorValues.RenderProfiling,"Profiling GUI setting lost");
                Check(vectorValues.RenderProfileSeconds==180&&Named(form,"RenderProfileSeconds").Enabled,"Capture length GUI setting lost/disabled");
                ((CheckBox)Named(form,"DiagnosticsEnabled")).Checked=false;
                var quietValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(!quietValues.DiagnosticsEnabled&&!Named(form,"RenderProfileSeconds").Enabled,"Diagnostics-off did not disable captures");
                ((CheckBox)Named(form,"DiagnosticsEnabled")).Checked=true;
                Check(Named(form,"RenderProfileSeconds").Enabled,"Re-enabling diagnostics lost saved capture preference");
                Check(vectorValues.Backend=="VectorXR"&&vectorValues.VectorXrManifest==vector.VectorXrManifest&&vectorValues.QuadViewsManifest==vector.QuadViewsManifest,"VectorXR GUI settings roundtrip");
                Check(!Named(form,"OuterResolution").Enabled&&!Named(form,"InnerResolution").Enabled,"VectorXR misleadingly enables QVFR resolution controls");
                Check(Named(form,"VectorXrManifest").Enabled&&!Named(form,"QvfrManifest").Enabled,"Wrong manifest picker enabled for VectorXR");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=1;
                var switched=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(switched.VectorXrManifest==vector.VectorXrManifest&&switched.QuadViewsManifest==vector.QuadViewsManifest,"Switching provider lost a manifest path");
            }
            foreach(float scale in new[]{1f,1.5f,2f})foreach(bool minimum in new[]{false,true}) {
                using(var form=new Launcher()) {
                    form.Opacity=0;form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-16000,-16000);
                    if(minimum)form.Size=form.MinimumSize;
                    form.Show();Settle(form);
                    Check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),new IntPtr(-4)),"Actual thread DPI awareness is not PerMonitorV2");
                    Check(form.Icon!=null&&form.Icon.Width>0,"Launcher icon is missing");
                    Check(typeof(Launcher).Assembly.GetManifestResourceInfo("LmuQuadView.Icon")!=null,"Custom icon resource is missing");
                    Console.WriteLine("Actual hidden HWND DPI="+GetDpiForWindow(form.Handle)+"; thread awareness=PerMonitorV2. Next case adds SIMULATED geometry/font scaling relative to this DPI; it is not a physical Windows display-scale test.");
                    SimulateScale(form,scale);
                    string stem="SIMULATED-"+(int)(scale*100)+"pct-"+(minimum?"minimum":"default");
                    Page(form,"RenderingPage","RenderingNav",output,stem);
                    Page(form,"SetupPage","SetupNav",output,stem);
                    form.Close();
                }
            }
            Console.WriteLine("GUI layout smoke tests passed. Physical-monitor DPI transition testing remains separate.");return 0;
        } catch(Exception error) { Console.Error.WriteLine(error);return 1; }
    }
}
