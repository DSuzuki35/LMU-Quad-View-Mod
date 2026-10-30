using System;
using System.IO;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using LmuQuadViewLauncher;
class LaunchServiceTests {
    static void Check(bool yes,string message) { if(!yes) throw new Exception(message); }
    static void Reject(Action action,string message) { bool rejected=false;try { action(); } catch { rejected=true; } Check(rejected,message); }
    static int Main() {
        var root=Path.Combine(Path.GetTempPath(),"lmu-launch-tests-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        var originalVrs=Environment.GetEnvironmentVariable("LMU_QV_VRS_EXCLUSIVE");
        try {
            var providerRoot=Path.Combine(root,"provider's files");Directory.CreateDirectory(providerRoot);
            var dll=Path.Combine(providerRoot,"provider.dll");File.WriteAllBytes(dll,new byte[]{1,2,3});
            var manifest=Path.Combine(providerRoot,"quad.json");
            var original="{\"file_format_version\":\"1.0.0\",\"api_layer\":{\"name\":\"XR_APILAYER_MBUCCHIA_quad_views_foveated\",\"library_path\":\"provider.dll\",\"api_version\":\"1.0\",\"disable_environment\":\"DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated\",\"enable_environment\":\"ENABLE_TEST\"}}";
            File.WriteAllText(manifest,original);
            var provider=LaunchService.ReadProviderManifest(manifest);
            Check(provider.Library==dll,"Provider DLL moved from original folder");
            var plan=LaunchService.MakeLayerPlan(root,"QVFR",provider,Path.Combine(root,"run"));
            Check(plan.Layers=="XR_APILAYER_LMU_quadview;XR_APILAYER_MBUCCHIA_quad_views_foveated","Windows layer ordering");
            var json=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(plan.ExplicitManifest));
            var layer=(Dictionary<string,object>)json["api_layer"];
            Check(!layer.ContainsKey("disable_environment")&&!layer.ContainsKey("enable_environment"),"Explicit activation switches retained");
            Check((string)layer["library_path"]==dll,"Explicit DLL path changed");
            Check(File.ReadAllText(manifest)==original,"Installed manifest modified");
            var settings=new LauncherSettings {GameDirectory=Path.Combine(root,"game's folder"),Backend="QVFR",FoveationMode="fixed",DebugFocus=true,NativeOuterResolution=false,OuterBlur=1.25m};
            Environment.SetEnvironmentVariable("LMU_QV_VRS_EXCLUSIVE","1");
            var info=LaunchService.MakeStartInfo(root,settings,plan,Path.Combine(root,"quad.log"));
            Check(!info.UseShellExecute&&info.Arguments=="+XR"&&info.FileName==Path.Combine(settings.GameDirectory,"Le Mans Ultimate.exe"),"Must launch executable directly");
            Check(info.EnvironmentVariables[plan.DisableVariable]=="1","Implicit QVFR not disabled in child");
            Check(Environment.GetEnvironmentVariable(plan.DisableVariable)==null,"Host process environment modified");
            Check(info.EnvironmentVariables["LMU_QV_FOVEATION_MODE"]=="fixed"&&info.EnvironmentVariables["LMU_QV_DEBUG_FOCUS"]=="1","Mode/outline lost");
            Check(info.EnvironmentVariables["LMU_QV_OUTER_BLUR"]=="1.25","Blur control not passed as invariant decimal");
            Check(info.EnvironmentVariables["LMU_QV_NATIVE_OUTER"]=="0"&&info.EnvironmentVariables["LMU_QV_VRS_EXCLUSIVE"]=="0","Inherited experimental controls leak");
            Check(info.EnvironmentVariables["LMU_QV_USE_RUNTIME_SETTINGS"]=="1"&&info.EnvironmentVariables["LMU_QV_FOCUS_PIXELS_PERCENT"]=="100","Provider dimensions scaled twice");
            var native=LaunchService.MakeLayerPlan(root,"Native",provider,null);
            Check(native.Layers=="XR_APILAYER_LMU_quadview"&&native.DisableVariable==provider.DisableVariable,"Native did not suppress implicit emulation");
            Reject(delegate{LaunchService.MakeLayerPlan(root+";bad","Native",null,null);},"Search separator allowed in root");
            Reject(delegate{LaunchService.MakeLayerPlan(root,"QVFR",provider,Path.Combine(root,"run"));},"Existing manifest directory overwritten");
            File.Delete(dll);Reject(delegate{LaunchService.ReadProviderManifest(manifest);},"Missing provider DLL accepted");
            File.WriteAllBytes(dll,new byte[]{1,2,3});File.WriteAllText(manifest,original.Replace("XR_APILAYER_MBUCCHIA_quad_views_foveated","wrong_layer"));
            Reject(delegate{LaunchService.ReadProviderManifest(manifest);},"Unrelated layer accepted");
            var fakeExe=Path.Combine(root,"fake.exe");File.WriteAllText(fakeExe,"not LMU");
            Reject(delegate{LaunchService.ValidateExecutable(fakeExe);},"Pinned game hash guard weakened");
            Reject(delegate{LaunchService.Prepare(root,settings);},"Prepare accepted missing game/package");
            Console.WriteLine("LaunchService tests passed: direct launch, fixed/native modes, layer order/isolation, path quoting, manifests, environment reset, pinned hash guard");
            return 0;
        } finally {
            Environment.SetEnvironmentVariable("LMU_QV_VRS_EXCLUSIVE",originalVrs);
            if(Path.GetFullPath(root).StartsWith(Path.GetFullPath(Path.GetTempPath()),StringComparison.OrdinalIgnoreCase)) Directory.Delete(root,true);
        }
    }
}
