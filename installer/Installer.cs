using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

[assembly: AssemblyTitle("Bakugan: Battle Brawlers Setup")]
[assembly: AssemblyDescription("Installer for Bakugan: Battle Brawlers Recomp")]
[assembly: AssemblyCompany("Bakugan Recomp")]
[assembly: AssemblyProduct("Bakugan: Battle Brawlers")]
[assembly: AssemblyCopyright("Copyright (c) Bakugan Recomp Contributors (MIT)")]

namespace BakuganInstaller
{
    internal static class Loc
    {
        public static readonly bool IsSpanish =
            CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.Equals("es", StringComparison.OrdinalIgnoreCase);

        public static string T(string es, string en)
        {
            return IsSpanish ? es : en;
        }
    }

    internal static class Program
    {
        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();

        [STAThread]
        private static void Main(string[] args)
        {
            SetProcessDPIAware();
            var options = Options.Parse(args);
            if (options.Silent || options.CheckIso)
            {
                int code = SilentRun(options);
                Environment.Exit(code);
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new InstallerForm(options));
        }

        private static int SilentRun(Options o)
        {
            var log = new StringBuilder();
            try
            {
                if (o.CheckIso || o.ExtractElf)
                    InspectIso(o, log);
                else
                    Installer.Run(o, log, null);
                File.WriteAllText(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "installer.log"), log.ToString());
                return 0;
            }
            catch (Exception ex)
            {
                log.AppendLine("ERROR: " + ex.Message);
                File.WriteAllText(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "installer.log"), log.ToString());
                return 1;
            }
        }

        private static void ExtractElfForInspect(Options o, string elf, string bin, StringBuilder log)
        {
            string target = string.IsNullOrEmpty(o.Out)
                ? Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "EBOOT.elf")
                : o.Out;
            if (Installer.IsValidElf(elf))
            {
                File.Copy(elf, target, true);
                log.AppendLine("ELF extracted: " + target + " (existing EBOOT.elf)");
                return;
            }
            string reason = "";
            bool ok = File.Exists(bin) && SelfExtract.TryExtract(bin, target, out reason);
            if (!ok && Installer.IsValidElf(o.Eboot))
            {
                File.Copy(o.Eboot, target, true);
                reason = "EBOOT.elf fallback";
                ok = true;
            }
            if (!ok)
                throw new Exception("Could not extract EBOOT.elf: " + reason);
            log.AppendLine("ELF extracted: " + target + " (" + reason + ")");
        }

        private static void InspectIso(Options o, StringBuilder log)
        {
            string sourceInput = Installer.NormalizeSourcePath(o.Iso);
            if (string.IsNullOrEmpty(sourceInput))
                throw new Exception("ISO or game folder not specified.");
            if (Directory.Exists(sourceInput))
            {
                string ps3GameDir, discSfb, reason;
                if (!Installer.ResolveDirectorySource(sourceInput, out ps3GameDir, out discSfb, out reason))
                    throw new Exception(reason);
                string ud = Path.Combine(ps3GameDir, "USRDIR");
                string elf = Path.Combine(ud, "EBOOT.elf");
                string bin = Path.Combine(ud, "EBOOT.BIN");
                log.AppendLine("FOLDER OK: " + ps3GameDir + " (sfb=" + (!string.IsNullOrEmpty(discSfb)) +
                               ", elf=" + File.Exists(elf) + ", bin=" + File.Exists(bin) + ")");
                if (o.ExtractElf)
                    ExtractElfForInspect(o, elf, bin, log);
                return;
            }
            if (!File.Exists(sourceInput))
                throw new Exception("ISO or game folder not found: " + sourceInput);
            bool already;
            string letter = Iso.Mount(sourceInput, out already);
            try
            {
                string root = letter + ":\\";
                string ud = Path.Combine(root, "PS3_GAME", "USRDIR");
                string elf = Path.Combine(ud, "EBOOT.elf");
                string bin = Path.Combine(ud, "EBOOT.BIN");
                if (!File.Exists(elf) && !File.Exists(bin))
                    throw new Exception("PS3_GAME\\USRDIR\\EBOOT.BIN not found in ISO");
                log.AppendLine("ISO OK: " + root + " (elf=" + File.Exists(elf) + ", bin=" + File.Exists(bin) + ")");
                if (o.ExtractElf)
                    ExtractElfForInspect(o, elf, bin, log);
            }
            finally
            {
                Iso.Unmount(sourceInput, already);
            }
        }

    }

    internal sealed class Options
    {
        public string Iso;
        public string Dir;
        public string Eboot;
        public string Out;
        public bool Desktop = true;
        public bool StartMenu = true;
        public bool Silent;
        public bool CheckIso;
        public bool ExtractElf;

        public static Options Parse(string[] args)
        {
            var o = new Options();
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                if ((a == "--iso" || a == "--folder") && i + 1 < args.Length) o.Iso = args[++i];
                else if (a == "--dir" && i + 1 < args.Length) o.Dir = args[++i];
                else if (a == "--eboot" && i + 1 < args.Length) o.Eboot = args[++i];
                else if (a == "--out" && i + 1 < args.Length) o.Out = args[++i];
                else if (a == "--desktop" && i + 1 < args.Length) o.Desktop = args[++i] != "0";
                else if (a == "--startmenu" && i + 1 < args.Length) o.StartMenu = args[++i] != "0";
                else if (a == "--silent") o.Silent = true;
                else if (a == "--extract-elf") { o.ExtractElf = true; o.Silent = true; }
                else if (a == "--check-iso") { o.CheckIso = true; o.Silent = true; }
            }
            if (string.IsNullOrEmpty(o.Dir))
                o.Dir = Installer.DefaultInstallDirectory();
            return o;
        }
    }

    internal static class Iso
    {
        private const uint VirtualStorageTypeDeviceIso = 1;
        private static readonly Guid VirtualStorageTypeVendorMicrosoft =
            new Guid("EC984AEC-A0F9-47E9-901F-71415A66345B");

        private const uint VirtualDiskAccessAttachRo = 0x00010000;
        private const uint VirtualDiskAccessDetach = 0x00040000;
        private const uint VirtualDiskAccessGetInfo = 0x00080000;
        private const uint AttachVirtualDiskFlagReadOnly = 0x00000001;

        private static SafeFileHandle activeHandle;

        [StructLayout(LayoutKind.Sequential)]
        private struct VirtualStorageType
        {
            public uint DeviceId;
            public Guid VendorId;
        }

        [DllImport("virtdisk.dll", CharSet = CharSet.Unicode)]
        private static extern uint OpenVirtualDisk(
            ref VirtualStorageType virtualStorageType,
            string path,
            uint virtualDiskAccessMask,
            uint flags,
            IntPtr parameters,
            out SafeFileHandle handle);

        [DllImport("virtdisk.dll")]
        private static extern uint AttachVirtualDisk(
            SafeFileHandle virtualDiskHandle,
            IntPtr securityDescriptor,
            uint flags,
            uint providerSpecificFlags,
            IntPtr parameters,
            IntPtr overlapped);

        [DllImport("virtdisk.dll")]
        private static extern uint DetachVirtualDisk(
            SafeFileHandle virtualDiskHandle,
            uint flags,
            uint providerSpecificFlags);

        [DllImport("virtdisk.dll", CharSet = CharSet.Unicode)]
        private static extern uint GetVirtualDiskPhysicalPath(
            SafeFileHandle virtualDiskHandle,
            ref uint diskPathSizeInBytes,
            StringBuilder diskPath);

        private static string GetPhysicalPath(SafeFileHandle handle)
        {
            uint sizeBytes = 520;
            var sb = new StringBuilder(260);
            uint rc = GetVirtualDiskPhysicalPath(handle, ref sizeBytes, sb);
            return rc == 0 ? sb.ToString() : null;
        }

        private static HashSet<string> GetCdRomLetters()
        {
            var letters = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (DriveInfo drive in DriveInfo.GetDrives())
            {
                if (drive.DriveType == DriveType.CDRom)
                    letters.Add(drive.Name.Substring(0, 1));
            }
            return letters;
        }

        private static string FindPs3CdRomLetter()
        {
            foreach (DriveInfo drive in DriveInfo.GetDrives())
            {
                if (drive.DriveType != DriveType.CDRom || !drive.IsReady)
                    continue;
                string root = drive.Name;
                if (Directory.Exists(Path.Combine(root, "PS3_GAME")))
                    return root.Substring(0, 1);
            }
            return null;
        }

        public static string Mount(string iso, out bool alreadyMounted)
        {
            var storageType = new VirtualStorageType
            {
                DeviceId = VirtualStorageTypeDeviceIso,
                VendorId = VirtualStorageTypeVendorMicrosoft
            };
            SafeFileHandle handle;
            uint access = VirtualDiskAccessAttachRo | VirtualDiskAccessGetInfo | VirtualDiskAccessDetach;
            uint rc = OpenVirtualDisk(ref storageType, Path.GetFullPath(iso), access, 0, IntPtr.Zero, out handle);
            if (rc != 0 || handle == null || handle.IsInvalid)
                throw new Exception("Could not open ISO image (Win32 error " + rc + ")");

            HashSet<string> before = GetCdRomLetters();
            string physical = GetPhysicalPath(handle);
            alreadyMounted = !string.IsNullOrEmpty(physical);
            if (!alreadyMounted)
            {
                rc = AttachVirtualDisk(handle, IntPtr.Zero, AttachVirtualDiskFlagReadOnly, 0, IntPtr.Zero, IntPtr.Zero);
                if (rc != 0)
                {
                    handle.Dispose();
                    throw new Exception("Could not mount ISO image (Win32 error " + rc + ")");
                }
                activeHandle = handle;
            }
            else
            {
                handle.Dispose();
                activeHandle = null;
            }

            for (int attempt = 0; attempt < 50; attempt++)
            {
                if (!alreadyMounted)
                {
                    foreach (DriveInfo drive in DriveInfo.GetDrives())
                    {
                        if (drive.DriveType != DriveType.CDRom)
                            continue;
                        string letter = drive.Name.Substring(0, 1);
                        if (!before.Contains(letter) && drive.IsReady)
                            return letter;
                    }
                }
                else
                {
                    string existing = FindPs3CdRomLetter();
                    if (!string.IsNullOrEmpty(existing))
                        return existing;
                }
                Thread.Sleep(100);
            }

            Unmount(iso, alreadyMounted);
            throw new Exception("The mounted ISO did not receive a drive letter.");
        }

        public static void Unmount(string iso, bool alreadyMounted)
        {
            if (alreadyMounted)
                return;
            SafeFileHandle h = activeHandle;
            activeHandle = null;
            if (h != null && !h.IsInvalid)
            {
                try { DetachVirtualDisk(h, 0, 0); } catch { }
                try { h.Dispose(); } catch { }
            }
        }
    }

    internal static class Installer
    {
        private const string ReleaseVersion = ReleaseMetadata.Version;
        private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\BakuganRecomp";
        private static long totalBytes;
        private static long copiedBytes;
        private static Action<int, string> report;

        public static bool IsValidElf(string path)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
                return false;
            try
            {
                var info = new FileInfo(path);
                if (info.Length < 1000000)
                    return false;
                using (var f = File.OpenRead(path))
                {
                    var head = new byte[4];
                    return f.Read(head, 0, 4) == 4 &&
                           head[0] == 0x7F && head[1] == (byte)'E' &&
                           head[2] == (byte)'L' && head[3] == (byte)'F';
                }
            }
            catch
            {
                return false;
            }
        }

        public static bool CanReuseGame(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) return false;
            string game = Path.Combine(dir, "game", "PS3_GAME");
            return File.Exists(Path.Combine(dir, "BakuganLauncher.exe")) &&
                   File.Exists(Path.Combine(game, "PARAM.SFO")) &&
                   IsValidElf(Path.Combine(game, "USRDIR", "EBOOT.elf"));
        }

        public static string DefaultInstallDirectory()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(UninstallKey))
                {
                    string installed = key == null ? null : key.GetValue("InstallLocation") as string;
                    string reason;
                    if (!IsUnsafeInstallDirectory(installed, out reason) && CanReuseGame(installed))
                        return installed;
                }
            }
            catch { /* A stale registration must not block opening the installer. */ }
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                "Programs", "BakuganRecomp");
        }

        internal static string NormalizeSourcePath(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return "";
            return path.Trim().Trim('"').Trim();
        }

        private static string NormalizeDirPath(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (full.Length == 2 && full[1] == ':')
                full += Path.DirectorySeparatorChar;
            return full;
        }

        internal static bool PathsOverlap(string pathA, string pathB)
        {
            if (string.IsNullOrWhiteSpace(pathA) || string.IsNullOrWhiteSpace(pathB))
                return false;
            try
            {
                string a = NormalizeDirPath(pathA);
                string b = NormalizeDirPath(pathB);
                string sep = Path.DirectorySeparatorChar.ToString();
                string aPrefix = a.EndsWith(sep, StringComparison.Ordinal) ? a : a + sep;
                string bPrefix = b.EndsWith(sep, StringComparison.Ordinal) ? b : b + sep;
                return string.Equals(a, b, StringComparison.OrdinalIgnoreCase) ||
                       b.StartsWith(aPrefix, StringComparison.OrdinalIgnoreCase) ||
                       a.StartsWith(bPrefix, StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        internal static bool ResolveDirectorySource(string inputPath, out string ps3GameDir,
                                                    out string discSfbPath, out string reason)
        {
            ps3GameDir = null;
            discSfbPath = null;
            reason = null;

            string normalized = NormalizeSourcePath(inputPath);
            if (string.IsNullOrEmpty(normalized) || !Directory.Exists(normalized))
            {
                reason = Loc.T(
                    "La carpeta seleccionada no existe.",
                    "The selected folder does not exist.");
                return false;
            }

            string full;
            try
            {
                full = NormalizeDirPath(normalized);
            }
            catch
            {
                reason = Loc.T("La ruta de origen no es válida.", "The source path is invalid.");
                return false;
            }

            string candidateGame = Path.Combine(full, "PS3_GAME");
            if (Directory.Exists(candidateGame))
            {
                ps3GameDir = candidateGame;
                string sfb = Path.Combine(full, "PS3_DISC.SFB");
                if (File.Exists(sfb))
                    discSfbPath = sfb;
            }
            else if (string.Equals(Path.GetFileName(full), "PS3_GAME", StringComparison.OrdinalIgnoreCase) ||
                     Directory.Exists(Path.Combine(full, "USRDIR")))
            {
                ps3GameDir = full;
                string parent = Path.GetDirectoryName(full);
                if (!string.IsNullOrEmpty(parent))
                {
                    string sfb = Path.Combine(parent, "PS3_DISC.SFB");
                    if (File.Exists(sfb))
                        discSfbPath = sfb;
                }
            }
            else
            {
                reason = Loc.T(
                    "La carpeta seleccionada no es válida o no corresponde a Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "Debe contener PS3_GAME\\USRDIR\\EBOOT.BIN (o ser directamente la carpeta PS3_GAME).",
                    "The selected folder is invalid or does not match Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "It must contain PS3_GAME\\USRDIR\\EBOOT.BIN (or be the PS3_GAME folder directly).");
                return false;
            }

            string ud = Path.Combine(ps3GameDir, "USRDIR");
            string elf = Path.Combine(ud, "EBOOT.elf");
            string bin = Path.Combine(ud, "EBOOT.BIN");
            if (!Directory.Exists(ps3GameDir) || (!File.Exists(elf) && !File.Exists(bin)))
            {
                reason = Loc.T(
                    "La carpeta seleccionada no es válida o no corresponde a Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "No contiene la estructura PS3_GAME\\USRDIR\\EBOOT.BIN.",
                    "The selected folder is invalid or does not match Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "It does not contain PS3_GAME\\USRDIR\\EBOOT.BIN.");
                return false;
            }

            return true;
        }

        public static bool IsUnsafeSourceOverlap(string sourceDir, string installDir, out string reason)
        {
            reason = null;
            string normalized = NormalizeSourcePath(sourceDir);
            if (string.IsNullOrEmpty(normalized) || !Directory.Exists(normalized) || string.IsNullOrWhiteSpace(installDir))
                return false;

            bool overlap = PathsOverlap(normalized, installDir);
            if (!overlap)
            {
                string ps3GameDir, discSfbPath, resolveReason;
                if (ResolveDirectorySource(normalized, out ps3GameDir, out discSfbPath, out resolveReason))
                    overlap = PathsOverlap(ps3GameDir, installDir);
            }
            if (overlap)
            {
                reason = Loc.T(
                    "La carpeta de origen del juego no puede coincidir ni solaparse con la carpeta de instalación (evita copia recursiva o sobrescritura).",
                    "The source game folder cannot match or overlap the installation folder (prevents recursive copy or self-overwrite).");
                return true;
            }
            return false;
        }

        public static bool IsUnsafeInstallDirectory(string dir, out string reason)
        {
            reason = null;
            if (string.IsNullOrWhiteSpace(dir))
            {
                reason = Loc.T("Selecciona la carpeta de instalación.",
                               "Please select the installation folder.");
                return true;
            }
            string full;
            try
            {
                full = NormalizeDirPath(dir);
            }
            catch
            {
                reason = Loc.T("La ruta de instalación no es válida.",
                               "The installation path is invalid.");
                return true;
            }
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (string.IsNullOrEmpty(localAppData))
                localAppData = Environment.GetEnvironmentVariable("LOCALAPPDATA") ?? "";
            if (!string.IsNullOrEmpty(localAppData))
            {
                string saveRoot = Path.GetFullPath(Path.Combine(localAppData, "BakuganRecomp"))
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                if (PathsOverlap(full, saveRoot))
                {
                    reason = Loc.T(
                        "No se puede instalar en '" + saveRoot + "' (ni en una carpeta que la contenga) porque ahí se guardan tus partidas y ajustes (player.ini), y Uninstall.exe borraría tus partidas.\n\n" +
                        "Elige otra carpeta de instalación, por ejemplo:\n" + Path.Combine(localAppData, "Programs", "BakuganRecomp"),
                        "Cannot install into '" + saveRoot + "' (or a folder containing it) because your save data and settings (player.ini) are stored there, and Uninstall.exe would delete your saves.\n\n" +
                        "Please choose another installation folder, for example:\n" + Path.Combine(localAppData, "Programs", "BakuganRecomp"));
                    return true;
                }
            }
            return false;
        }

        public static void Run(Options o, StringBuilder log, Action<int, string> progress)
        {
            InstallFiles(o, log, progress);
            Status(98, Loc.T("Creando accesos directos...", "Creating shortcuts..."));
            string launcher = Path.Combine(o.Dir, "BakuganLauncher.exe");
            string uninstaller = Path.Combine(o.Dir, "Uninstall.exe");
            if (o.Desktop) Shortcut.Create(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                           launcher, o.Dir, "Bakugan Battle Brawlers",
                                           Loc.T("Bakugan: Battle Brawlers", "Bakugan: Battle Brawlers"));
            string menuDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs),
                                          "Bakugan Battle Brawlers");
            if (o.StartMenu)
            {
                Shortcut.Create(menuDir, launcher, o.Dir, "Bakugan Battle Brawlers",
                                Loc.T("Bakugan: Battle Brawlers", "Bakugan: Battle Brawlers"));
                if (File.Exists(uninstaller))
                    Shortcut.Create(menuDir, uninstaller, o.Dir,
                                    Loc.T("Desinstalar Bakugan", "Uninstall Bakugan"),
                                    Loc.T("Desinstalar Bakugan: Battle Brawlers", "Uninstall Bakugan: Battle Brawlers"));
            }

            try
            {
                using (RegistryKey k = Registry.CurrentUser.CreateSubKey(UninstallKey))
                {
                    if (k != null)
                    {
                        k.SetValue("DisplayName", "Bakugan: Battle Brawlers");
                        k.SetValue("DisplayVersion", ReleaseVersion);
                        k.SetValue("Publisher", "Bakugan Recomp Team");
                        k.SetValue("InstallLocation", o.Dir);
                        k.SetValue("DisplayIcon", launcher);
                        k.SetValue("UninstallString", "\"" + uninstaller + "\"");
                        k.SetValue("NoModify", 1, RegistryValueKind.DWord);
                        k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                    }
                }
            }
            catch { /* a locked registry must not fail the installation */ }

            Status(100, Loc.T("Instalación completada.", "Installation completed."));
            log.AppendLine("desktop=" + o.Desktop + " startmenu=" + o.StartMenu);
        }

        internal static void InstallFiles(Options o, StringBuilder log, Action<int, string> progress)
        {
            report = progress;
            string unsafeReason;
            if (IsUnsafeInstallDirectory(o.Dir, out unsafeReason))
                throw new Exception(unsafeReason);
            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            string payload = Path.Combine(exeDir, "payload");
            if (!Directory.Exists(payload) || !File.Exists(Path.Combine(payload, "BakuganLauncher.exe")))
            {
                throw new Exception(Loc.T(
                    "No se encontró la carpeta 'payload' junto al instalador.\n\n" +
                    "Por favor, asegúrate de haber descomprimido completamente todo el contenido del archivo ZIP " +
                    "(Bakugan-Battle-Brawlers-Recomp-" + ReleaseVersion + "-win64.zip) en una carpeta antes de ejecutar BakuganSetup.exe.",
                    "The 'payload' folder was not found next to the installer.\n\n" +
                    "Please make sure you have fully extracted all contents of the ZIP archive " +
                    "(Bakugan-Battle-Brawlers-Recomp-" + ReleaseVersion + "-win64.zip) into a folder before running BakuganSetup.exe."));
            }

            string sourceInput = NormalizeSourcePath(o.Iso);
            bool reuseGame = string.IsNullOrWhiteSpace(sourceInput) && CanReuseGame(o.Dir);
            bool isFolder = !reuseGame && !string.IsNullOrEmpty(sourceInput) && Directory.Exists(sourceInput);
            bool isFile = !reuseGame && !isFolder && !string.IsNullOrEmpty(sourceInput) && File.Exists(sourceInput);
            if (!reuseGame && !isFolder && !isFile)
                throw new Exception(Loc.T("Selecciona una ISO o carpeta del juego válida para instalar o reparar el juego.",
                                          "Select a valid ISO or game folder to install or repair the game."));

            string folderSource = null;
            string folderSfb = null;
            if (isFolder)
            {
                string overlapReason;
                if (IsUnsafeSourceOverlap(sourceInput, o.Dir, out overlapReason))
                    throw new Exception(overlapReason);
                string folderReason;
                if (!ResolveDirectorySource(sourceInput, out folderSource, out folderSfb, out folderReason))
                    throw new Exception(folderReason);
            }

            Directory.CreateDirectory(o.Dir);
            Status(0, Loc.T("Copiando archivos del programa...", "Copying application files..."));
            totalBytes = 0; copiedBytes = 0;
            CountTree(payload);
            CopyTree(payload, o.Dir, 0, 30);
            try { File.Delete(Path.Combine(o.Dir, "shadercache", "fp_v2.cache")); } catch { }
            try
            {
                string legacyRpcs3 = Path.Combine(o.Dir, "rpcs3");
                if (Directory.Exists(legacyRpcs3)) Directory.Delete(legacyRpcs3, true);
            }
            catch { }
            log.AppendLine("payload copied to " + o.Dir);

            if (reuseGame)
            {
                log.AppendLine("existing game data reused");
                return;
            }

            if (isFolder)
            {
                InstallGameFromSource(folderSource, folderSfb, o.Dir, o.Eboot, log);
                log.AppendLine("game source: folder (" + folderSource + ")");
                return;
            }

            bool already;
            string letter = Iso.Mount(sourceInput, out already);
            try
            {
                string source = Path.Combine(letter + ":\\", "PS3_GAME");
                string sfb = Path.Combine(letter + ":\\", "PS3_DISC.SFB");
                InstallGameFromSource(source, sfb, o.Dir, o.Eboot, log);
                log.AppendLine("game source: ISO (" + sourceInput + ")");
            }
            finally
            {
                Iso.Unmount(sourceInput, already);
            }
        }

        private static void InstallGameFromSource(string source, string sfb, string installDir,
                                                  string fallbackEboot, StringBuilder log)
        {
            string sourceElf = Path.Combine(source, "USRDIR", "EBOOT.elf");
            string sourceBin = Path.Combine(source, "USRDIR", "EBOOT.BIN");
            if (!Directory.Exists(source) || (!File.Exists(sourceElf) && !File.Exists(sourceBin)))
            {
                throw new Exception(Loc.T(
                    "El origen seleccionado no es válido o no corresponde a Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "No contiene la estructura PS3_GAME\\USRDIR\\EBOOT.BIN.",
                    "The selected source is invalid or does not match Bakugan: Battle Brawlers (PS3 BLES00758). " +
                    "It does not contain PS3_GAME\\USRDIR\\EBOOT.BIN."));
            }

            string gameDir = Path.Combine(installDir, "game");
            Directory.CreateDirectory(gameDir);
            Status(30, Loc.T("Copiando datos del juego...", "Copying game data..."));
            totalBytes = 0; copiedBytes = 0;
            CountTree(source);
            CopyTree(source, Path.Combine(gameDir, "PS3_GAME"), 30, 95);

            if (!string.IsNullOrEmpty(sfb) && File.Exists(sfb))
                CopyInstallFile(sfb, Path.Combine(gameDir, "PS3_DISC.SFB"), 95, 95);

            /* Retail discs carry an encrypted EBOOT.BIN. The port boots the
             * decrypted ELF, extracted natively during install via SelfExtract
             * (or via the optional --eboot CLI fallback), with no emulator bundled. */
            string installedElf = Path.Combine(installDir, "game", "PS3_GAME", "USRDIR", "EBOOT.elf");
            string reason = "";
            if (!IsValidElf(installedElf))
            {
                MakeInstallFileWritable(installedElf);
                if (IsValidElf(sourceElf))
                {
                    CopyInstallFile(sourceElf, installedElf, 95, 95);
                    log.AppendLine("EBOOT.elf copied from decrypted source");
                }
                else if (File.Exists(sourceBin))
                {
                    Status(95, Loc.T("Extrayendo EBOOT.elf...", "Extracting EBOOT.elf..."));
                    bool extracted = SelfExtract.TryExtract(sourceBin, installedElf, out reason);
                    if (extracted && IsValidElf(installedElf))
                        log.AppendLine("EBOOT.elf extracted with SelfExtract: " + reason);
                }
                else if (File.Exists(sourceElf))
                {
                    reason = Loc.T("El archivo EBOOT.elf del origen está truncado o dañado.",
                                   "The EBOOT.elf file in the source is truncated or corrupted.");
                }

                if (!IsValidElf(installedElf) && IsValidElf(fallbackEboot))
                {
                    CopyInstallFile(fallbackEboot, installedElf, 96, 96);
                    log.AppendLine("EBOOT.elf copied from --eboot fallback: " + fallbackEboot);
                }
            }

            // Always verify resulting ELF integrity and size (including re-installations)
            if (!IsValidElf(installedElf))
            {
                throw new Exception(Loc.T(
                    "No se pudo extraer automáticamente un EBOOT.elf válido desde la ISO o carpeta seleccionada.\n\n" +
                    "Causa: " + (string.IsNullOrEmpty(reason) ? "EBOOT.elf ausente, truncado (< 1 MB) o cabecera ELF inválida" : reason) + "\n\n" +
                    "Asegúrate de haber descomprimido todo el ZIP (con la carpeta 'payload' junto a BakuganSetup.exe) y de usar una copia válida de PS3 (BLES00758).",
                    "Could not automatically extract a valid EBOOT.elf from the selected ISO or folder.\n\n" +
                    "Cause: " + (string.IsNullOrEmpty(reason) ? "EBOOT.elf missing, truncated (< 1 MB), or invalid ELF header" : reason) + "\n\n" +
                    "Ensure you extracted the complete ZIP (with the 'payload' folder beside BakuganSetup.exe) and selected a valid European PS3 copy (BLES00758)."));
            }
            log.AppendLine("game data copied to " + gameDir);
        }

        public static void CountTree(string path)
        {
            foreach (var f in Directory.GetFiles(path)) totalBytes += new FileInfo(f).Length;
            foreach (var d in Directory.GetDirectories(path)) CountTree(d);
        }

        public static void CopyTree(string src, string dst, int fromPercent, int toPercent)
        {
            Directory.CreateDirectory(dst);
            foreach (var file in Directory.GetFiles(src))
                CopyInstallFile(file, Path.Combine(dst, Path.GetFileName(file)), fromPercent, toPercent);
            foreach (var dir in Directory.GetDirectories(src))
                CopyTree(dir, Path.Combine(dst, Path.GetFileName(dir)), fromPercent, toPercent);
        }

        private static void MakeInstallFileWritable(string path)
        {
            if (!File.Exists(path)) return;
            FileAttributes attributes = File.GetAttributes(path);
            if ((attributes & FileAttributes.ReadOnly) != 0)
                File.SetAttributes(path, attributes & ~FileAttributes.ReadOnly);
        }

        private static void CopyInstallFile(string source, string target, int fromPercent, int toPercent)
        {
            // Prior installers copied PS3_DISC.SFB directly from the ISO along with its read-only bit.
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                MakeInstallFileWritable(target);
                using (var output = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None))
                {
                    var buffer = new byte[1024 * 1024];
                    int n;
                    while ((n = input.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        output.Write(buffer, 0, n);
                        copiedBytes += n;
                        if (totalBytes > 0)
                        {
                            int pct = fromPercent + (int)((toPercent - fromPercent) * copiedBytes / totalBytes);
                            Status(Math.Min(pct, toPercent), Loc.T("Copiando ", "Copying ") + Path.GetFileName(source) + "...");
                        }
                    }
                }
            }
        }

        private static void Status(int percent, string text)
        {
            if (report != null) report(percent, text);
        }
    }

    internal static class Shortcut
    {
        [ComImport]
        [Guid("00021401-0000-0000-C000-000000000046")]
        private class ShellLink
        {
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214F9-0000-0000-C000-000000000046")]
        private interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int maxPath, IntPtr findData, uint flags);
            void GetIDList(out IntPtr idList);
            void SetIDList(IntPtr idList);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int maxName);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder dir, int maxPath);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder args, int maxPath);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
            void GetHotkey(out short hotkey);
            void SetHotkey(short hotkey);
            void GetShowCmd(out int showCmd);
            void SetShowCmd(int showCmd);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder iconPath, int iconPathLen, out int iconIndex);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string iconPath, int iconIndex);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string pathRel, uint reserved);
            void Resolve(IntPtr hwnd, uint flags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
        }

        public static void Create(string folder, string target, string workDir,
                                  string name = "Bakugan Battle Brawlers",
                                  string description = "Bakugan: Battle Brawlers")
        {
            Directory.CreateDirectory(folder);
            string linkPath = Path.Combine(folder, name + ".lnk");
            var link = (IShellLinkW)new ShellLink();
            try
            {
                link.SetPath(target);
                link.SetWorkingDirectory(workDir);
                link.SetIconLocation(target, 0);
                link.SetDescription(description);
                ((IPersistFile)link).Save(linkPath, false);
            }
            finally
            {
                Marshal.FinalReleaseComObject(link);
            }
        }
    }

    internal sealed class InstallerForm : Form
    {
        private readonly TextBox iso = new TextBox();
        private readonly TextBox dir = new TextBox();
        private readonly CheckBox desktop = new CheckBox();
        private readonly CheckBox startMenu = new CheckBox();
        private readonly Button install = new Button();
        private readonly Button browseIso = new Button();
        private readonly Button browseFolder = new Button();
        private readonly Button browseDir = new Button();
        private readonly ProgressBar bar = new ProgressBar();
        private readonly Label status = new Label();
        private readonly BackgroundWorker worker = new BackgroundWorker();
        private readonly StringBuilder log = new StringBuilder();
        private readonly string initialEboot;

        public InstallerForm() : this(null)
        {
        }

        public InstallerForm(Options initial)
        {
            SuspendLayout();
            Text = Loc.T("Bakugan: Battle Brawlers - Instalador", "Bakugan: Battle Brawlers - Installer");
            ClientSize = new Size(572, 336);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9f);

            var l1 = new Label
            {
                Text = Loc.T("ISO o carpeta del juego (PS3_GAME, BLES00758). Opcional al actualizar:",
                             "Game ISO or folder (PS3_GAME, BLES00758). Optional when updating:"),
                Location = new Point(18, 18),
                AutoSize = true
            };
            iso.Location = new Point(18, 42);
            iso.Width = 350;
            browseIso.Text = "ISO...";
            browseIso.Location = new Point(376, 40);
            browseIso.Size = new Size(86, 28);
            browseIso.UseVisualStyleBackColor = true;
            browseIso.Click += delegate { BrowseIso(); };
            browseFolder.Text = Loc.T("Carpeta...", "Folder...");
            browseFolder.Location = new Point(468, 40);
            browseFolder.Size = new Size(86, 28);
            browseFolder.UseVisualStyleBackColor = true;
            browseFolder.Click += delegate { BrowseFolder(); };

            var l2 = new Label
            {
                Text = Loc.T("Carpeta de instalación:", "Installation folder:"),
                Location = new Point(18, 86),
                AutoSize = true
            };
            dir.Location = new Point(18, 110);
            dir.Width = 444;
            dir.Text = Installer.DefaultInstallDirectory();
            browseDir.Text = Loc.T("Examinar...", "Browse...");
            browseDir.Location = new Point(468, 108);
            browseDir.Size = new Size(86, 28);
            browseDir.UseVisualStyleBackColor = true;
            browseDir.Click += delegate { BrowseDir(); };

            desktop.Text = Loc.T("Crear icono en el escritorio", "Create desktop shortcut");
            desktop.Location = new Point(18, 156);
            desktop.AutoSize = true;
            desktop.Checked = true;

            startMenu.Text = Loc.T("Añadir al menú Inicio", "Add to Start Menu");
            startMenu.Location = new Point(18, 184);
            startMenu.AutoSize = true;
            startMenu.Checked = true;

            if (initial != null)
            {
                string initialSrc = Installer.NormalizeSourcePath(initial.Iso);
                if (!string.IsNullOrEmpty(initialSrc)) iso.Text = initialSrc;
                if (!string.IsNullOrWhiteSpace(initial.Dir)) dir.Text = initial.Dir;
                initialEboot = initial.Eboot;
                desktop.Checked = initial.Desktop;
                startMenu.Checked = initial.StartMenu;
            }

            install.Text = Loc.T("Instalar / Actualizar", "Install / Update");
            install.Location = new Point(400, 284);
            install.Size = new Size(154, 34);
            install.UseVisualStyleBackColor = true;
            install.Click += delegate { BeginInstall(); };

            bar.Location = new Point(18, 224);
            bar.Size = new Size(536, 22);

            status.Location = new Point(18, 256);
            status.AutoSize = true;
            status.Text = Loc.T("Listo para instalar.", "Ready to install.");

            Controls.AddRange(new Control[] { l1, iso, browseIso, browseFolder, l2, dir, browseDir,
                                              desktop, startMenu, bar, status, install });
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            ResumeLayout(false);
            PerformLayout();

            worker.WorkerReportsProgress = true;
            worker.DoWork += delegate(object sender, DoWorkEventArgs e)
            {
                var o = (Options)e.Argument;
                Installer.Run(o, log, delegate(int pct, string text) { worker.ReportProgress(pct, text); });
            };
            worker.ProgressChanged += delegate(object sender, ProgressChangedEventArgs e)
            {
                bar.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
                if (e.UserState != null) status.Text = (string)e.UserState;
            };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e)
            {
                install.Enabled = true;
                if (e.Error != null)
                {
                    bar.Value = 0;
                    status.Text = Loc.T("Error: ", "Error: ") + e.Error.Message;
                    MessageBox.Show(this, e.Error.Message,
                                    Loc.T("Error de instalación", "Installation Error"),
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                else
                {
                    bar.Value = 100;
                    status.Text = Loc.T("Instalación completada.", "Installation completed.");
                    MessageBox.Show(this,
                                    Loc.T("Bakugan: Battle Brawlers se ha instalado correctamente.",
                                          "Bakugan: Battle Brawlers has been installed successfully."),
                                    Loc.T("Instalación completada", "Installation Complete"),
                                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            };
        }

        private void BrowseIso()
        {
            using (var d = new OpenFileDialog())
            {
                d.Filter = Loc.T("Imagen ISO (*.iso)|*.iso|Todos los archivos (*.*)|*.*",
                                 "ISO Image (*.iso)|*.iso|All files (*.*)|*.*");
                if (d.ShowDialog(this) == DialogResult.OK) iso.Text = d.FileName;
            }
        }

        private void BrowseFolder()
        {
            using (var d = new FolderBrowserDialog())
            {
                d.Description = Loc.T(
                    "Selecciona la carpeta del juego (la que contiene PS3_GAME o directamente PS3_GAME):",
                    "Select the game folder (the folder containing PS3_GAME, or PS3_GAME directly):");
                d.ShowNewFolderButton = false;
                string current = Installer.NormalizeSourcePath(iso.Text);
                if (!string.IsNullOrEmpty(current) && Directory.Exists(current))
                    d.SelectedPath = current;
                if (d.ShowDialog(this) == DialogResult.OK) iso.Text = d.SelectedPath;
            }
        }

        private void BrowseDir()
        {
            using (var d = new FolderBrowserDialog())
            {
                d.SelectedPath = dir.Text;
                if (d.ShowDialog(this) == DialogResult.OK) dir.Text = d.SelectedPath;
            }
        }

        private void BeginInstall()
        {
            string unsafeReason;
            if (Installer.IsUnsafeInstallDirectory(dir.Text, out unsafeReason))
            {
                MessageBox.Show(this,
                                unsafeReason,
                                Loc.T("Carpeta no permitida", "Disallowed Folder"),
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string src = Installer.NormalizeSourcePath(iso.Text);
            if (string.IsNullOrEmpty(src))
            {
                if (!Installer.CanReuseGame(dir.Text))
                {
                    MessageBox.Show(this,
                                    Loc.T("Selecciona una ISO o carpeta válida (PS3_GAME), o deja el campo vacío para actualizar una instalación existente.",
                                          "Select a valid ISO or game folder (PS3_GAME), or leave it blank to update an existing installation."),
                                    Loc.T("Falta el origen del juego", "Missing Game Source"),
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }
            else if (Directory.Exists(src))
            {
                string overlapReason;
                if (Installer.IsUnsafeSourceOverlap(src, dir.Text, out overlapReason))
                {
                    MessageBox.Show(this,
                                    overlapReason,
                                    Loc.T("Carpeta no permitida", "Disallowed Folder"),
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                string ps3GameDir, discSfb, folderReason;
                if (!Installer.ResolveDirectorySource(src, out ps3GameDir, out discSfb, out folderReason))
                {
                    MessageBox.Show(this,
                                    folderReason,
                                    Loc.T("Carpeta de juego no válida", "Invalid Game Folder"),
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }
            else if (!File.Exists(src))
            {
                MessageBox.Show(this,
                                Loc.T("Selecciona una ISO o carpeta válida (PS3_GAME), o deja el campo vacío para actualizar una instalación existente.",
                                      "Select a valid ISO or game folder (PS3_GAME), or leave it blank to update an existing installation."),
                                Loc.T("Origen no encontrado", "Source Not Found"),
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            install.Enabled = false;
            bar.Value = 0;
            worker.RunWorkerAsync(new Options
            {
                Iso = src,
                Dir = dir.Text,
                Eboot = initialEboot,
                Desktop = desktop.Checked,
                StartMenu = startMenu.Checked
            });
        }
    }
}
