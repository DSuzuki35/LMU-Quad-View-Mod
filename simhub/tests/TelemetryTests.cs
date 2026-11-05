using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using LmuQuadView.SimHub;

// Reads an ABI image written by the mod's own C++ writer (telemetry_fixture.cpp).
static class TelemetryTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static bool Near(double a, double b) { return Math.Abs(a - b) < 1e-6; }
    static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--exit-fixture") return 0;
        string source = args[0], work = Path.Combine(Path.GetTempPath(), "lmuqv-telemetry-" + Guid.NewGuid().ToString("N") + ".bin");
        File.Copy(source, work);
        try
        {
            var now = new DateTime(2026, 11, 5, 12, 0, 0, DateTimeKind.Utc);
            using (var file = MemoryMappedFile.CreateFromFile(work, FileMode.Open, null, 0, MemoryMappedFileAccess.ReadWrite))
            using (var writer = file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.ReadWrite))
            {
                Require(writer.ReadInt64(24) == 200, "fixture should contain 200 samples");
                var reject = new QuadViewTelemetry();
                bool rejected = false;
                try { reject.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), 999, now); } catch (InvalidDataException) { rejected = true; }
                Require(rejected, "mapping of another process accepted");
                int livePid = Process.GetCurrentProcess().Id;
                writer.Write(12, livePid);

                var telemetry = new QuadViewTelemetry();
                writer.Write(24, 100L);
                telemetry.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), livePid, now);
                writer.Write(24, 200L);
                telemetry.Poll(now);
                var s = telemetry.Current;
                Require(s.Connected && !s.Stale && s.SceneActive, "not connected/active");
                Require(s.Mode == "Quad views" && s.ProjectionCount == 4 && s.OverlayVisible && !s.Recording, "header fields");
                Require(s.OuterLeftWidth == 1200 && s.OuterLeftHeight == 1100 && s.OuterRightWidth == 1200 && s.FocusWidth == 1600 && s.FocusHeight == 1500, "resolutions");
                Require(Near(s.FrameMs, 11.1111) && Near(s.AppMs, 3.25) && Near(s.ProviderMs, 1.5) && Near(s.GpuMs, 7.75), "last sample values");
                double expectedAvg = (89 * 11.1111 + 25.0) / 90; // samples 110..200 within 1 s, minus non-scene sample 120
                Require(Near(s.FrameMsAvg, expectedAvg) && Near(s.FrameMsMax, 25.0) && Near(s.Fps, 1000 / expectedAvg), "rolling window " + s.FrameMsAvg + " " + s.FrameMsMax);
                Require(s.FovValid && Near(s.FovLeftHorizontal, 48.5) && Near(s.FovRightVertical, 40.25), "FOV");
                Require(s.Frames == 200 && s.DroppedSamples == 0, "frame counters");

                // A slot that is being written (negative commit) is never accepted.
                long slot201 = 256 + ((201 - 1) % 4096) * 80;
                writer.Write(slot201, -201L); writer.Write(24, 201L);
                telemetry.Poll(now.AddMilliseconds(50));
                Require(telemetry.Current.Frames == 200, "torn slot accepted");

                // No heartbeat for longer than 3 s: stale, no scene values.
                telemetry.Poll(now.AddSeconds(5));
                s = telemetry.Current;
                Require(s.Connected && s.Stale && !s.SceneActive && Double.IsNaN(s.Fps) && !s.FovValid, "stale detection");
                writer.Write(32, writer.ReadInt64(32) + 1);
                telemetry.Poll(now.AddSeconds(6));
                Require(!telemetry.Current.Stale, "heartbeat recovery");

                // Stream restart (new session): disconnect and rediscover.
                writer.Write(24, 5L);
                telemetry.Poll(now.AddSeconds(7));
                Require(!telemetry.Current.Connected, "restart not detected");
                telemetry.Dispose();

                // GPU samples share the launcher's millisecond plausibility rules.
                writer.Write(24, 200L);
                long last = 256 + 199 * 80;
                foreach (double invalid in new[] { -1.0, 10001.0, Double.PositiveInfinity, Double.NaN })
                {
                    using (var reader = new QuadViewTelemetry())
                    {
                        writer.Write(last + 48, invalid);
                        reader.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), livePid, now);
                        reader.Poll(now);
                        Require(Double.IsNaN(reader.Current.GpuMs), "invalid GPU timing accepted: " + invalid);
                    }
                }
                writer.Write(last + 48, 7.75);
                using (var reader = new QuadViewTelemetry())
                {
                    reader.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), livePid, now);
                    reader.Poll(now);
                    writer.Write(168, 0UL);
                    reader.Poll(now);
                    Require(!reader.Current.FovValid, "unpublished FOV frame accepted");
                    writer.Write(168, 180UL);
                    // An unavailable new timing must age out old rolling/GPU data.
                    WriteSample(writer, 201, 50000000L, Double.NaN, 1u);
                    reader.Poll(now.AddSeconds(1));
                    Require(Double.IsNaN(reader.Current.Fps) && Double.IsNaN(reader.Current.GpuMs), "expired timings retained");
                    WriteSample(writer, 202, 50111111L, 11.1111, 9u);
                    reader.Poll(now.AddSeconds(1.1));
                    Require(Near(reader.Current.GpuMs, 7.75), "GPU timing did not recover");
                    WriteSample(writer, 203, 50222222L, 11.1111, 0u);
                    reader.Poll(now.AddSeconds(1.2));
                    WriteSample(writer, 204, 50333333L, 11.1111, 1u);
                    reader.Poll(now.AddSeconds(1.3));
                    Require(Double.IsNaN(reader.Current.GpuMs), "previous scene GPU timing retained");
                }
                using (var child = Process.Start(new ProcessStartInfo(System.Reflection.Assembly.GetExecutingAssembly().Location, "--exit-fixture") { UseShellExecute = false, CreateNoWindow = true }))
                using (var reader = new QuadViewTelemetry())
                {
                    Require(child.WaitForExit(5000), "exit fixture did not finish");
                    writer.Write(12, child.Id);
                    reader.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), child.Id, now);
                    reader.Poll(now);
                    reader.Poll(now.AddSeconds(5));
                    Require(!reader.Current.Connected, "exited process mapping retained instead of reopening discovery");
                    writer.Write(12, livePid);
                    reader.Attach(null, file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read), livePid, now.AddSeconds(6));
                    reader.Poll(now.AddSeconds(6));
                    Require(reader.Current.Connected && reader.Current.ProcessId == livePid && !reader.Current.Stale, "new process did not reconnect");
                }
            }
            Console.WriteLine("SimHub telemetry tests passed: native ABI, millisecond/QPC units, rolling window, torn slots, invalid/expired timings, FOV validity, staleness, restart, foreign PID, exited PID/reconnection.");
            return 0;
        }
        finally { try { File.Delete(work); } catch (IOException) { } }
    }

    static void WriteSample(MemoryMappedViewAccessor writer, long number, long qpc, double interval, uint flags)
    {
        long slot = 256 + ((number - 1) % 4096) * 80;
        writer.Write(slot, -number); writer.Write(slot + 8, qpc);
        writer.Write(slot + 24, interval); writer.Write(slot + 32, 3.25); writer.Write(slot + 40, 1.5);
        writer.Write(slot + 48, 7.75); writer.Write(slot + 64, flags);
        writer.Write(slot, number); writer.Write(32, qpc); writer.Write(24, number);
    }
}
