using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace LmuQuadView.SimHub
{
    // Read-only consumer of the mod's benchmark ABI v1 (mod/benchmark_native.hpp).
    // Independent of SimHub so it can be tested without SimHub's assemblies.
    // The mapping exists only while LMU runs with Diagnostics + Benchmark enabled.
    public sealed class TelemetrySnapshot
    {
        public static readonly TelemetrySnapshot Disconnected = new TelemetrySnapshot();
        public bool Connected { get; internal set; }
        public bool Stale { get; internal set; }
        public int ProcessId { get; internal set; }
        public string Mode { get; internal set; }
        public bool SceneActive { get; internal set; }
        public double Fps { get; internal set; }
        public double FrameMs { get; internal set; }
        public double FrameMsAvg { get; internal set; }
        public double FrameMsMax { get; internal set; }
        public double AppMs { get; internal set; }
        public double ProviderMs { get; internal set; }
        public double GpuMs { get; internal set; }
        public int ProjectionCount { get; internal set; }
        public int NativeStatus { get; internal set; }
        public int LastXrResult { get; internal set; }
        public int DiagnosticReason { get; internal set; }
        public bool OverlayVisible { get; internal set; }
        public bool Recording { get; internal set; }
        public long DroppedSamples { get; internal set; }
        public long Frames { get; internal set; }
        public int OuterLeftWidth { get; internal set; }
        public int OuterLeftHeight { get; internal set; }
        public int OuterRightWidth { get; internal set; }
        public int OuterRightHeight { get; internal set; }
        public int FocusWidth { get; internal set; }
        public int FocusHeight { get; internal set; }
        public bool FovValid { get; internal set; }
        public double FovLeftHorizontal { get; internal set; }
        public double FovLeftVertical { get; internal set; }
        public double FovRightHorizontal { get; internal set; }
        public double FovRightVertical { get; internal set; }
        public TelemetrySnapshot()
        {
            Mode = ""; Fps = FrameMs = FrameMsAvg = FrameMsMax = AppMs = ProviderMs = GpuMs = Double.NaN;
            FovLeftHorizontal = FovLeftVertical = FovRightHorizontal = FovRightVertical = Double.NaN;
        }
    }

    public sealed class QuadViewTelemetry : IDisposable
    {
        internal const uint Magic = 0x51424D4C;
        internal const int Version = 1, Size = 1900800, HeaderSize = 256, Slots = 4096, SlotSize = 80;
        const string MappingPrefix = "Local\\LMUQuadView.Benchmark.";
        static readonly TimeSpan DiscoverInterval = TimeSpan.FromSeconds(2), StaleAfter = TimeSpan.FromSeconds(3), Window = TimeSpan.FromSeconds(1);

        MemoryMappedFile mapping;
        MemoryMappedViewAccessor view;
        int pid;
        long nextSample, dropped, lastHeartbeat, frequency, latestQpc, gpuQpc;
        DateTime nextDiscover = DateTime.MinValue, heartbeatSeen, nextProcessCheck;
        readonly double[] intervals = new double[1024];
        readonly long[] sampleQpc = new long[1024];
        int intervalCount, intervalHead;
        double lastFrame = Double.NaN, lastApp = Double.NaN, lastProvider = Double.NaN, lastGpu = Double.NaN;
        bool lastScene;
        volatile TelemetrySnapshot current = TelemetrySnapshot.Disconnected;

        public TelemetrySnapshot Current { get { return current; } }

        // Call regularly (SimHub DataUpdate). Cheap when connected; process discovery is throttled.
        public void Poll() { Poll(DateTime.UtcNow); }
        internal void Poll(DateTime now)
        {
            try
            {
                if (view == null)
                {
                    if (now < nextDiscover) return;
                    nextDiscover = now + DiscoverInterval;
                    if (!Discover(now)) { current = TelemetrySnapshot.Disconnected; return; }
                }
                Read(now);
            }
            catch (Exception error)
            {
                if (error is OutOfMemoryException) throw;
                Detach(); current = TelemetrySnapshot.Disconnected;
            }
        }

        bool Discover(DateTime now)
        {
            var games = Process.GetProcessesByName("Le Mans Ultimate");
            try
            {
                foreach (var game in games)
                {
                    MemoryMappedFile found = null;
                    try
                    {
                        found = MemoryMappedFile.OpenExisting(MappingPrefix + game.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), MemoryMappedFileRights.Read);
                        Attach(found, found.CreateViewAccessor(0, Size, MemoryMappedFileAccess.Read), game.Id, now);
                        return true;
                    }
                    catch (FileNotFoundException) { if (found != null) found.Dispose(); }
                    catch (IOException) { if (found != null) found.Dispose(); }
                    catch (UnauthorizedAccessException) { if (found != null) found.Dispose(); }
                    catch (InvalidDataException) { if (found != null) found.Dispose(); }
                }
                return false;
            }
            finally { foreach (var game in games) game.Dispose(); }
        }

        // Test seam: attach any accessor that carries the ABI layout.
        internal void Attach(MemoryMappedFile owner, MemoryMappedViewAccessor accessor, int processId, DateTime now)
        {
            if (accessor.Capacity < Size || accessor.ReadUInt32(0) != Magic || accessor.ReadInt32(4) != Version || accessor.ReadInt32(8) != Size ||
                accessor.ReadInt32(12) != processId || accessor.ReadInt64(16) <= 0)
            { accessor.Dispose(); throw new InvalidDataException("Incompatible LMU Quad View telemetry"); }
            Detach();
            mapping = owner; view = accessor; pid = processId; frequency = view.ReadInt64(16);
            nextSample = Math.Max(1, view.ReadInt64(24)); dropped = 0;
            lastHeartbeat = view.ReadInt64(32); heartbeatSeen = now;
            nextProcessCheck = now;
            intervalCount = intervalHead = 0; lastScene = false;
            latestQpc = gpuQpc = 0;
            lastFrame = lastApp = lastProvider = lastGpu = Double.NaN;
        }

        void Read(DateTime now)
        {
            long published = view.ReadInt64(24);
            if (published < nextSample - 1) { Detach(); current = TelemetrySnapshot.Disconnected; return; } // stream restarted
            long oldest = Math.Max(1, published - Slots + 1);
            if (nextSample < oldest) { dropped += oldest - nextSample; nextSample = oldest; }
            for (int read = 0; nextSample <= published && read < Slots; read++)
            {
                if (!TryRead(nextSample)) break; // in-flight slot: retry next poll, never accept a torn sample
                nextSample++;
            }
            long heartbeat = view.ReadInt64(32);
            if (heartbeat != lastHeartbeat) { lastHeartbeat = heartbeat; heartbeatSeen = now; }
            bool stale = now - heartbeatSeen > StaleAfter;
            if (stale && now >= nextProcessCheck)
            {
                nextProcessCheck = now + DiscoverInterval;
                // A reader keeps the mapping alive after its game exits. Release it
                // so the next poll can discover a new game PID instead of staying stale forever.
                if (!ProcessRunning(pid)) { Detach(); current = TelemetrySnapshot.Disconnected; return; }
            }
            current = Build(stale);
        }

        static bool ProcessRunning(int processId)
        {
            try { using (var process = Process.GetProcessById(processId)) return !process.HasExited; }
            catch (ArgumentException) { return false; }
            // Access denied is not proof of exit (protected game processes).
            catch (System.ComponentModel.Win32Exception) { return true; }
        }

        bool TryRead(long number)
        {
            long offset = HeaderSize + ((number - 1) % Slots) * SlotSize;
            if (view.ReadInt64(offset) != number) return false;
            Thread.MemoryBarrier();
            long qpc = view.ReadInt64(offset + 8);
            double interval = view.ReadDouble(offset + 24), app = view.ReadDouble(offset + 32), provider = view.ReadDouble(offset + 40), gpu = view.ReadDouble(offset + 48);
            uint flags = view.ReadUInt32(offset + 64);
            Thread.MemoryBarrier();
            if (view.ReadInt64(offset) != number) return false;
            latestQpc = qpc;
            lastScene = (flags & 1) != 0;
            if (!lastScene) { lastFrame = lastApp = lastProvider = lastGpu = Double.NaN; return true; }
            lastFrame = Finite(interval); lastApp = Finite(app); lastProvider = Finite(provider);
            if ((flags & 8) != 0) { lastGpu = Finite(gpu); gpuQpc = qpc; }
            if (IsValue(lastFrame) && interval > 0)
            {
                intervals[intervalHead] = interval; sampleQpc[intervalHead] = qpc;
                intervalHead = (intervalHead + 1) % intervals.Length; if (intervalCount < intervals.Length) intervalCount++;
            }
            return true;
        }

        TelemetrySnapshot Build(bool stale)
        {
            var s = new TelemetrySnapshot { Connected = true, Stale = stale, ProcessId = pid, DroppedSamples = dropped, Frames = Math.Max(0, nextSample - 1) };
            int status = view.ReadInt32(84), running = view.ReadInt32(88);
            s.NativeStatus = status; s.LastXrResult = view.ReadInt32(92); s.ProjectionCount = view.ReadInt32(96); s.DiagnosticReason = view.ReadInt32(132);
            s.OverlayVisible = view.ReadInt32(44) != 0; s.Recording = view.ReadInt32(52) != 0;
            s.OuterLeftWidth = Dim(view.ReadUInt32(108)); s.OuterLeftHeight = Dim(view.ReadUInt32(112));
            s.OuterRightWidth = Dim(view.ReadUInt32(116)); s.OuterRightHeight = Dim(view.ReadUInt32(120));
            s.FocusWidth = Dim(view.ReadUInt32(124)); s.FocusHeight = Dim(view.ReadUInt32(128));
            s.Mode = s.FocusWidth > 0 && s.FocusHeight > 0 ? "Quad views" : s.OuterLeftWidth > 0 ? "Stereo VRS" : "";
            s.SceneActive = !stale && running != 0 && lastScene && s.ProjectionCount > 0;
            if (s.SceneActive)
            {
                s.FrameMs = lastFrame; s.AppMs = lastApp; s.ProviderMs = lastProvider;
                if (latestQpc >= gpuQpc && (latestQpc - gpuQpc) / (double)frequency <= 2) s.GpuMs = lastGpu;
                // Rolling one-second window by sample timestamps (QPC), independent of poll rate.
                long newest = latestQpc;
                long span = (long)(frequency * Window.TotalSeconds);
                double sum = 0, max = 0; int n = 0;
                for (int i = 0; i < intervalCount; i++)
                {
                    int index = (intervalHead - 1 - i + intervals.Length) % intervals.Length;
                    if (newest - sampleQpc[index] > span) break;
                    sum += intervals[index]; if (intervals[index] > max) max = intervals[index]; n++;
                }
                if (n > 0) { s.FrameMsAvg = sum / n; s.FrameMsMax = max; s.Fps = 1000.0 / s.FrameMsAvg; }
            }
            ReadFov(s);
            return s;
        }

        void ReadFov(TelemetrySnapshot s)
        {
            int sequence = view.ReadInt32(136); if (sequence == 0 || (sequence & 1) != 0) return;
            Thread.MemoryBarrier();
            uint flags = view.ReadUInt32(140);
            float lh = view.ReadSingle(148), lv = view.ReadSingle(152), rh = view.ReadSingle(156), rv = view.ReadSingle(160);
            ulong frame = view.ReadUInt64(168);
            Thread.MemoryBarrier(); if (view.ReadInt32(136) != sequence || (flags & 1) == 0 || frame == 0 || !s.SceneActive) return;
            foreach (float extent in new[] { lh, lv, rh, rv }) if (!IsValue(extent) || extent <= 0 || extent >= 180) return;
            s.FovValid = true; s.FovLeftHorizontal = lh; s.FovLeftVertical = lv; s.FovRightHorizontal = rh; s.FovRightVertical = rv;
        }

        static int Dim(uint value) { return value > 65536 ? 0 : (int)value; }
        static bool IsValue(double value) { return !Double.IsNaN(value) && !Double.IsInfinity(value); }
        static double Finite(double value) { return IsValue(value) && value >= 0 && value <= 10000 ? value : Double.NaN; }

        void Detach()
        {
            if (view != null) { view.Dispose(); view = null; }
            if (mapping != null) { mapping.Dispose(); mapping = null; }
            nextDiscover = DateTime.MinValue;
        }
        public void Dispose() { Detach(); current = TelemetrySnapshot.Disconnected; }
    }

    // Commands are plain signals to the launcher, which owns the overlay/recording
    // state. The plugin never writes into the game's shared memory.
    public static class QuadViewCommands
    {
        public const string ToggleOverlay = "Local\\LMUQuadView.Command.ToggleOverlay";
        public const string ToggleRecording = "Local\\LMUQuadView.Command.ToggleRecording";
        public static bool Signal(string name)
        {
            try
            {
                EventWaitHandle handle;
                if (!EventWaitHandle.TryOpenExisting(name, System.Security.AccessControl.EventWaitHandleRights.Modify, out handle)) return false;
                using (handle) return handle.Set();
            }
            catch (UnauthorizedAccessException) { return false; }
            catch (IOException) { return false; }
            catch (WaitHandleCannotBeOpenedException) { return false; }
        }
    }
}
