using System;
using System.IO;
using System.IO.Compression;

namespace BakuganInstaller
{
    /// <summary>
    /// Extract a bootable ELF from an already-decrypted EBOOT.BIN.
    ///
    /// A retail SELF stores an ELF header but encrypts the segment payload. A
    /// decrypted SELF (debug fSELF key revision 0x8000, or a retail SELF whose
    /// segments were already decrypted by the user's own tool) stores plain or
    /// zlib-deflated segment bytes. We never attempt key-based decryption; when
    /// the payload still looks encrypted the caller must supply EBOOT.elf.
    /// </summary>
    internal static class SelfExtract
    {
        private const uint SelfMagic = 0x53434500;
        private const uint ElfMagic = 0x7F454C46;
        private const ushort DebugKeyRev = 0x8000;
        private const uint ShTypeNobits = 8;
        private const uint ShFlagAlloc = 2;

        public static bool TryExtract(string binPath, string outPath, out string reason)
        {
            reason = "";
            byte[] d = File.ReadAllBytes(binPath);
            if (d.Length < 0x70) { reason = "EBOOT.BIN demasiado pequeño"; return false; }
            uint magic = U32(d, 0);
            if (magic == ElfMagic)
            {
                File.Copy(binPath, outPath, true);
                reason = "EBOOT.BIN ya era un ELF";
                return true;
            }
            if (magic != SelfMagic) { reason = "EBOOT.BIN no es SELF ni ELF"; return false; }

            ushort keyRev = U16(d, 8);
            ushort hdrType = U16(d, 10);
            if (hdrType != 1) { reason = "La cabecera SCE no es un SELF"; return false; }
            ulong elfSize = U64(d, 0x18);
            ulong elfOff = U64(d, 0x30);
            ulong phdrOff = U64(d, 0x38);
            ulong shdrOff = U64(d, 0x40);
            ulong segInfoOff = U64(d, 0x48);
            if (elfOff + 0x40 > (ulong)d.Length || U32(d, (int)elfOff) != ElfMagic)
            { reason = "El SELF no contiene una cabecera ELF válida"; return false; }
            if (elfSize == 0 || elfSize > 512UL * 1024 * 1024)
            { reason = "Tamaño ELF no válido en el SELF"; return false; }

            ulong ePhOff = U64(d, (int)elfOff + 0x20);
            ulong eShOff = U64(d, (int)elfOff + 0x28);
            ushort ePhentsize = U16(d, (int)elfOff + 0x36);
            ushort ePhnum = U16(d, (int)elfOff + 0x38);
            ushort eShentsize = U16(d, (int)elfOff + 0x3A);
            ushort eShnum = U16(d, (int)elfOff + 0x3C);
            if (ePhnum == 0 || ePhentsize < 0x38 || phdrOff + (ulong)ePhnum * ePhentsize > (ulong)d.Length)
            { reason = "Tabla de segmentos no válida"; return false; }

            /* Retail key revision: only proceed if the segment bytes no longer
             * look like ciphertext. Code/data entropy is well below 7.5. */
            if (keyRev != DebugKeyRev)
            {
                if (!SegmentsLookPlain(d, phdrOff, segInfoOff, ePhnum))
                {
                    reason = "La ISO no está descifrada: EBOOT.BIN es un SELF retail cifrado. " +
                             "Selecciona EBOOT.elf o descífralo con tus propias claves.";
                    return false;
                }
            }

            byte[] elf = new byte[(int)elfSize];
            Array.Copy(d, (int)elfOff, elf, 0, 0x40);
            int phBytes = ePhnum * ePhentsize;
            if (ePhOff + (ulong)phBytes > (ulong)elf.Length)
            { reason = "Los program headers no caben en el ELF"; return false; }
            Array.Copy(d, (int)phdrOff, elf, (int)ePhOff, phBytes);

            for (int i = 0; i < ePhnum; i++)
            {
                int ph = (int)phdrOff + i * ePhentsize;
                ulong pOffset = U64(d, ph + 8);
                ulong pFilesz = U64(d, ph + 32);
                int si = (int)segInfoOff + i * 0x20;
                if (si + 0x20 > d.Length) { reason = "Segment info fuera del archivo"; return false; }
                ulong fOff = U64(d, si);
                ulong fSize = U64(d, si + 8);
                uint comp = U32(d, si + 16);
                if (fOff == 0 || fSize == 0) continue;
                if (fOff + fSize > (ulong)d.Length) { reason = "Segmento fuera del archivo"; return false; }
                byte[] blob = new byte[(int)fSize];
                Array.Copy(d, (int)fOff, blob, 0, (int)fSize);
                if (comp == 2)
                {
                    try { blob = InflateZlib(blob); }
                    catch (Exception ex) { reason = "Segmento comprimido inválido: " + ex.Message; return false; }
                }
                if ((ulong)blob.Length != pFilesz)
                { reason = "Segmento " + i + ": tamaño " + blob.Length + " != " + pFilesz; return false; }
                if (pOffset + pFilesz > (ulong)elf.Length)
                { reason = "Segmento " + i + " fuera del ELF"; return false; }
                Array.Copy(blob, 0, elf, (int)pOffset, blob.Length);
            }

            /* Non-allocated sections (symtab/strtab/DWARF) sit in the SELF tail
             * at a constant delta from the original section offsets. Not needed
             * to boot, but keeps the generated ELF faithful for the recompiler. */
            if (eShnum > 0 && eShentsize >= 0x40 && shdrOff + (ulong)eShnum * eShentsize <= (ulong)d.Length)
            {
                long delta = (long)shdrOff - (long)eShOff;
                for (int i = 0; i < eShnum; i++)
                {
                    int sh = (int)shdrOff + i * eShentsize;
                    uint type = U32(d, sh + 4);
                    ulong flags = U64(d, sh + 8);
                    ulong off = U64(d, sh + 24);
                    ulong size = U64(d, sh + 32);
                    if (type == ShTypeNobits || size == 0 || (flags & ShFlagAlloc) != 0) continue;
                    long src = (long)off + delta;
                    if (src < 0 || (ulong)src + size > (ulong)d.Length) continue;
                    if (off + size > (ulong)elf.Length) continue;
                    Array.Copy(d, (int)src, elf, (int)off, (int)size);
                }
                ulong shBytes = (ulong)eShnum * eShentsize;
                if (eShOff + shBytes <= (ulong)elf.Length)
                    Array.Copy(d, (int)shdrOff, elf, (int)eShOff, (int)shBytes);
            }

            File.WriteAllBytes(outPath, elf);
            reason = keyRev == DebugKeyRev ? "fSELF de debug extraído" : "SELF descifrado extraído";
            return true;
        }

        private static bool SegmentsLookPlain(byte[] d, ulong phdrOff, ulong segInfoOff, int phnum)
        {
            for (int i = 0; i < phnum; i++)
            {
                int si = (int)segInfoOff + i * 0x20;
                if (si + 0x20 > d.Length) return false;
                ulong fOff = U64(d, si);
                ulong fSize = U64(d, si + 8);
                uint comp = U32(d, si + 16);
                if (fOff == 0 || fSize < 4096 || fOff + fSize > (ulong)d.Length) continue;
                byte[] blob = new byte[(int)fSize];
                Array.Copy(d, (int)fOff, blob, 0, (int)fSize);
                if (comp == 2)
                {
                    try { blob = InflateZlib(blob); }
                    catch { return false; }
                }
                int sample = Math.Min(65536, blob.Length);
                return Entropy(blob, 0, sample) < 7.5;
            }
            return false;
        }

        private static double Entropy(byte[] d, int off, int length)
        {
            if (off < 0 || off >= d.Length) return 8.0;
            if (length > d.Length - off) length = d.Length - off;
            if (length <= 0) return 8.0;
            int[] counts = new int[256];
            for (int i = 0; i < length; i++) counts[d[off + i]]++;
            double h = 0.0;
            for (int i = 0; i < 256; i++)
            {
                if (counts[i] == 0) continue;
                double p = (double)counts[i] / length;
                h -= p * Math.Log(p, 2.0);
            }
            return h;
        }

        private static byte[] InflateZlib(byte[] data)
        {
            if (data.Length < 2 || (data[0] & 0x0F) != 8) throw new InvalidDataException("no zlib");
            int start = 2;
            if ((data[1] & 0x20) != 0) start += 4;
            using (var input = new MemoryStream(data, start, data.Length - start))
            using (var ds = new DeflateStream(input, CompressionMode.Decompress))
            using (var output = new MemoryStream())
            {
                ds.CopyTo(output);
                return output.ToArray();
            }
        }

        private static ushort U16(byte[] d, int o) { return (ushort)((d[o] << 8) | d[o + 1]); }
        private static uint U32(byte[] d, int o) { return ((uint)d[o] << 24) | ((uint)d[o + 1] << 16) | ((uint)d[o + 2] << 8) | d[o + 3]; }
        private static ulong U64(byte[] d, int o) { return ((ulong)U32(d, o) << 32) | U32(d, o + 4); }
    }
}
