using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace LmuQuadViewLauncher {
    public static class LaunchService {
        public const string SupportedExecutableHash="66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF";
        const string ProviderName="XR_APILAYER_MBUCCHIA_quad_views_foveated";
        const string AdapterName="XR_APILAYER_LMU_quadview";
        internal sealed class ProviderManifest {
            public string Path,Library,DisableVariable;
            public Dictionary<string,object> Document;
        }
        internal sealed class LayerPlan {
            public string Paths,Layers,DisableVariable,ProviderPath,ExplicitManifest,Library;
        }
        static string Value(Dictionary<string,object> dictionary,string key) {
            object value;return dictionary.TryGetValue(key,out value)?value as string:null;
        }
        static Dictionary<string,object> ReadJson(string path) {
            try { return new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(path)); }
            catch(Exception error) { throw new InvalidDataException("Cannot read OpenXR manifest: "+path,error); }
        }
        static Dictionary<string,object> Layer(Dictionary<string,object> document) {
            object raw;return document!=null&&document.TryGetValue("api_layer",out raw)?raw as Dictionary<string,object>:null;
        }
        internal static ProviderManifest ReadProviderManifest(string path,bool requireLibrary=true) {
            path=System.IO.Path.GetFullPath(path);
            var document=ReadJson(path);var layer=Layer(document);
            if(layer==null||Value(layer,"name")!=ProviderName) throw new InvalidDataException("Select the Quad-Views-Foveated JSON manifest.");
            var disable=Value(layer,"disable_environment");
            if(String.IsNullOrEmpty(disable)||!Regex.IsMatch(disable,"^[A-Za-z_][A-Za-z0-9_]*$")||disable.IndexOf("quad",StringComparison.OrdinalIgnoreCase)<0)
                throw new InvalidDataException("QVFR manifest has no recognizable disable_environment switch; safe layer ordering is unavailable.");
            var library=Value(layer,"library_path");
            if(String.IsNullOrWhiteSpace(library)) throw new InvalidDataException("QVFR manifest has no library_path.");
            if(!System.IO.Path.IsPathRooted(library)) library=System.IO.Path.Combine(System.IO.Path.GetDirectoryName(path),library);
            library=System.IO.Path.GetFullPath(library);
            if(requireLibrary&&!File.Exists(library)) throw new FileNotFoundException("QVFR library was not found. Repair its installation.",library);
            return new ProviderManifest {Path=path,Library=library,DisableVariable=disable,Document=document};
        }
        static string FindProviderManifest() {
            var candidates=new List<string>();
            foreach(var hive in new[]{RegistryHive.LocalMachine,RegistryHive.CurrentUser}) {
                try {
                    using(var baseKey=RegistryKey.OpenBaseKey(hive,RegistryView.Registry64))
                    using(var key=baseKey.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit",false)) {
                        if(key!=null) foreach(var name in key.GetValueNames()) if(name.EndsWith(".json",StringComparison.OrdinalIgnoreCase)) candidates.Add(name);
                    }
                } catch(System.Security.SecurityException) { } catch(UnauthorizedAccessException) { }
            }
            foreach(var directory in new[]{"OpenXR-Quad-Views-Foveated","Quad-Views-Foveated"}) {
                var path=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),directory);
                if(Directory.Exists(path)) candidates.AddRange(Directory.GetFiles(path,"*.json"));
            }
            foreach(var path in candidates) {
                if(!File.Exists(path)) continue;
                try { var layer=Layer(ReadJson(path));if(layer!=null&&Value(layer,"name")==ProviderName) return path; }
                catch(InvalidDataException) { } catch(IOException) { } catch(UnauthorizedAccessException) { }
            }
            return null;
        }
        static void CheckSearchPath(string path) {
            if(path.IndexOf(';')>=0) throw new ArgumentException("Package/run paths cannot contain ';', the OpenXR search-path separator.");
        }
        internal static LayerPlan MakeLayerPlan(string root,string backend,ProviderManifest provider,string runDirectory) {
            root=System.IO.Path.GetFullPath(root);CheckSearchPath(root);
            var plan=new LayerPlan {Paths=root,Layers=AdapterName,DisableVariable=provider==null?null:provider.DisableVariable};
            if(backend=="Native") return plan;
            if(backend!="QVFR"||provider==null) throw new InvalidOperationException("Install Quad-Views-Foveated or select its JSON manifest.");
            if(String.IsNullOrWhiteSpace(runDirectory)) throw new ArgumentException("Unique layer run directory is required.");
            runDirectory=System.IO.Path.GetFullPath(runDirectory);CheckSearchPath(runDirectory);
            if(Directory.Exists(runDirectory)||File.Exists(runDirectory)) throw new IOException("Layer run directory already exists; refusing to overwrite it.");
            var document=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(new JavaScriptSerializer().Serialize(provider.Document));
            var layer=Layer(document);layer["library_path"]=provider.Library;
            layer.Remove("disable_environment");layer.Remove("enable_environment");
            Directory.CreateDirectory(runDirectory);
            plan.ExplicitManifest=System.IO.Path.Combine(runDirectory,ProviderName+".json");
            WriteJson(plan.ExplicitManifest,document);
            plan.Paths=root+";"+runDirectory;plan.Layers=AdapterName+";"+ProviderName;
            plan.ProviderPath=provider.Path;plan.Library=provider.Library;
            return plan;
        }
        internal static ProcessStartInfo MakeStartInfo(string root,LauncherSettings settings,LayerPlan plan,string log) {
            var game=System.IO.Path.GetFullPath(settings.GameDirectory);
            var info=new ProcessStartInfo {FileName=System.IO.Path.Combine(game,"Le Mans Ultimate.exe"),Arguments="+XR",WorkingDirectory=game,UseShellExecute=false,WindowStyle=ProcessWindowStyle.Normal};
            // Never inherit experiments or stale capture/benchmark triggers from another launch.
            var inherited=new List<string>();foreach(string key in info.EnvironmentVariables.Keys) if(key.StartsWith("LMU_QV_",StringComparison.OrdinalIgnoreCase)) inherited.Add(key);
            foreach(var key in inherited) info.EnvironmentVariables.Remove(key);
            var environment=info.EnvironmentVariables;
            environment["XR_API_LAYER_PATH"]=plan.Paths;environment["XR_ENABLE_API_LAYERS"]=plan.Layers;
            if(!String.IsNullOrEmpty(plan.DisableVariable)) environment[plan.DisableVariable]="1";
            environment["LMU_QV_MODE"]="quad";environment["LMU_QV_USE_RUNTIME_SETTINGS"]="1";
            environment["LMU_QV_FOVEATION_MODE"]=settings.FoveationMode;environment["LMU_QV_LOG"]=log;
            environment["LMU_QV_DEBUG_FOCUS"]=settings.DebugFocus?"1":"0";
            environment["LMU_QV_OUTER_BLUR"]=settings.OuterBlur.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_NATIVE_OUTER"]=settings.NativeOuterResolution?"1":"0";
            environment["LMU_QV_PIXEL_STATS"]="0";environment["LMU_QV_VRS_EXCLUSIVE"]="0";environment["LMU_QV_OUTER_SHADING_RATE"]="2";
            foreach(var key in new[]{"CAPTURE_DIR","CAPTURE_TRIGGER","GPU_TIMING_TRIGGER","BENCH_CONTROL"}) environment["LMU_QV_"+key]="";
            foreach(var key in new[]{"OUTER_PIXELS_PERCENT","FOCUS_PIXELS_PERCENT","FOCUS_BOX_PERCENT","FOCUS_WIDTH_PERCENT","FOCUS_HEIGHT_PERCENT"}) environment["LMU_QV_"+key]="100";
            return info;
        }
        static string HashFile(string path) {
            using(var input=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","");
        }
        internal static void ValidateExecutable(string executable) {
            if(!File.Exists(executable)) throw new FileNotFoundException("Select the folder containing Le Mans Ultimate.exe.",executable);
            if(!String.Equals(HashFile(executable),SupportedExecutableHash,StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("LMU executable differs from the supported build. This mod needs validation for that game version.");
        }
        static void WriteJson(string path,object value) {
            File.WriteAllText(path,new JavaScriptSerializer().Serialize(value),new UTF8Encoding(false));
        }
        static string MetadataPath(ProcessStartInfo info) { return System.IO.Path.ChangeExtension(info.EnvironmentVariables["LMU_QV_LOG"],null)+"-run.json"; }
        public static ProcessStartInfo Prepare(string root,LauncherSettings settings) {
            QvfrSettings.Validate(settings);
            root=System.IO.Path.GetFullPath(root);CheckSearchPath(root);
            ValidateExecutable(System.IO.Path.Combine(System.IO.Path.GetFullPath(settings.GameDirectory),"Le Mans Ultimate.exe"));
            var processes=Process.GetProcessesByName("Le Mans Ultimate");
            bool running=processes.Length!=0;foreach(var process in processes) process.Dispose();
            if(running) throw new InvalidOperationException("LMU is already running. Close it before launching again.");
            foreach(var relative in new[]{"XR_APILAYER_LMU_quadview.json",@"build\lmu_quadview.dll"}) if(!File.Exists(System.IO.Path.Combine(root,relative))) throw new FileNotFoundException("The mod package is incomplete: "+relative);
            var ownLayer=Layer(ReadJson(System.IO.Path.Combine(root,"XR_APILAYER_LMU_quadview.json")));
            if(ownLayer==null||Value(ownLayer,"name")!=AdapterName) throw new InvalidDataException("The LMU layer manifest is invalid.");
            var ownLibrary=Value(ownLayer,"library_path");
            if(String.IsNullOrWhiteSpace(ownLibrary)||!String.Equals(System.IO.Path.GetFullPath(System.IO.Path.Combine(root,ownLibrary)),System.IO.Path.Combine(root,@"build\lmu_quadview.dll"),StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The LMU manifest must reference this package's build/lmu_quadview.dll.");
            string providerPath=settings.Backend=="QVFR"&&!String.IsNullOrWhiteSpace(settings.QuadViewsManifest)?settings.QuadViewsManifest:FindProviderManifest();
            ProviderManifest provider=String.IsNullOrWhiteSpace(providerPath)?null:ReadProviderManifest(providerPath,settings.Backend=="QVFR");
            if(settings.Backend=="QVFR"&&provider==null) throw new FileNotFoundException("Quad-Views-Foveated is not installed. Install it or select its JSON manifest.");
            var logDirectory=System.IO.Path.Combine(root,"logs");
            var stamp=DateTime.Now.ToString("yyyyMMdd-HHmmss",CultureInfo.InvariantCulture)+"-"+Guid.NewGuid().ToString("N").Substring(0,8);
            var plan=MakeLayerPlan(root,settings.Backend,provider,System.IO.Path.Combine(logDirectory,"layers-"+stamp));
            Directory.CreateDirectory(logDirectory);
            var log=System.IO.Path.Combine(logDirectory,"quad-"+stamp+".log");
            var info=MakeStartInfo(root,settings,plan,log);
            WriteJson(MetadataPath(info),new {Status="prepared",Pid=(int?)null,Mode="quad",Backend=settings.Backend,FoveationMode=settings.FoveationMode,
                Exe=info.FileName,ExeSHA256=SupportedExecutableHash,LayerSHA256=HashFile(System.IO.Path.Combine(root,@"build\lmu_quadview.dll")),
                ProviderManifest=plan.ProviderPath,ExplicitManifest=plan.ExplicitManifest,EnabledLayers=plan.Layers,Log=log,
                DebugFocus=settings.DebugFocus,NativeOuterResolution=settings.NativeOuterResolution,UseRuntimeSettings=true,
                OuterBlur=settings.OuterBlur,
                OuterPercent=settings.OuterPercent,InnerPercent=settings.InnerPercent,FocusWidthPercent=settings.FocusWidthPercent,FocusHeightPercent=settings.FocusHeightPercent,
                PreparedUTC=DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture)});
            return info;
        }
        public static int Launch(string root,LauncherSettings settings) {
            var info=Prepare(root,settings);
            try {
            // Preparation is testable without touching the user's QVFR profile.
            // Only the actual launch commits this configuration, after all guards.
            if(settings.Backend=="QVFR") QvfrSettings.SaveProfile(settings);
            using(var process=Process.Start(info)) {
                if(process==null) throw new InvalidOperationException("Windows did not start LMU.");
                int pid=process.Id;
                try {
                    var path=MetadataPath(info);var metadata=ReadJson(path);
                    metadata["Status"]="started";metadata["Pid"]=pid;metadata["StartedUTC"]=DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture);WriteJson(path,metadata);
                } catch(IOException) { } catch(UnauthorizedAccessException) { } // Game already started; do not report a false launch failure.
                return pid;
            }
            } catch(Exception error) {
                try {
                    var path=MetadataPath(info);var metadata=ReadJson(path);
                    metadata["Status"]="failed";metadata["Error"]=error.Message;WriteJson(path,metadata);
                } catch(IOException) { } catch(UnauthorizedAccessException) { }
                throw;
            }
        }
    }
}
