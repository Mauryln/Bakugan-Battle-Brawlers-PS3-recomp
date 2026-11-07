using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

namespace BakuganInstaller
{
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
            Application.Run(new InstallerForm());
        }

        private static int SilentRun(Options o)
        {
            var log = new StringBuilder();
            try
            {
                if (string.IsNullOrEmpty(o.Iso) || !File.Exists(o.Iso))
                    throw new Exception("ISO not found: " + o.Iso);
                bool already;
                string letter = Iso.Mount(o.Iso, out already);
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
                    {
                        string target = string.IsNullOrEmpty(o.Out)
                            ? Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "EBOOT.elf")
                            : o.Out;
                        string reason = "";
                        bool ok = SelfExtract.TryExtract(bin, target, out reason);
                        if (!ok)
                        {
                            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
                            string rpcs3Elf = null, rpcs3Reason = "";
                            ok = Rpcs3Decrypt.TryDecrypt(bin, Path.Combine(exeDir, ".decrypt"),
                                Path.Combine(exeDir, "payload", "rpcs3"), out rpcs3Elf, out rpcs3Reason);
                            if (ok)
                            {
                                File.Copy(rpcs3Elf, target, true);
                                reason = "RPCS3 --decrypt";
                            }
                            else
                            {
                                reason = reason + " " + rpcs3Reason;
                            }
                        }
                        if (!ok)
                            throw new Exception("No se pudo extraer el ELF: " + reason);
                        log.AppendLine("ELF extraído: " + target + " (" + reason + ")");
                    }
                    else if (!o.CheckIso)
                    {
                        if (string.IsNullOrEmpty(o.Dir))
                            throw new Exception("Install folder is empty");
                        Installer.Run(o, log, null);
                    }
                }
                finally
                {
                    Iso.Unmount(o.Iso, already);
                }
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
                if (a == "--iso" && i + 1 < args.Length) o.Iso = args[++i];
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
                o.Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                     "Programs", "BakuganRecomp");
            if (string.IsNullOrEmpty(o.Eboot))
            {
                string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
                string[] guesses = {
                    Path.Combine(exeDir, "game", "PS3_GAME", "USRDIR", "EBOOT.elf"),
                    Path.Combine(exeDir, "..", "game", "PS3_GAME", "USRDIR", "EBOOT.elf"),
                    Path.Combine(exeDir, "..", "..", "game", "PS3_GAME", "USRDIR", "EBOOT.elf"),
                    Path.Combine(Environment.CurrentDirectory, "game", "PS3_GAME", "USRDIR", "EBOOT.elf")
                };
                foreach (string guess in guesses)
                {
                    if (File.Exists(guess)) { o.Eboot = Path.GetFullPath(guess); break; }
                }
            }
            return o;
        }
    }

    internal static class Iso
    {
        internal static string RunPowerShell(string command)
        {
            var psi = new ProcessStartInfo("powershell.exe",
                "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + command.Replace("\"", "\\\"") + "\"");
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            using (var p = Process.Start(psi))
            {
                string output = p.StandardOutput.ReadToEnd().Trim();
                string error = p.StandardError.ReadToEnd().Trim();
                p.WaitForExit();
                if (p.ExitCode != 0)
                    throw new Exception("PowerShell failed: " + (error.Length > 0 ? error : output));
                return output;
            }
        }

        private static string Quote(string path) { return path.Replace("'", "''"); }

        public static string Mount(string iso, out bool alreadyMounted)
        {
            string attached = RunPowerShell(
                "$i = Get-DiskImage -ImagePath '" + Quote(iso) + "' -ErrorAction SilentlyContinue; " +
                "if ($i -and $i.Attached) { '1' } else { '0' }");
            alreadyMounted = attached == "1";
            string command =
                "$i = Get-DiskImage -ImagePath '" + Quote(iso) + "' -ErrorAction SilentlyContinue; " +
                "if (-not $i -or -not $i.Attached) { $i = Mount-DiskImage -ImagePath '" + Quote(iso) + "' -PassThru }; " +
                "$v = $i | Get-Volume; if (-not $v -or -not $v.DriveLetter) { throw 'The ISO has no drive letter' }; " +
                "$v.DriveLetter";
            string letter = RunPowerShell(command).Trim();
            if (letter.Length != 1)
                throw new Exception("Could not mount ISO: " + letter);
            return letter;
        }

        public static void Unmount(string iso, bool alreadyMounted)
        {
            if (alreadyMounted) return;
            try
            {
                RunPowerShell("Dismount-DiskImage -ImagePath '" + Quote(iso) + "' | Out-Null");
            }
            catch { }
        }
    }

    internal static class Installer
    {
        private static long totalBytes;
        private static long copiedBytes;
        private static Action<int, string> report;

        public static void Run(Options o, StringBuilder log, Action<int, string> progress)
        {
            report = progress;
            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            string payload = Path.Combine(exeDir, "payload");
            if (!File.Exists(Path.Combine(payload, "BakuganLauncher.exe")))
                throw new Exception("payload\\BakuganLauncher.exe is missing next to the installer");

            Directory.CreateDirectory(o.Dir);
            Status(0, "Copiando el programa...");
            totalBytes = 0; copiedBytes = 0;
            CountTree(payload);
            CopyTree(payload, o.Dir, 0, 30);
            log.AppendLine("payload copied to " + o.Dir);

            bool already;
            string letter = Iso.Mount(o.Iso, out already);
            try
            {
                string source = Path.Combine(letter + ":\\", "PS3_GAME");
                string sourceElf = Path.Combine(source, "USRDIR", "EBOOT.elf");
                string sourceBin = Path.Combine(source, "USRDIR", "EBOOT.BIN");
                if (!File.Exists(sourceElf) && !File.Exists(sourceBin))
                    throw new Exception("The selected ISO does not contain PS3_GAME\\USRDIR\\EBOOT.BIN");

                string gameDir = Path.Combine(o.Dir, "game");
                Directory.CreateDirectory(gameDir);
                Status(30, "Copiando los datos del juego...");
                totalBytes = 0; copiedBytes = 0;
                CountTree(source);
                CopyTree(source, Path.Combine(gameDir, "PS3_GAME"), 30, 95);

                string sfb = Path.Combine(letter + ":\\", "PS3_DISC.SFB");
                if (File.Exists(sfb)) File.Copy(sfb, Path.Combine(gameDir, "PS3_DISC.SFB"), true);

                /* Retail discs carry an encrypted EBOOT.BIN. The port boots the
                 * decrypted ELF, so use the one inside the ISO when the dump is
                 * already decrypted, otherwise the user-supplied ELF. We never
                 * ship keys or game code. */
                string installedElf = Path.Combine(o.Dir, "game", "PS3_GAME", "USRDIR", "EBOOT.elf");
                if (!File.Exists(installedElf))
                {
                    string reason = "";
                    bool extracted = File.Exists(sourceBin) &&
                                     SelfExtract.TryExtract(sourceBin, installedElf, out reason);
                    if (extracted)
                    {
                        log.AppendLine("EBOOT.elf extracted from ISO: " + reason);
                    }
                    else
                    {
                        Status(96, "Extrayendo EBOOT.elf con RPCS3...");
                        string rpcs3Elf = null;
                        string rpcs3Reason = "";
                        string rpcs3Dir = Path.Combine(o.Dir, "rpcs3");
                        string workDir = Path.Combine(o.Dir, ".decrypt");
                        bool rpcs3 = File.Exists(sourceBin) &&
                            Rpcs3Decrypt.TryDecrypt(sourceBin, workDir, rpcs3Dir,
                                                    out rpcs3Elf, out rpcs3Reason);
                        if (rpcs3)
                        {
                            File.Copy(rpcs3Elf, installedElf, true);
                            try { Directory.Delete(workDir, true); } catch { }
                            log.AppendLine("EBOOT.elf extracted with RPCS3");
                        }
                        else if (!string.IsNullOrEmpty(o.Eboot) && File.Exists(o.Eboot))
                        {
                            File.Copy(o.Eboot, installedElf, true);
                            log.AppendLine("EBOOT.elf copied from " + o.Eboot + " (" + rpcs3Reason + ")");
                        }
                        else
                        {
                            throw new Exception(
                                (string.IsNullOrEmpty(reason) ? "" : reason + " ") +
                                (string.IsNullOrEmpty(rpcs3Reason) ? "" : "RPCS3: " + rpcs3Reason + " ") +
                                "Selecciona tu EBOOT.elf descifrado (por ejemplo, el de tu carpeta " +
                                "game\\PS3_GAME\\USRDIR) para instalar el juego.");
                        }
                    }
                }
                log.AppendLine("game data copied to " + gameDir);
            }
            finally
            {
                Iso.Unmount(o.Iso, already);
            }

            Status(97, "Creando accesos directos...");
            string launcher = Path.Combine(o.Dir, "BakuganLauncher.exe");
            string uninstaller = Path.Combine(o.Dir, "Uninstall.exe");
            if (o.Desktop) Shortcut.Create(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                           launcher, o.Dir);
            string menuDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs),
                                          "Bakugan Battle Brawlers");
            if (o.StartMenu)
            {
                Shortcut.Create(menuDir, launcher, o.Dir);
                if (File.Exists(uninstaller))
                    Shortcut.Create(menuDir, uninstaller, o.Dir, "Uninstall Bakugan",
                                    "Uninstall Bakugan: Battle Brawlers");
            }

            try
            {
                using (RegistryKey k = Registry.CurrentUser.CreateSubKey(
                           @"Software\Microsoft\Windows\CurrentVersion\Uninstall\BakuganRecomp"))
                {
                    if (k != null)
                    {
                        k.SetValue("DisplayName", "Bakugan: Battle Brawlers");
                        k.SetValue("DisplayVersion", "1.0");
                        k.SetValue("Publisher", "Bakugan Recomp");
                        k.SetValue("InstallLocation", o.Dir);
                        k.SetValue("DisplayIcon", launcher);
                        k.SetValue("UninstallString", "\"" + uninstaller + "\"");
                        k.SetValue("NoModify", 1, RegistryValueKind.DWord);
                        k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                    }
                }
            }
            catch { /* a locked registry must not fail the installation */ }

            Status(100, "Instalación completada.");
            log.AppendLine("desktop=" + o.Desktop + " startmenu=" + o.StartMenu);
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
            {
                string target = Path.Combine(dst, Path.GetFileName(file));
                long length = new FileInfo(file).Length;
                using (var input = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read))
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
                            if (pct > toPercent) pct = toPercent;
                            Status(pct, "Copiando " + Path.GetFileName(file) + "...");
                        }
                    }
                }
                if (length == 0) Status(fromPercent, "Copiando " + Path.GetFileName(file) + "...");
            }
            foreach (var dir in Directory.GetDirectories(src))
                CopyTree(dir, Path.Combine(dst, Path.GetFileName(dir)), fromPercent, toPercent);
        }

        private static void Status(int percent, string text)
        {
            if (report != null) report(percent, text);
        }
    }

    internal static class Shortcut
    {
        public static void Create(string folder, string target, string workDir,
                                  string name = "Bakugan Battle Brawlers",
                                  string description = "Bakugan: Battle Brawlers")
        {
            Directory.CreateDirectory(folder);
            string link = Path.Combine(folder, name + ".lnk");
            string command =
                "$w = New-Object -ComObject WScript.Shell; " +
                "$s = $w.CreateShortcut('" + link.Replace("'", "''") + "'); " +
                "$s.TargetPath = '" + target.Replace("'", "''") + "'; " +
                "$s.WorkingDirectory = '" + workDir.Replace("'", "''") + "'; " +
                "$s.IconLocation = '" + target.Replace("'", "''") + ",0'; " +
                "$s.Description = '" + description.Replace("'", "''") + "'; " +
                "$s.Save()";
            Iso.RunPowerShell(command);
        }
    }

    internal sealed class InstallerForm : Form
    {
        private readonly TextBox iso = new TextBox();
        private readonly TextBox dir = new TextBox();
        private readonly TextBox eboot = new TextBox();
        private readonly CheckBox desktop = new CheckBox();
        private readonly CheckBox startMenu = new CheckBox();
        private readonly Button install = new Button();
        private readonly Button browseIso = new Button();
        private readonly Button browseDir = new Button();
        private readonly Button browseEboot = new Button();
        private readonly ProgressBar bar = new ProgressBar();
        private readonly Label status = new Label();
        private readonly BackgroundWorker worker = new BackgroundWorker();
        private readonly StringBuilder log = new StringBuilder();

        public InstallerForm()
        {
            Text = "Bakugan: Battle Brawlers - Instalador";
            ClientSize = new Size(560, 370);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9f);

            var l1 = new Label { Text = "Imagen ISO del juego:", Location = new Point(18, 18), AutoSize = true };
            iso.Location = new Point(18, 40); iso.Width = 430;
            browseIso.Text = "Examinar..."; browseIso.Location = new Point(456, 38); browseIso.Width = 86;
            browseIso.Click += delegate { BrowseIso(); };

            var l2 = new Label { Text = "Carpeta de instalación:", Location = new Point(18, 82), AutoSize = true };
            dir.Location = new Point(18, 104); dir.Width = 430;
            dir.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                    "Programs", "BakuganRecomp");
            browseDir.Text = "Examinar..."; browseDir.Location = new Point(456, 102); browseDir.Width = 86;
            browseDir.Click += delegate { BrowseDir(); };

            var l3 = new Label { Text = "EBOOT.elf de respaldo (sólo si la ISO no se puede extraer):", Location = new Point(18, 146), AutoSize = true };
            eboot.Location = new Point(18, 168); eboot.Width = 430;
            string guess = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath),
                                        "game", "PS3_GAME", "USRDIR", "EBOOT.elf");
            if (File.Exists(guess)) eboot.Text = guess;
            browseEboot.Text = "Examinar..."; browseEboot.Location = new Point(456, 166); browseEboot.Width = 86;
            browseEboot.Click += delegate { BrowseEboot(); };

            desktop.Text = "Crear icono en el escritorio";
            desktop.Location = new Point(18, 210); desktop.AutoSize = true; desktop.Checked = true;
            startMenu.Text = "Añadir al menú Inicio";
            startMenu.Location = new Point(18, 234); startMenu.AutoSize = true; startMenu.Checked = true;

            install.Text = "Instalar"; install.Location = new Point(388, 312); install.Width = 154; install.Height = 30;
            install.Click += delegate { BeginInstall(); };

            bar.Location = new Point(18, 276); bar.Width = 524; bar.Height = 18;
            status.Location = new Point(18, 296); status.AutoSize = true; status.Text = "Listo.";

            Controls.AddRange(new Control[] { l1, iso, browseIso, l2, dir, browseDir, l3, eboot, browseEboot,
                                              desktop, startMenu, bar, status, install });

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
                    status.Text = "Error: " + e.Error.Message;
                    MessageBox.Show(this, e.Error.Message, "Instalación fallida", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                else
                {
                    bar.Value = 100;
                    status.Text = "Instalación completada.";
                    MessageBox.Show(this, "Bakugan: Battle Brawlers se ha instalado correctamente.", "Instalación completada",
                                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            };
        }

        private void BrowseIso()
        {
            using (var d = new OpenFileDialog())
            {
                d.Filter = "Imagen ISO (*.iso)|*.iso|Todos los archivos (*.*)|*.*";
                if (d.ShowDialog(this) == DialogResult.OK) iso.Text = d.FileName;
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

        private void BrowseEboot()
        {
            using (var d = new OpenFileDialog())
            {
                d.Filter = "EBOOT descifrado (EBOOT.elf)|EBOOT.elf|Todos los archivos (*.*)|*.*";
                if (d.ShowDialog(this) == DialogResult.OK) eboot.Text = d.FileName;
            }
        }

        private void BeginInstall()
        {
            if (!File.Exists(iso.Text))
            {
                MessageBox.Show(this, "Selecciona un archivo ISO válido.", "Falta la ISO", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (string.IsNullOrEmpty(dir.Text))
            {
                MessageBox.Show(this, "Selecciona la carpeta de instalación.", "Falta la carpeta", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            install.Enabled = false;
            bar.Value = 0;
            worker.RunWorkerAsync(new Options
            {
                Iso = iso.Text,
                Dir = dir.Text,
                Eboot = eboot.Text,
                Desktop = desktop.Checked,
                StartMenu = startMenu.Checked
            });
        }
    }
}
