using System;
using System.Diagnostics;
using System.IO;

namespace BakuganInstaller
{
    /// <summary>
    /// Use the portable RPCS3 `--decrypt` command for ISOs whose EBOOT.BIN is a
    /// retail SELF. RPCS3 uses its own key database; the installer only invokes
    /// the user's local copy. No keys are shipped here.
    /// </summary>
    internal static class Rpcs3Decrypt
    {
        public static bool TryDecrypt(string binPath, string workDir, string rpcs3Dir,
                                      out string elfPath, out string reason)
        {
            elfPath = null;
            string exe = Path.Combine(rpcs3Dir, "rpcs3.exe");
            if (!File.Exists(exe))
            {
                reason = "No se encontró RPCS3 en " + rpcs3Dir;
                return false;
            }
            try
            {
                Directory.CreateDirectory(workDir);
                string input = Path.Combine(workDir, "EBOOT.BIN");
                string output = Path.Combine(workDir, "EBOOT.elf");
                File.Copy(binPath, input, true);
                if (File.Exists(output)) File.Delete(output);

                var psi = new ProcessStartInfo(exe, "--decrypt \"" + input + "\"");
                psi.WorkingDirectory = rpcs3Dir;
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                using (Process p = Process.Start(psi))
                {
                    if (p == null) { reason = "No se pudo iniciar RPCS3"; return false; }
                    if (!p.WaitForExit(180000))
                    {
                        try { p.Kill(); } catch { }
                        reason = "RPCS3 no terminó al descifrar";
                        return false;
                    }
                    if (p.ExitCode != 0)
                    {
                        reason = "RPCS3 --decrypt devolvió " + p.ExitCode;
                        return false;
                    }
                }
                if (!File.Exists(output))
                {
                    reason = "RPCS3 no generó EBOOT.elf";
                    return false;
                }
                using (var f = File.OpenRead(output))
                {
                    var head = new byte[4];
                    if (f.Read(head, 0, 4) != 4 || head[0] != 0x7F || head[1] != (byte)'E' ||
                        head[2] != (byte)'L' || head[3] != (byte)'F')
                    {
                        reason = "RPCS3 generó un archivo que no es ELF";
                        return false;
                    }
                }
                elfPath = output;
                reason = "RPCS3 --decrypt";
                return true;
            }
            catch (Exception ex)
            {
                reason = "RPCS3: " + ex.Message;
                return false;
            }
        }
    }
}
