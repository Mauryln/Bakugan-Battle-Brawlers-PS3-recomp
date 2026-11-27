using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using BakuganInstaller;

internal static class InstallerUpgradeTest
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void InvokeUninstallerRemove(MethodInfo removeMethod, string dir, string selfPath)
    {
        try
        {
            removeMethod.Invoke(null, new object[] { dir, selfPath });
        }
        catch (TargetInvocationException ex)
        {
            if (ex.InnerException != null) throw ex.InnerException;
            throw;
        }
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

        // Folder install from dump root (PS3_GAME\ + PS3_DISC.SFB)
        string dumpRoot = Path.Combine(fixture, "dump-root");
        string dumpPs3Game = Path.Combine(dumpRoot, "PS3_GAME");
        Directory.CreateDirectory(Path.Combine(dumpPs3Game, "USRDIR"));
        File.WriteAllText(Path.Combine(dumpRoot, "PS3_DISC.SFB"), "disc descriptor from folder");
        File.WriteAllText(Path.Combine(dumpPs3Game, "PARAM.SFO"), "BLES00758");
        File.WriteAllBytes(Path.Combine(dumpPs3Game, "USRDIR", "EBOOT.elf"), elf);

        string folderInstall = Path.Combine(fixture, "folder-install");
        log.Clear();
        Installer.InstallFiles(new Options { Iso = dumpRoot, Dir = folderInstall }, log, null);
        Check(log.ToString().Contains("game source: folder"), "Folder install must log folder source.");
        Check(!Directory.Exists(Path.Combine(folderInstall, "rpcs3")),
              "Installed game folder must never contain rpcs3.");
        Check(!Directory.Exists(Path.Combine(folderInstall, ".decrypt")),
              "Installed game folder must not retain temporary .decrypt directory.");
        Check(File.Exists(Path.Combine(folderInstall, "BakuganRecomp.exe")), "BakuganRecomp.exe missing.");
        Check(File.Exists(Path.Combine(folderInstall, "SDL3.dll")), "SDL3.dll missing.");
        Check(File.Exists(Path.Combine(folderInstall, "assets", "launcher", "Control.png")), "Launcher asset missing.");
        Check(File.Exists(Path.Combine(folderInstall, "assets", "button_prompts", "xbox", "a748b20fa470925a.png")),
              "Xbox button prompt atlas missing.");
        Check(File.Exists(Path.Combine(folderInstall, "assets", "button_prompts", "nintendo", "a748b20fa470925a.png")),
              "Nintendo button prompt atlas missing.");
        Check(File.Exists(Path.Combine(folderInstall, "shaders", "fsr1", "present.hlsl")), "FSR1 shader missing.");
        Check(File.ReadAllText(Path.Combine(folderInstall, "game", "PS3_DISC.SFB")) == "disc descriptor from folder",
              "PS3_DISC.SFB was not copied from dump root.");
        Check(File.ReadAllBytes(Path.Combine(folderInstall, "game", "PS3_GAME", "USRDIR", "EBOOT.elf")).SequenceEqual(elf),
              "EBOOT.elf was not copied from dump root.");

        // Direct PS3_GAME folder install + --folder CLI parsing + silent --eboot fallback
        string directInstall = Path.Combine(fixture, "direct-ps3game-install");
        Options cliOpts = Options.Parse(new[] { "--folder", dumpPs3Game, "--dir", directInstall });
        Check(cliOpts.Iso == dumpPs3Game && cliOpts.Dir == directInstall, "--folder CLI parsing failed.");
        Check(string.IsNullOrEmpty(cliOpts.Eboot), "Options.Parse must not auto-populate Eboot without --eboot.");
        log.Clear();
        Installer.InstallFiles(cliOpts, log, null);
        Check(!Directory.Exists(Path.Combine(directInstall, "rpcs3")),
              "Direct PS3_GAME install must not contain rpcs3.");
        Check(File.ReadAllText(Path.Combine(directInstall, "game", "PS3_DISC.SFB")) == "disc descriptor from folder",
              "Direct PS3_GAME install must copy sibling PS3_DISC.SFB.");
        Check(File.ReadAllBytes(Path.Combine(directInstall, "game", "PS3_GAME", "USRDIR", "EBOOT.elf")).SequenceEqual(elf),
              "Direct PS3_GAME install did not copy EBOOT.elf.");

        string fallbackSource = Path.Combine(fixture, "unreadable-bin-dump");
        Directory.CreateDirectory(Path.Combine(fallbackSource, "PS3_GAME", "USRDIR"));
        File.WriteAllText(Path.Combine(fallbackSource, "PS3_GAME", "PARAM.SFO"), "BLES00758");
        File.WriteAllBytes(Path.Combine(fallbackSource, "PS3_GAME", "USRDIR", "EBOOT.BIN"), new byte[128]);
        string fallbackElf = Path.Combine(fixture, "fallback-EBOOT.elf");
        File.WriteAllBytes(fallbackElf, elf);
        string fallbackInstall = Path.Combine(fixture, "fallback-install");
        log.Clear();
        Installer.InstallFiles(new Options { Iso = fallbackSource, Dir = fallbackInstall, Eboot = fallbackElf }, log, null);
        Check(log.ToString().Contains("--eboot fallback"), "Silent --eboot fallback was not used.");
        Check(File.ReadAllBytes(Path.Combine(fallbackInstall, "game", "PS3_GAME", "USRDIR", "EBOOT.elf")).SequenceEqual(elf),
              "Silent --eboot fallback did not copy valid EBOOT.elf.");

        // Overlap rejection: source == dest, dest inside source, source inside dest
        string[] overlapCases = new string[]
        {
            dumpRoot,
            Path.Combine(dumpRoot, "nested-install"),
            Path.GetDirectoryName(dumpRoot)
        };
        foreach (string badDest in overlapCases)
        {
            bool overlapRejected = false;
            try { Installer.InstallFiles(new Options { Iso = dumpRoot, Dir = badDest }, log, null); }
            catch (Exception) { overlapRejected = true; }
            Check(overlapRejected, "Overlapping source/dest must fail: " + badDest);
        }
        Check(!Directory.Exists(Path.Combine(dumpRoot, "nested-install")),
              "Rejected overlapping install must not create destination directory.");

        string payloadUninstaller = Path.Combine(payload, "Uninstall.exe");
        Check(File.Exists(payloadUninstaller), "Uninstall.exe missing from payload.");
        Assembly uninstallAsm = Assembly.LoadFrom(payloadUninstaller);
        Type uninstallProgram = uninstallAsm.GetType("BakuganUninstaller.Program", true);
        MethodInfo removeMethod = uninstallProgram.GetMethod(
            "RemoveInstalledFiles", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);
        Check(removeMethod != null, "RemoveInstalledFiles method not found in Uninstall.exe.");

        // %LOCALAPPDATA%\BakuganRecomp protection in Installer and Uninstaller
        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrEmpty(localAppData))
            localAppData = Environment.GetEnvironmentVariable("LOCALAPPDATA") ?? "";
        if (!string.IsNullOrEmpty(localAppData))
        {
            string saveRoot = Path.Combine(localAppData, "BakuganRecomp");
            bool saveInstallRejected = false;
            try { Installer.InstallFiles(new Options { Iso = dumpRoot, Dir = saveRoot }, log, null); }
            catch (Exception) { saveInstallRejected = true; }
            Check(saveInstallRejected, "Installer must block %LOCALAPPDATA%\\BakuganRecomp.");

            bool saveUninstallRejected = false;
            try { InvokeUninstallerRemove(removeMethod, saveRoot, null); }
            catch (Exception) { saveUninstallRejected = true; }
            Check(saveUninstallRejected, "Uninstaller must block %LOCALAPPDATA%\\BakuganRecomp.");

            bool saveSubUninstallRejected = false;
            try { InvokeUninstallerRemove(removeMethod, Path.Combine(saveRoot, "dev_hdd0"), null); }
            catch (Exception) { saveSubUninstallRejected = true; }
            Check(saveSubUninstallRejected, "Uninstaller must block paths inside %LOCALAPPDATA%\\BakuganRecomp.");
        }

        // Uninstaller cleanup verification (keeps Uninstall.exe itself, removes app files & assets)
        string installedUninstaller = Path.Combine(folderInstall, "Uninstall.exe");
        Check(File.Exists(installedUninstaller), "Uninstall.exe missing from installed folder.");
        InvokeUninstallerRemove(removeMethod, folderInstall, installedUninstaller);
        Check(File.Exists(installedUninstaller), "Uninstall.exe should remain after RemoveInstalledFiles.");
        Check(!File.Exists(Path.Combine(folderInstall, "BakuganRecomp.exe")), "BakuganRecomp.exe was not removed.");
        Check(!File.Exists(Path.Combine(folderInstall, "BakuganLauncher.exe")), "BakuganLauncher.exe was not removed.");
        Check(!File.Exists(Path.Combine(folderInstall, "SDL3.dll")), "SDL3.dll was not removed.");
        Check(!Directory.Exists(Path.Combine(folderInstall, "assets")), "assets/ was not removed.");
        Check(!Directory.Exists(Path.Combine(folderInstall, "shaders")), "shaders/ was not removed.");
        Check(!Directory.Exists(Path.Combine(folderInstall, "game")), "game/ was not removed.");

        Console.WriteLine("PASS: read-only ISO repair, 1.2.0 upgrade without ISO, folder/PS3_GAME install, overlap & save guards, uninstaller cleanup, and zero-rpcs3 installed tree.");
    }
}
