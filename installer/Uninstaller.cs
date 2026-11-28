using System;
using System.ComponentModel;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Bakugan: Battle Brawlers Uninstaller")]
[assembly: AssemblyDescription("Uninstaller for Bakugan: Battle Brawlers Recomp")]
[assembly: AssemblyConfiguration("Release")]
[assembly: AssemblyCompany("Bakugan Recomp")]
[assembly: AssemblyProduct("Bakugan: Battle Brawlers")]
[assembly: AssemblyCopyright("Copyright (c) Bakugan Recomp Contributors (MIT)")]
[assembly: AssemblyCulture("")]

namespace BakuganUninstaller
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
        private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\BakuganRecomp";
        private const string LinkName = "Bakugan Battle Brawlers.lnk";
        private const string MenuFolder = "Bakugan Battle Brawlers";

        private static readonly string[] AllowedSubdirectories = new string[]
        {
            "assets",
            "shaders",
            "licenses",
            "game",
            "gamedata",
            "shadercache",
            "rpcs3",
            ".decrypt"
        };

        private static readonly string[] AllowedRootFiles = new string[]
        {
            "BakuganRecomp.exe",
            "BakuganLauncher.exe",
            "SDL3.dll",
            "ffx_fsr3_x64.dll",
            "ffx_fsr3upscaler_x64.dll",
            "ffx_opticalflow_x64.dll",
            "ffx_frameinterpolation_x64.dll",
            "ffx_backend_dx12_x64.dll",
            "msvcp140.dll",
            "vcruntime140.dll",
            "vcruntime140_1.dll",
            "BakuganBrawlers.cmd",
            "README.txt",
            "RELEASE_NOTES.md",
            "SHA256SUMS.txt",
            "installer.log"
        };

        private static readonly string[] MenuShortcutFiles = new string[]
        {
            "Bakugan Battle Brawlers.lnk",
            "Desinstalar Bakugan.lnk",
            "Uninstall Bakugan.lnk"
        };

        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();

        [STAThread]
        private static void Main(string[] args)
        {
            bool silent = false;
            foreach (string a in args)
                if (a == "/S" || a == "--silent") silent = true;

            string selfPath = Path.GetFullPath(Application.ExecutablePath);
            string installDir = Path.GetDirectoryName(selfPath);

            if (silent)
            {
                try
                {
                    ExecuteUninstall(installDir, selfPath, null);
                }
                catch
                {
                    Environment.ExitCode = 1;
                }
                return;
            }

            SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new UninstallForm(installDir, selfPath));
        }

        internal static void ExecuteUninstall(string installDir, string selfPath, Action<int, string> progress)
        {
            Report(progress, 10, Loc.T("Eliminando accesos directos...", "Removing shortcuts..."));
            RemoveShortcuts();

            Report(progress, 25, Loc.T("Limpiando registro de usuario...", "Cleaning user registry entry..."));
            try { Registry.CurrentUser.DeleteSubKey(UninstallKey, false); } catch { }

            RemoveInstalledFilesCore(installDir, selfPath, progress);
            Report(progress, 100, Loc.T("Desinstalación completada.", "Uninstallation complete."));
        }

        private static void RemoveShortcuts()
        {
            string desktop = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), LinkName);
            DeleteSingleFileIfExists(desktop);

            string menu = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Programs), MenuFolder);
            if (!Directory.Exists(menu)) return;

            var menuInfo = new DirectoryInfo(menu);
            if ((menuInfo.Attributes & FileAttributes.ReparsePoint) != 0) return;

            foreach (string shortcutName in MenuShortcutFiles)
                DeleteSingleFileIfExists(Path.Combine(menu, shortcutName));

            if (Directory.GetFileSystemEntries(menu).Length == 0)
                Directory.Delete(menu, false);
        }

        internal static void RemoveInstalledFiles(string dir, string selfPath)
        {
            RemoveInstalledFilesCore(dir, selfPath, null);
        }

        private static void RemoveInstalledFilesCore(string dir, string selfPath, Action<int, string> progress)
        {
            string full = ValidateInstallDirectory(dir);
            if (string.IsNullOrEmpty(full) || !Directory.Exists(full)) return;

            string normalizedSelf = string.IsNullOrEmpty(selfPath) ? null : Path.GetFullPath(selfPath);
            int totalSteps = AllowedSubdirectories.Length + AllowedRootFiles.Length;
            int step = 0;

            foreach (string subName in AllowedSubdirectories)
            {
                step++;
                Report(progress, 30 + (60 * step) / totalSteps,
                    Loc.T("Eliminando carpeta ", "Removing folder ") + subName + "...");

                string subPath = Path.Combine(full, subName);
                if (!Directory.Exists(subPath)) continue;

                var rootInfo = new DirectoryInfo(subPath);
                string rootNormalized = NormalizeDirectoryPath(rootInfo.FullName);
                string rootPrefix = EnsureTrailingSeparator(rootNormalized);
                DeleteDirectoryTreeManaged(rootNormalized, rootPrefix, rootInfo);
            }

            foreach (string fileName in AllowedRootFiles)
            {
                step++;
                Report(progress, 30 + (60 * step) / totalSteps,
                    Loc.T("Eliminando archivos de aplicación...", "Removing application files..."));

                string filePath = Path.Combine(full, fileName);
                if (!File.Exists(filePath)) continue;
                if (!string.IsNullOrEmpty(normalizedSelf) &&
                    string.Equals(Path.GetFullPath(filePath), normalizedSelf, StringComparison.OrdinalIgnoreCase))
                    continue;
                DeleteSingleFileIfExists(filePath);
            }
        }

        private static string ValidateInstallDirectory(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) return null;

            string full = NormalizeDirectoryPath(dir);
            string trimmedFull = full.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string trimmedRoot = (Path.GetPathRoot(full) ?? "").TrimEnd(
                Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

            if (string.Equals(trimmedFull, trimmedRoot, StringComparison.OrdinalIgnoreCase) ||
                IsDisallowedSpecialFolder(trimmedFull) ||
                OverlapsSaveDirectory(full))
            {
                throw new InvalidOperationException(
                    Loc.T("Ruta de instalación no segura: ", "Unsafe installation path: ") + full);
            }

            if (Directory.Exists(full))
            {
                FileAttributes attrs = File.GetAttributes(full);
                if ((attrs & FileAttributes.ReparsePoint) != 0)
                {
                    throw new InvalidOperationException(
                        Loc.T("Ruta de instalación no segura: ", "Unsafe installation path: ") + full);
                }
            }

            return full;
        }

        private static bool IsDisallowedSpecialFolder(string trimmedFull)
        {
            Environment.SpecialFolder[] protectedFolders = new Environment.SpecialFolder[]
            {
                Environment.SpecialFolder.Windows,
                Environment.SpecialFolder.System,
                Environment.SpecialFolder.ProgramFiles,
                Environment.SpecialFolder.ProgramFilesX86,
                Environment.SpecialFolder.UserProfile,
                Environment.SpecialFolder.DesktopDirectory,
                Environment.SpecialFolder.MyDocuments,
                Environment.SpecialFolder.ApplicationData,
                Environment.SpecialFolder.LocalApplicationData
            };

            foreach (Environment.SpecialFolder sf in protectedFolders)
            {
                string folderPath = Environment.GetFolderPath(sf);
                if (string.IsNullOrEmpty(folderPath)) continue;
                string normalized = NormalizeDirectoryPath(folderPath).TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                if (string.Equals(trimmedFull, normalized, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }

        private static bool OverlapsSaveDirectory(string dir)
        {
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (string.IsNullOrEmpty(localAppData))
                localAppData = Environment.GetEnvironmentVariable("LOCALAPPDATA") ?? "";
            if (string.IsNullOrEmpty(localAppData))
                return false;

            string saveRoot = Path.GetFullPath(Path.Combine(localAppData, "BakuganRecomp"))
                .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string full = NormalizeDirectoryPath(dir)
                .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string sep = Path.DirectorySeparatorChar.ToString();
            string savePrefix = saveRoot + sep;
            string fullPrefix = full.EndsWith(sep, StringComparison.Ordinal) ? full : full + sep;
            return string.Equals(full, saveRoot, StringComparison.OrdinalIgnoreCase) ||
                   full.StartsWith(savePrefix, StringComparison.OrdinalIgnoreCase) ||
                   saveRoot.StartsWith(fullPrefix, StringComparison.OrdinalIgnoreCase);
        }

        private static string NormalizeDirectoryPath(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd(
                Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (full.Length == 2 && full[1] == ':')
                full += Path.DirectorySeparatorChar;
            return full;
        }

        private static string EnsureTrailingSeparator(string normalizedDir)
        {
            string sep = Path.DirectorySeparatorChar.ToString();
            return normalizedDir.EndsWith(sep, StringComparison.Ordinal) ? normalizedDir : normalizedDir + sep;
        }

        private static void DeleteDirectoryTreeManaged(string allowedRoot, string rootPrefix, DirectoryInfo current)
        {
            if (!current.Exists) return;
            if ((current.Attributes & FileAttributes.ReparsePoint) != 0) return;

            string currentNormalized = NormalizeDirectoryPath(current.FullName);
            if (!string.Equals(currentNormalized, allowedRoot, StringComparison.OrdinalIgnoreCase) &&
                !currentNormalized.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
                return;

            foreach (FileInfo file in current.GetFiles())
            {
                if ((file.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                if (file.IsReadOnly) file.IsReadOnly = false;
                file.Delete();
            }

            foreach (DirectoryInfo child in current.GetDirectories())
            {
                DeleteDirectoryTreeManaged(allowedRoot, rootPrefix, child);
            }

            if ((current.Attributes & FileAttributes.ReadOnly) != 0)
                current.Attributes = current.Attributes & ~FileAttributes.ReadOnly;

            if (current.GetFileSystemInfos().Length == 0)
                current.Delete(false);
        }

        private static void DeleteSingleFileIfExists(string path)
        {
            if (!File.Exists(path)) return;
            var file = new FileInfo(path);
            if ((file.Attributes & FileAttributes.ReparsePoint) != 0) return;
            if (file.IsReadOnly) file.IsReadOnly = false;
            file.Delete();
        }

        private static void Report(Action<int, string> progress, int percent, string text)
        {
            if (progress != null) progress(percent, text);
        }
    }

    internal sealed class UninstallForm : Form
    {
        private readonly string installDir;
        private readonly string selfPath;
        private readonly Label status = new Label();
        private readonly ProgressBar bar = new ProgressBar();
        private readonly Button uninstallBtn = new Button();
        private readonly Button cancelBtn = new Button();
        private readonly BackgroundWorker worker = new BackgroundWorker();

        public UninstallForm(string installDir, string selfPath)
        {
            this.installDir = installDir;
            this.selfPath = selfPath;

            SuspendLayout();
            Text = Loc.T("Desinstalar Bakugan: Battle Brawlers", "Uninstall Bakugan: Battle Brawlers");
            ClientSize = new Size(496, 224);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9f);

            var info = new Label
            {
                Text = Loc.T(
                    "Se eliminarán los archivos del juego y los accesos directos de:\n" + installDir + "\n\n" +
                    "Tus partidas y ajustes en %LOCALAPPDATA%\\BakuganRecomp se conservarán.",
                    "Game files and shortcuts will be removed from:\n" + installDir + "\n\n" +
                    "Your save data and settings in %LOCALAPPDATA%\\BakuganRecomp will be preserved."),
                Location = new Point(18, 18),
                Size = new Size(460, 74)
            };

            bar.Location = new Point(18, 108);
            bar.Size = new Size(460, 22);

            status.Location = new Point(18, 140);
            status.Size = new Size(460, 22);
            status.Text = Loc.T("Listo para desinstalar.", "Ready to uninstall.");

            uninstallBtn.Text = Loc.T("Desinstalar", "Uninstall");
            uninstallBtn.Location = new Point(268, 174);
            uninstallBtn.Size = new Size(102, 32);
            uninstallBtn.UseVisualStyleBackColor = true;
            uninstallBtn.Click += delegate { BeginUninstall(); };

            cancelBtn.Text = Loc.T("Cancelar", "Cancel");
            cancelBtn.Location = new Point(376, 174);
            cancelBtn.Size = new Size(102, 32);
            cancelBtn.UseVisualStyleBackColor = true;
            cancelBtn.Click += delegate { Close(); };

            Controls.AddRange(new Control[] { info, bar, status, uninstallBtn, cancelBtn });
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            ResumeLayout(false);
            PerformLayout();

            worker.WorkerReportsProgress = true;
            worker.DoWork += delegate(object sender, DoWorkEventArgs e)
            {
                Program.ExecuteUninstall(this.installDir, this.selfPath,
                    delegate(int pct, string text) { worker.ReportProgress(pct, text); });
            };
            worker.ProgressChanged += delegate(object sender, ProgressChangedEventArgs e)
            {
                bar.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
                if (e.UserState != null) status.Text = (string)e.UserState;
            };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e)
            {
                if (e.Error != null)
                {
                    bar.Value = 0;
                    uninstallBtn.Enabled = true;
                    cancelBtn.Enabled = true;
                    status.Text = Loc.T("Error: ", "Error: ") + e.Error.Message;
                    MessageBox.Show(this,
                        Loc.T("No se pudo completar la desinstalación: ",
                              "Could not complete uninstallation: ") + e.Error.Message,
                        "Bakugan: Battle Brawlers", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                bar.Value = 100;
                status.Text = Loc.T("Desinstalación completada.", "Uninstallation complete.");
                cancelBtn.Text = Loc.T("Cerrar", "Close");
                cancelBtn.Enabled = true;
                MessageBox.Show(this,
                    Loc.T(
                        "Desinstalación completada. Todos los archivos del juego y accesos directos fueron eliminados.\n\n" +
                        "Puedes borrar manualmente esta carpeta si deseas quitar también Uninstall.exe:\n" + this.installDir,
                        "Uninstallation complete. All game files and shortcuts have been removed.\n\n" +
                        "You may delete this folder manually if you also want to remove Uninstall.exe:\n" + this.installDir),
                    "Bakugan: Battle Brawlers", MessageBoxButtons.OK, MessageBoxIcon.Information);
                Close();
            };
        }

        private void BeginUninstall()
        {
            uninstallBtn.Enabled = false;
            cancelBtn.Enabled = false;
            bar.Value = 0;
            worker.RunWorkerAsync();
        }
    }
}
