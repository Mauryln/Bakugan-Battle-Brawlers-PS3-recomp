using System;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Bakugan: Battle Brawlers Uninstaller")]
[assembly: AssemblyDescription("Uninstaller for Bakugan: Battle Brawlers Recomp")]
[assembly: AssemblyCompany("Bakugan Recomp")]
[assembly: AssemblyProduct("Bakugan: Battle Brawlers")]
[assembly: AssemblyCopyright("Copyright (c) Bakugan Recomp Contributors (MIT)")]

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

        [STAThread]
        private static void Main(string[] args)
        {
            bool silent = false;
            foreach (string a in args)
                if (a == "/S" || a == "--silent") silent = true;

            string selfPath = Path.GetFullPath(Application.ExecutablePath);
            string installDir = Path.GetDirectoryName(selfPath);
            if (!silent)
            {
                DialogResult r = MessageBox.Show(
                    Loc.T(
                        "Se eliminarán los archivos del juego y los accesos directos.\n\n" +
                        "Tus partidas y ajustes en %LOCALAPPDATA%\\BakuganRecomp se conservarán.\n\n" +
                        "¿Desinstalar Bakugan: Battle Brawlers?",
                        "Game files and shortcuts will be removed.\n\n" +
                        "Your save data and settings in %LOCALAPPDATA%\\BakuganRecomp will be preserved.\n\n" +
                        "Uninstall Bakugan: Battle Brawlers?"),
                    Loc.T("Desinstalar Bakugan: Battle Brawlers", "Uninstall Bakugan: Battle Brawlers"),
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                if (r != DialogResult.Yes) return;
            }

            try
            {
                RemoveShortcuts();
                try { Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false); } catch { }
                RemoveInstalledFiles(installDir, selfPath);
                if (!silent)
                    MessageBox.Show(
                        Loc.T(
                            "Desinstalación completada. Todos los archivos del juego y accesos directos fueron eliminados.\n\n" +
                            "Puedes borrar manualmente esta carpeta si deseas quitar también Uninstall.exe:\n" + installDir,
                            "Uninstallation complete. All game files and shortcuts have been removed.\n\n" +
                            "You may delete this folder manually if you also want to remove Uninstall.exe:\n" + installDir),
                        "Bakugan: Battle Brawlers", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                if (!silent)
                    MessageBox.Show(
                        Loc.T("No se pudo completar la desinstalación: ",
                              "Could not complete uninstallation: ") + ex.Message,
                        "Bakugan: Battle Brawlers", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private static void RemoveShortcuts()
        {
            string desktop = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), LinkName);
            if (File.Exists(desktop)) File.Delete(desktop);
            string menu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), MenuFolder);
            if (Directory.Exists(menu)) Directory.Delete(menu, true);
        }

        internal static void RemoveInstalledFiles(string dir, string selfPath)
        {
            if (string.IsNullOrEmpty(dir)) return;
            string full = Path.GetFullPath(dir).TrimEnd('\\');
            if (full.Length == 2 && full[1] == ':') full += '\\';
            string root = Path.GetPathRoot(full);
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            if (string.Equals(full.TrimEnd('\\'), root.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase) ||
                string.Equals(full.TrimEnd('\\'), windows.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase) ||
                OverlapsSaveDirectory(full))
                throw new Exception(Loc.T("Ruta de instalación no segura: ", "Unsafe installation path: ") + full);
            if (!Directory.Exists(full)) return;

            foreach (string subDir in Directory.GetDirectories(full))
            {
                ClearReadOnlyAttributes(subDir);
                Directory.Delete(subDir, true);
            }
            foreach (string file in Directory.GetFiles(full))
            {
                if (!string.IsNullOrEmpty(selfPath) &&
                    string.Equals(Path.GetFullPath(file), Path.GetFullPath(selfPath), StringComparison.OrdinalIgnoreCase))
                    continue;
                File.SetAttributes(file, FileAttributes.Normal);
                File.Delete(file);
            }
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
            string full = Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (full.Length == 2 && full[1] == ':')
                full += Path.DirectorySeparatorChar;
            string sep = Path.DirectorySeparatorChar.ToString();
            string savePrefix = saveRoot + sep;
            string fullPrefix = full.EndsWith(sep, StringComparison.Ordinal) ? full : full + sep;
            return string.Equals(full, saveRoot, StringComparison.OrdinalIgnoreCase) ||
                   full.StartsWith(savePrefix, StringComparison.OrdinalIgnoreCase) ||
                   saveRoot.StartsWith(fullPrefix, StringComparison.OrdinalIgnoreCase);
        }

        private static void ClearReadOnlyAttributes(string dir)
        {
            foreach (string file in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
            {
                FileAttributes attrs = File.GetAttributes(file);
                if ((attrs & FileAttributes.ReadOnly) != 0)
                    File.SetAttributes(file, attrs & ~FileAttributes.ReadOnly);
            }
        }
    }
}
