using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
using Microsoft.Win32;

namespace BakuganUninstaller
{
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

            string installDir = Path.GetDirectoryName(Application.ExecutablePath);
            if (!silent)
            {
                DialogResult r = MessageBox.Show(
                    "Se eliminarán los archivos del juego y los accesos directos.\n\n" +
                    "Tus partidas y ajustes en %LOCALAPPDATA%\\BakuganRecomp se conservarán.\n\n" +
                    "¿Desinstalar Bakugan: Battle Brawlers?",
                    "Desinstalar Bakugan: Battle Brawlers",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                if (r != DialogResult.Yes) return;
            }

            try
            {
                RemoveShortcuts();
                try { Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false); } catch { }
                ScheduleDelete(installDir);
                if (!silent)
                    MessageBox.Show("Desinstalación iniciada. La carpeta se eliminará en unos segundos.",
                                    "Bakugan: Battle Brawlers", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                if (!silent)
                    MessageBox.Show("No se pudo completar la desinstalación: " + ex.Message,
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

        private static void ScheduleDelete(string dir)
        {
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return;
            string full = Path.GetFullPath(dir).TrimEnd('\\');
            string root = Path.GetPathRoot(full);
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            if (string.Equals(full, root.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase) ||
                string.Equals(full, windows.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase))
                throw new Exception("Ruta de instalación no segura: " + full);

            /* The uninstaller is running from inside the folder, so it cannot
             * remove its own directory. A short-lived batch waits for this
             * process to exit and then deletes the tree. */
            string bat = Path.Combine(Path.GetTempPath(), "bakugan-uninstall-" +
                                      Process.GetCurrentProcess().Id + ".cmd");
            File.WriteAllText(bat,
                "@echo off\r\n" +
                "chcp 65001 >nul\r\n" +
                "ping -n 4 127.0.0.1 >nul\r\n" +
                "rmdir /s /q \"" + full + "\"\r\n" +
                "del \"%~f0\"\r\n", System.Text.Encoding.UTF8);
            var psi = new ProcessStartInfo("cmd.exe", "/c \"" + bat + "\"");
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.WindowStyle = ProcessWindowStyle.Hidden;
            Process.Start(psi);
        }
    }
}
