using System;
using LmuQuadViewLauncher;
class LaunchCommandTests {
    static void Check(bool yes, string error) { if (!yes) throw new Exception(error); }
    static int Main() {
        var command=LaunchCommand.Invocation("C:\\a'b\\launch.ps1", "C:\\game's folder", true, "QVFR", "fixed", "C:\\provider's\\quad.json");
        Check(command.Contains("'C:\\a''b\\launch.ps1'"), "Script quoting");
        Check(command.Contains("-Backend QVFR -FoveationMode fixed"), "Backend/mode missing");
        Check(command.Contains("-QuadViewsManifest 'C:\\provider''s\\quad.json'"), "Manifest quoting");
        Check(command.Contains("-DebugFocus"), "Outline lost");
        Check(LaunchCommand.Invocation("launch.ps1", "game", false).Contains("-Backend Native -FoveationMode auto"), "Old defaults changed");
        bool rejected=false; try { LaunchCommand.Invocation("x", "g", false, "QVFR; exit", "auto", ""); } catch (ArgumentException) { rejected=true; }
        Check(rejected, "Backend injection accepted");
        Console.WriteLine("Launcher command tests passed: portable/fixed, backward defaults, path quoting, validation");
        return 0;
    }
}
