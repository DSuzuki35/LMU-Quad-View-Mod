using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace LmuQuadViewLauncher {
    // Immutable settings let the comparison survive slider changes and saves.
    public sealed class PixelSettings {
        public readonly decimal Outer, Inner, Width, Height;
        public readonly bool NativeOuter;
        public PixelSettings(decimal outer,decimal inner,decimal width,decimal height,bool nativeOuter) {
            Outer=outer;Inner=inner;Width=width;Height=height;NativeOuter=nativeOuter;
        }
        public bool Valid { get { return Outer>=10&&Outer<=400&&Inner>=25&&Inner<=400&&
            Width>=10&&Width<=90&&Height>=10&&Height<=90&&Outer<=Inner*Math.Min(Width,Height)/100m; } }
    }

    public sealed class PixelEstimate {
        public readonly decimal Outer, Focus;
        public decimal Total { get { return Outer+Focus; } }
        public PixelEstimate(decimal outer,decimal focus) { Outer=outer;Focus=focus; }
        static decimal Square(decimal value) { return value*value; }
        public static PixelEstimate Calculate(PixelSettings settings,PixelMeasurement measurement) {
            if(settings==null||!settings.Valid)throw new ArgumentException("Invalid pixel estimate settings.");
            decimal outer,focus;
            if(measurement==null) {
                // Units: the area of two full-view, 100%-scale eye images.
                outer=Square(settings.Outer/100m);
                focus=Square(settings.Inner/100m)*settings.Width*settings.Height/10000m;
            } else {
                if(measurement.Backend!="QVFR")throw new ArgumentException("Only QVFR settings can predict resolution here.");
                var previous=measurement.Settings;
                outer=measurement.OuterPixels*Square(settings.Outer/previous.Outer);
                focus=measurement.FocusPixels*Square(settings.Inner/previous.Inner)*
                    (settings.Width/previous.Width)*(settings.Height/previous.Height);
            }
            // Without the native outer bank, LMU renders outer scenes in the
            // full common focus buffer before resampling to the outer outputs.
            return new PixelEstimate(settings.NativeOuter?outer:focus,focus);
        }
    }

    public sealed class PixelMeasurement {
        public readonly string Backend;
        public readonly PixelSettings Settings;
        public readonly decimal OuterPixels, FocusPixels;
        public readonly DateTime TimestampUtc;
        PixelMeasurement(string backend,PixelSettings settings,decimal outer,decimal focus,DateTime timestamp) {
            Backend=backend;Settings=settings;OuterPixels=outer;FocusPixels=focus;TimestampUtc=timestamp;
        }
        static decimal Number(Dictionary<string,object> values,string key) {
            object value;
            if(!values.TryGetValue(key,out value)||value==null||value is bool||value is string)throw new FormatException();
            return Convert.ToDecimal(value,CultureInfo.InvariantCulture);
        }
        static bool Flag(Dictionary<string,object> values,string key) {
            object value;if(!values.TryGetValue(key,out value)||!(value is bool))throw new FormatException();
            return (bool)value;
        }
        public static PixelMeasurement Parse(string log,string json,DateTime timestamp) {
            try {
                var values=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json);
                if(values==null||!values.ContainsKey("Status")||!values.ContainsKey("Mode")||!values.ContainsKey("Backend")||
                    !Equals(values["Status"],"started")||!Equals(values["Mode"],"quad")||!Flag(values,"UseRuntimeSettings"))return null;
                string backend=values["Backend"] as string;
                if(backend!="QVFR"&&backend!="Native"&&backend!="VectorXR")return null;
                var settings=new PixelSettings(Number(values,"OuterPercent"),Number(values,"InnerPercent"),
                    Number(values,"FocusWidthPercent"),Number(values,"FocusHeightPercent"),Flag(values,"NativeOuterResolution"));
                if(backend=="QVFR"&&!settings.Valid)return null;
                var matches=Regex.Matches(log??"",@"Actual swapchains outer0=(\d+)x(\d+) outer1=(\d+)x(\d+) focus=(\d+)x(\d+)(?=\s|$)");
                if(matches.Count==0)return null;
                var match=matches[matches.Count-1];var size=new decimal[6];
                for(int i=0;i<size.Length;i++)
                    if(!Decimal.TryParse(match.Groups[i+1].Value,NumberStyles.None,CultureInfo.InvariantCulture,out size[i])||size[i]<1||size[i]>65536)return null;
                return new PixelMeasurement(backend,settings,size[0]*size[1]+size[2]*size[3],2m*size[4]*size[5],timestamp);
            } catch(ArgumentException) { return null; } catch(InvalidOperationException) { return null; }
              catch(FormatException) { return null; } catch(OverflowException) { return null; }
              catch(InvalidCastException) { return null; }
        }
        static string ReadPrefix(string path,int limit,bool requireComplete) {
            using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
            using(var reader=new StreamReader(stream)) {
                var buffer=new char[limit];int count=reader.ReadBlock(buffer,0,buffer.Length);
                if(requireComplete&&!reader.EndOfStream)return null;
                return new string(buffer,0,count);
            }
        }
        public static PixelMeasurement Latest(string root) {
            try {
                string directory=Path.Combine(root,"logs");if(!Directory.Exists(directory))return null;
                var files=new DirectoryInfo(directory).GetFiles("quad-*.log");
                Array.Sort(files,delegate(FileInfo a,FileInfo b){return b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc);});
                if(files.Length==0)return null;
                // Use only a paired run. An incomplete new run invalidates old
                // measurements rather than silently borrowing another headset.
                string metadata=Path.ChangeExtension(files[0].FullName,null)+"-run.json";
                if(!File.Exists(metadata))return null;
                string json=ReadPrefix(metadata,64*1024,true);if(json==null)return null;
                return Parse(ReadPrefix(files[0].FullName,256*1024,false),json,files[0].LastWriteTimeUtc);
            } catch(IOException) { return null; } catch(UnauthorizedAccessException) { return null; }
        }
    }
}
