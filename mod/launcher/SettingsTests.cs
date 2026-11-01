using System;
using System.Globalization;
using System.IO;
using System.Threading;
using LmuQuadViewLauncher;
class SettingsTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static void Rejected(Action action) { bool rejected=false; try { action(); } catch(ArgumentException) { rejected=true; } catch(InvalidDataException) { rejected=true; } Require(rejected,"Invalid settings accepted"); }
    static void Main()
    {
        string folder=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings-test-"+Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
        var s=new LauncherSettings { Backend="QVFR", OuterPercent=10,InnerPercent=200,FocusWidthPercent=33,FocusHeightPercent=25 };
        Require(typeof(LauncherSettings).GetProperty("FocusOffsetXPercent")!=null,"Horizontal offset setting missing");
        Require(typeof(LauncherSettings).GetProperty("FocusOffsetYPercent")!=null,"Vertical offset setting missing");
        string old="# custom\n[exe:DCS.exe]\nfocus_multiplier=1.7\n";
        Thread.CurrentThread.CurrentCulture=CultureInfo.GetCultureInfo("de-DE");
        string text=QvfrSettings.ProfileText(old,s);
        Require(text.StartsWith(old.TrimEnd('\n')),"Other app settings changed");
        Require(text.Contains("peripheral_multiplier=0.1\nfocus_multiplier=2\n"),"Linear resolution / culture conversion");
        Require(text.Contains("horizontal_focus_section=0.33\nvertical_focus_section=0.25\n"),"Independent size conversion");
        Require(QvfrSettings.ProfileText(text,s)==text,"Profile idempotency");
        string scoped=QvfrSettings.ProfileText(text+"sharpen_focus_view=0.1\n",s);
        Require(scoped.Contains("[exe:Le Mans Ultimate.exe]\nsharpen_focus_view=0.1"),"Unowned suffix key moved into another app section");
        Require(QvfrSettings.ProfileText(scoped,s)==scoped,"Scope preservation is not idempotent");
        string path=Path.Combine(folder,"settings.cfg"); File.WriteAllText(path,old);
        QvfrSettings.SaveProfile(path,s); string[] backups=Directory.GetFiles(folder,"*.lmu-backup-*");
        Require(backups.Length==1 && File.ReadAllText(backups[0])==old,"Backup preservation");
        QvfrSettings.SaveProfile(path,s); Require(Directory.GetFiles(folder,"*.lmu-backup-*").Length==1,"Unchanged profile rewritten");
        s.Backend="Native"; s.OuterPercent=100; QvfrSettings.SaveProfile(path,s); Require(File.ReadAllText(path)==text,"Native overwrote QVFR");
        s.Backend="QVFR"; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterPercent=10;
        Rejected(delegate{QvfrSettings.ProfileText("# BEGIN LMU QUAD VIEW MOD PROFILE",s);});
        Rejected(delegate{QvfrSettings.ProfileText("# END LMU QUAD VIEW MOD PROFILE\n# BEGIN LMU QUAD VIEW MOD PROFILE",s);});
        s.OuterPercent=9; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterPercent=10;
        s.FocusWidthPercent=100; Rejected(delegate{QvfrSettings.Validate(s);}); s.FocusWidthPercent=33;
        s.FocusHeightPercent=9; Rejected(delegate{QvfrSettings.Validate(s);}); s.FocusHeightPercent=25;
        s.OuterBlur=4; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterBlur=1.25m;
        s.FocusAspectRatio=2.19283709218806m;s.RenderProfiling=true;s.RenderProfileSeconds=300;s.DiagnosticsEnabled=false;s.StereoDrawAudit=true;s.StereoShaderCapture=true;s.StereoPairProof=true;s.CpuUploadReuse=true;
        s.FocusOffsetXPercent=-50;s.FocusOffsetYPercent=12.5m;
        string xml=Path.Combine(folder,"launcher-settings.xml"); QvfrSettings.Save(xml,s); var loaded=QvfrSettings.Load(xml);
        Require(loaded.FocusOffsetXPercent==-50&&loaded.FocusOffsetYPercent==12.5m,"Offsets did not roundtrip invariantly");
        s.FocusOffsetXPercent=-50.1m;Rejected(delegate{QvfrSettings.Validate(s);});s.FocusOffsetXPercent=50;
        s.FocusOffsetYPercent=50.1m;Rejected(delegate{QvfrSettings.Validate(s);});s.FocusOffsetYPercent=-50;
        QvfrSettings.Validate(s);s.FocusOffsetXPercent=-50;s.FocusOffsetYPercent=12.5m;
        Require(QvfrSettings.ProfileText(old,s)==text,"Offsets leaked into provider profile");
        Require(loaded.RenderProfiling,"Profiling setting did not roundtrip");
        Require(!loaded.DiagnosticsEnabled&&new LauncherSettings().DiagnosticsEnabled,"Diagnostics off setting/default lost");
        Require(loaded.StereoDrawAudit&&!new LauncherSettings().StereoDrawAudit,"Stereo audit preference/default lost");
        Require(loaded.StereoShaderCapture&&!new LauncherSettings().StereoShaderCapture,"Shader capture preference/default lost");
        Require(loaded.StereoPairProof&&loaded.CpuUploadReuse&&!new LauncherSettings().StereoPairProof&&!new LauncherSettings().CpuUploadReuse&&!new LauncherSettings().CpuUploadBatch,"Prototype preferences/defaults lost");
        s.CpuUploadBatch=true;QvfrSettings.Save(xml,s);Require(QvfrSettings.Load(xml).CpuUploadBatch,"CPU upload fast-path preference lost");
        Require(loaded.RenderProfileSeconds==300&&new LauncherSettings().RenderProfileSeconds==120,"Capture length roundtrip/default");
        s.RenderProfileSeconds=29;Rejected(delegate{QvfrSettings.Validate(s);});s.RenderProfileSeconds=301;Rejected(delegate{QvfrSettings.Validate(s);});s.RenderProfileSeconds=300;
        Require(!new LauncherSettings().RenderProfiling,"Profiling enabled by default");
        Require(loaded.FocusWidthPercent==33 && loaded.FocusHeightPercent==25 && loaded.InnerPercent==200 && loaded.OuterBlur==1.25m,"Settings roundtrip");
        Require(loaded.FocusAspectRatio==s.FocusAspectRatio,"Measured FOV calibration lost on roundtrip");
        File.WriteAllText(xml,"<QuadView><GameDirectory>D:\\Game</GameDirectory><DebugFocus>true</DebugFocus></QuadView>");
        loaded=QvfrSettings.Load(xml); Require(loaded.Backend=="Native" && loaded.DebugFocus && loaded.OuterPercent==25,"Old settings migration");
        Require(loaded.DiagnosticsEnabled,"Existing settings must retain diagnostic logging");
        Require(!loaded.StereoDrawAudit,"Existing settings must not enable draw auditing");
        Require(!loaded.StereoShaderCapture,"Existing settings must not enable shader capture");
        Require(!loaded.StereoPairProof&&!loaded.CpuUploadReuse&&!loaded.CpuUploadBatch,"Existing settings must not enable prototype options");
        Require(loaded.FocusOffsetXPercent==0&&loaded.FocusOffsetYPercent==0,"Old settings must default offsets to zero");
        Require(loaded.FocusAspectRatio==0,"Old settings invent headset geometry");
        s.Backend="VectorXR";s.VectorXrManifest=@"E:\VectorXR\layer.json";s.QuadViewsManifest=@"E:\QVFR\layer.json";
        QvfrSettings.Save(xml,s);loaded=QvfrSettings.Load(xml);
        Require(loaded.Backend=="VectorXR"&&loaded.VectorXrManifest==s.VectorXrManifest&&loaded.QuadViewsManifest==s.QuadViewsManifest,"Separate provider settings roundtrip");
        string unchangedProfile=File.ReadAllText(path);QvfrSettings.SaveProfile(path,s);
        Require(File.ReadAllText(path)==unchangedProfile,"VectorXR overwrote the mbucchia profile");
        Console.WriteLine("C# settings tests passed: independent dimensions, invariant culture, backups, preservation, validation, migration.");
    }
}
