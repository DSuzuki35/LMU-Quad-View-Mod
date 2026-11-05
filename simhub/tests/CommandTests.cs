using System;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using LmuQuadView.SimHub;
using LmuQuadViewLauncher;

class CommandTests {
    static void Require(bool good,string message) { if(!good)throw new Exception(message); }
    static void Main() {
        Require(QuadViewCommands.ToggleOverlay==ExternalCommands.ToggleOverlayName&&QuadViewCommands.ToggleRecording==ExternalCommands.ToggleRecordingName,"launcher/plugin command names disagree");
        string prefix="Local\\LMUQuadView.PluginTests."+Guid.NewGuid().ToString("N");
        int overlays=0,recordings=0;
        Require(!QuadViewCommands.Signal(prefix),"absent launcher signal accepted");
        using(var commands=new ExternalCommands(prefix,prefix+".record")) {
            Require(QuadViewCommands.Signal(prefix)&&QuadViewCommands.Signal(prefix+".record"),"plugin signal failed");
            commands.Poll(delegate{overlays++;},delegate{recordings++;});
            Require(overlays==1&&recordings==1,"plugin action did not reach launcher");
        }
        using(var denied=new EventWaitHandle(false,EventResetMode.AutoReset,prefix)) {
            var security=denied.GetAccessControl();
            security.AddAccessRule(new EventWaitHandleAccessRule(WindowsIdentity.GetCurrent().User,EventWaitHandleRights.Modify,AccessControlType.Deny));
            denied.SetAccessControl(security);
            Require(!QuadViewCommands.Signal(prefix),"denied command reported success");
        }
        using(var wrongType=new Mutex(false,prefix))
            Require(!QuadViewCommands.Signal(prefix),"wrong object type accepted");
        Console.WriteLine("SimHub command tests passed: launcher round trip, absent channel, denied channel, object collision.");
    }
}
