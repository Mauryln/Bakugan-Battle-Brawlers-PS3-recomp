using System;
using System.IO;
using System.Linq;
using System.Text;
using BakuganInstaller;

internal static class InstallerUpgradeTest
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void Main(string[] args)
    {
        string fixture = Path.GetFullPath(Path.Combine(args[0], Guid.NewGuid().ToString("N")));
        Directory.CreateDirectory(fixture);
        Console.WriteLine("Fixture: " + fixture);

        string source = Path.Combine(fixture, "disc");
        string destination = Path.Combine(fixture, "repair");
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(destination);
        string disc = Path.Combine(source, "PS3_DISC.SFB");
        string repairedDisc = Path.Combine(destination, "PS3_DISC.SFB");
        File.WriteAllText(disc, "disc descriptor from ISO");
        File.SetAttributes(disc, FileAttributes.ReadOnly);
        File.WriteAllText(repairedDisc, "read-only from version 1.2.0");
        File.SetAttributes(repairedDisc, FileAttributes.ReadOnly);
        bool oldCopyFailed = false;
        try { File.Copy(disc, repairedDisc, true); }
        catch (UnauthorizedAccessException) { oldCopyFailed = true; }
        Check(oldCopyFailed, "The old ISO copy must reproduce access denied.");
        Installer.CopyTree(source, destination, 0, 100);
        Check(File.ReadAllText(repairedDisc) == File.ReadAllText(disc), "Repair must overwrite the descriptor.");
        Check((File.GetAttributes(repairedDisc) & FileAttributes.ReadOnly) == 0, "Installed descriptor must be writable.");

        string installation = Path.Combine(fixture, "installed-v120");
        string game = Path.Combine(installation, "game", "PS3_GAME");
        Directory.CreateDirectory(Path.Combine(game, "USRDIR"));
        string launcher = Path.Combine(installation, "BakuganLauncher.exe");
        File.Copy(args[1], launcher);
        File.WriteAllText(Path.Combine(game, "PARAM.SFO"), "BLES00758");
        string boot = Path.Combine(game, "USRDIR", "EBOOT.elf");
        var elf = new byte[1024 * 1024];
        Buffer.BlockCopy(new byte[] { 0x7f, 0x45, 0x4c, 0x46 }, 0, elf, 0, 4);
        File.WriteAllBytes(boot, elf);
        string installedDisc = Path.Combine(installation, "game", "PS3_DISC.SFB");
        File.Copy(disc, installedDisc);
        string savedata = Path.Combine(installation, "gamedata", "save.dat");
        Directory.CreateDirectory(Path.GetDirectoryName(savedata));
        File.WriteAllText(savedata, "legacy save");
        string settings = Path.Combine(installation, "player.ini");
        File.WriteAllText(settings, "[Graphics]\nFPS=30\nframe_gen=fsr3\n");

        string payload = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "payload");
        string payloadLauncher = Path.Combine(payload, "BakuganLauncher.exe");
        Check(!File.ReadAllBytes(launcher).SequenceEqual(File.ReadAllBytes(payloadLauncher)),
              "Fixture must start with an older launcher.");
        var log = new StringBuilder();
        Installer.InstallFiles(new Options { Dir = installation }, log, null);
        Check(log.ToString().Contains("existing game data reused"), "Upgrade must reuse the prior game files.");
        Check(File.ReadAllBytes(launcher).SequenceEqual(File.ReadAllBytes(payloadLauncher)), "Launcher was not updated.");
        Check(File.Exists(Path.Combine(installation, "assets", "launcher", "Control.png")), "Controller image was not installed.");
        Check(File.ReadAllBytes(boot).SequenceEqual(elf), "Existing EBOOT.elf changed.");
        Check(File.ReadAllText(installedDisc) == File.ReadAllText(disc) &&
              (File.GetAttributes(installedDisc) & FileAttributes.ReadOnly) != 0, "Existing disc descriptor changed.");
        Check(File.ReadAllText(savedata) == "legacy save", "Existing save changed.");
        Check(File.ReadAllText(settings) == "[Graphics]\nFPS=30\nframe_gen=fsr3\n", "Existing settings changed.");

        string fresh = Path.Combine(fixture, "fresh-install");
        bool rejected = false;
        try { Installer.InstallFiles(new Options { Dir = fresh }, log, null); }
        catch (Exception) { rejected = true; }
        Check(rejected && !Directory.Exists(fresh), "Fresh installation without ISO must fail before creating files.");
        Console.WriteLine("PASS: read-only ISO repair, 1.2.0 upgrade without ISO, save/settings preservation, and ISO requirement for new installs.");
    }
}
