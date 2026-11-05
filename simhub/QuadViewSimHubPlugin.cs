using System;
using GameReaderCommon;
using SimHub.Plugins;

namespace LmuQuadView.SimHub
{
    // SimHub adapter. Properties appear as LmuQuadView.<Name>; actions can be bound
    // to wheel/button box inputs in SimHub's Controls and events.
    [PluginDescription("LMU Quad View: frame timing, resolution and FOV from the LMU Quad View mod (requires Benchmark enabled in the launcher).")]
    [PluginAuthor("Markus Stahl / LMU Quad View community")]
    [PluginName("LMU Quad View")]
    public class LmuQuadView : IPlugin, IDataPlugin
    {
        readonly QuadViewTelemetry telemetry = new QuadViewTelemetry();
        public PluginManager PluginManager { get; set; }

        public void Init(PluginManager pluginManager)
        {
            Func<TelemetrySnapshot> t = () => telemetry.Current;
            // AttachDelegate is evaluated on demand: no cost for unused properties.
            this.AttachDelegate("Connected", () => t().Connected);
            this.AttachDelegate("Stale", () => t().Stale);
            this.AttachDelegate("Mode", () => t().Mode);
            this.AttachDelegate("SceneActive", () => t().SceneActive);
            this.AttachDelegate("Fps", () => Round(t().Fps, 1));
            this.AttachDelegate("FrameMs", () => Round(t().FrameMs, 2));
            this.AttachDelegate("FrameMsAvg", () => Round(t().FrameMsAvg, 2));
            this.AttachDelegate("FrameMsMax", () => Round(t().FrameMsMax, 2));
            this.AttachDelegate("AppMs", () => Round(t().AppMs, 2));
            this.AttachDelegate("ProviderMs", () => Round(t().ProviderMs, 2));
            this.AttachDelegate("GpuMs", () => Round(t().GpuMs, 2));
            this.AttachDelegate("ProjectionCount", () => t().ProjectionCount);
            this.AttachDelegate("NativeStatus", () => t().NativeStatus);
            this.AttachDelegate("LastXrResult", () => t().LastXrResult);
            this.AttachDelegate("OverlayVisible", () => t().OverlayVisible);
            this.AttachDelegate("Recording", () => t().Recording);
            this.AttachDelegate("DroppedSamples", () => t().DroppedSamples);
            this.AttachDelegate("OuterLeftResolution", () => Resolution(t().OuterLeftWidth, t().OuterLeftHeight));
            this.AttachDelegate("OuterRightResolution", () => Resolution(t().OuterRightWidth, t().OuterRightHeight));
            this.AttachDelegate("FocusResolution", () => Resolution(t().FocusWidth, t().FocusHeight));
            this.AttachDelegate("FovValid", () => t().FovValid);
            this.AttachDelegate("FovLeftHorizontal", () => Round(t().FovLeftHorizontal, 1));
            this.AttachDelegate("FovLeftVertical", () => Round(t().FovLeftVertical, 1));
            this.AttachDelegate("FovRightHorizontal", () => Round(t().FovRightHorizontal, 1));
            this.AttachDelegate("FovRightVertical", () => Round(t().FovRightVertical, 1));
            this.AddAction("ToggleOverlay", (manager, action) => QuadViewCommands.Signal(QuadViewCommands.ToggleOverlay));
            this.AddAction("ToggleRecording", (manager, action) => QuadViewCommands.Signal(QuadViewCommands.ToggleRecording));
        }

        public void DataUpdate(PluginManager pluginManager, ref GameData data) { telemetry.Poll(); }
        public void End(PluginManager pluginManager) { telemetry.Dispose(); }

        static object Round(double value, int digits) { return Double.IsNaN(value) || Double.IsInfinity(value) ? (object)null : Math.Round(value, digits); }
        static string Resolution(int width, int height) { return width > 0 && height > 0 ? width + "x" + height : ""; }
    }
}
