using System;
using System.Globalization;
using System.IO;
using LmuQuadViewLauncher;

static class PixelEstimateTests {
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static void Near(decimal actual,decimal expected,string message) { Check(Math.Abs(actual-expected)<0.000001m,message); }
    static int Main() {
        string root=Path.Combine(Path.GetTempPath(),"lmu-pixels-"+Guid.NewGuid().ToString("N"));
        try {
            var high=new PixelSettings(25,300,50,50,true);
            var low=new PixelSettings(25,200,50,50,true);
            var a=PixelEstimate.Calculate(high,null);var b=PixelEstimate.Calculate(low,null);
            Near(a.Focus,2.25m,"300% focus area");Near(b.Focus,1m,"200% focus area");
            Near(100*(1-b.Focus/a.Focus),55.555555555555m,"Focus saving");
            Near(a.Total,2.3125m,"Outer area must also count");
            Near(PixelEstimate.Calculate(new PixelSettings(25,200,50,50,false),null).Total,2m,"Full-size outer rendering");
            Near(PixelEstimate.Calculate(new PixelSettings(25,200,25,50,true),null).Focus,.5m,"Focus width scales area");
            Check(!new PixelSettings(50,25,10,10,true).Valid,"Outer does not fit focus");
            Check(!new PixelSettings(0,200,50,50,true).Valid,"Invalid scale");
            string log="Actual swapchains outer0=1000x1000 outer1=1200x900 focus=4000x3000";
            string json="{\"Status\":\"started\",\"Mode\":\"quad\",\"Backend\":\"QVFR\",\"UseRuntimeSettings\":true,\"NativeOuterResolution\":true,\"OuterPercent\":25,\"InnerPercent\":300,\"FocusWidthPercent\":50,\"FocusHeightPercent\":50}";
            var sample=PixelMeasurement.Parse(log,json,DateTime.UtcNow);
            Check(sample!=null,"Valid sample");
            a=PixelEstimate.Calculate(high,sample);b=PixelEstimate.Calculate(low,sample);
            Near(a.Outer,2080000m,"Both asymmetric outer eyes");Near(a.Focus,24000000m,"Two common focus buffers");
            Near(b.Focus,24000000m*4/9,"Measured focus scaling");Near(b.Outer,a.Outer,"Unchanged measured outer");
            Near(PixelEstimate.Calculate(new PixelSettings(25,200,50,50,false),sample).Total,b.Focus*2,"Measured full-size outer");
            Check(PixelMeasurement.Parse(log,json.Replace("started","prepared"),DateTime.UtcNow)==null,"Prepared launch is not a run");
            Check(PixelMeasurement.Parse(log.Replace("4000x3000","0x3000"),json,DateTime.UtcNow)==null,"Zero dimension");
            Check(PixelMeasurement.Parse(log.Replace("4000x3000","999999999999x3000"),json,DateTime.UtcNow)==null,"Overflow dimension");
            Check(PixelMeasurement.Parse(log,"{bad",DateTime.UtcNow)==null,"Bad JSON");
            Check(PixelMeasurement.Parse(log,json.Replace("\"InnerPercent\":300","\"InnerPercent\":[300]"),DateTime.UtcNow)==null,"Malformed numeric object");
            Check(PixelMeasurement.Parse(log,json.Replace("\"InnerPercent\":300,",""),DateTime.UtcNow)==null,"Missing calibration settings");
            Check(PixelMeasurement.Parse(log,json.Replace("\"InnerPercent\":300","\"InnerPercent\":\"300\""),DateTime.UtcNow)==null,"Numeric strings are not accepted as metadata");
            Check(PixelMeasurement.Parse(log,json.Replace("QVFR","Unknown"),DateTime.UtcNow)==null,"Unknown provider");
            var native=PixelMeasurement.Parse(log,json.Replace("QVFR","Native"),DateTime.UtcNow);
            Check(native!=null&&native.Backend=="Native","Native recorded sizes");
            bool refused=false;try{PixelEstimate.Calculate(high,native);}catch(ArgumentException){refused=true;}
            Check(refused,"Native data must not calibrate QVFR sliders");
            var large=PixelMeasurement.Parse("Actual swapchains outer0=65536x65536 outer1=65536x65536 focus=65536x65536",json,DateTime.UtcNow);
            Near(PixelEstimate.Calculate(high,large).Total,17179869184m,"No 32-bit pixel overflow");
            CultureInfo.CurrentCulture=new CultureInfo("de-DE");
            Check(PixelMeasurement.Parse(log,json.Replace("\"OuterPercent\":25","\"OuterPercent\":25.5"),DateTime.UtcNow)!=null,"Invariant JSON values");
            Directory.CreateDirectory(Path.Combine(root,"logs"));
            var old=Path.Combine(root,"logs","quad-old.log");File.WriteAllText(old,log);File.WriteAllText(Path.ChangeExtension(old,null)+"-run.json",json);
            Check(PixelMeasurement.Latest(root)!=null,"Latest run read");
            var latest=Path.Combine(root,"logs","quad-new.log");File.WriteAllText(latest,"incomplete");File.SetLastWriteTimeUtc(latest,DateTime.UtcNow.AddMinutes(1));
            Check(PixelMeasurement.Latest(root)==null,"Never fall back to older run");
            Console.WriteLine("Pixel estimate tests passed: area ratios, measured counts, outer mode, malformed/latest data, overflow, culture.");return 0;
        } catch(Exception error) {Console.Error.WriteLine(error);return 1;}
        finally { if(Directory.Exists(root))Directory.Delete(root,true); }
    }
}
