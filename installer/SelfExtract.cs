using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

namespace BakuganInstaller
{
    internal static partial class SelfKeyVault
    {
        static partial void ResolveLocalKey(uint selfType, ushort keyRev, ref byte[] erk, ref byte[] riv);

        public static bool TryGetLocalKey(uint selfType, ushort keyRev, out byte[] erk, out byte[] riv)
        {
            erk = null;
            riv = null;
            ResolveLocalKey(selfType, keyRev, ref erk, ref riv);
            return erk != null && erk.Length == 32 && riv != null && riv.Length == 16;
        }
    }

    /// <summary>
    /// Extract a bootable ELF from an already-decrypted SELF/fSELF or decrypt a
    /// retail SELF natively via AES-256-CBC / AES-128-CTR / zlib when a local
    /// gitignored key_vault module is compiled in.
    /// </summary>
    internal static class SelfExtract
    {
        private const uint SelfMagic = 0x53434500;
        private const uint ElfMagic = 0x7F454C46;
        private const ushort DebugKeyRev = 0x8000;
        private const uint ShTypeNobits = 8;
        private const uint ShFlagAlloc = 2;
        private const uint MetaSecPhdr = 2;
        private const uint MetaCompressedZlib = 2;
        private const uint MetaEncryptedAesCtr = 3;
        private const ulong MaxElfSize = 512UL * 1024 * 1024;

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
            ulong metaOff = U32(d, 0x0C);
            ulong hdrLen = U64(d, 0x10);
            ulong elfSize = U64(d, 0x18);
            ulong appInfoOff = U64(d, 0x28);
            ulong elfOff = U64(d, 0x30);
            ulong phdrOff = U64(d, 0x38);
            ulong shdrOff = U64(d, 0x40);
            ulong segInfoOff = U64(d, 0x48);
            if (elfOff + 0x40 > (ulong)d.Length || U32(d, (int)elfOff) != ElfMagic)
            { reason = "El SELF no contiene una cabecera ELF válida"; return false; }
            if (elfSize == 0 || elfSize > MaxElfSize)
            { reason = "Tamaño ELF no válido en el SELF"; return false; }

            ulong ePhOff = U64(d, (int)elfOff + 0x20);
            ulong eShOff = U64(d, (int)elfOff + 0x28);
            ushort ePhentsize = U16(d, (int)elfOff + 0x36);
            ushort ePhnum = U16(d, (int)elfOff + 0x38);
            ushort eShentsize = U16(d, (int)elfOff + 0x3A);
            ushort eShnum = U16(d, (int)elfOff + 0x3C);
            if (ePhnum == 0 || ePhentsize < 0x38 || phdrOff + (ulong)ePhnum * ePhentsize > (ulong)d.Length)
            { reason = "Tabla de segmentos no válida"; return false; }

            if (keyRev == DebugKeyRev || SegmentsLookPlain(d, phdrOff, segInfoOff, ePhnum))
            {
                return ExtractUnencryptedSelf(d, outPath, keyRev, elfSize, elfOff, phdrOff, shdrOff, segInfoOff,
                                              ePhOff, eShOff, ePhentsize, ePhnum, eShentsize, eShnum, out reason);
            }

            uint selfType = 0;
            if (appInfoOff + 16UL <= (ulong)d.Length)
                selfType = U32(d, (int)appInfoOff + 12);

            return ExtractRetailEncryptedSelf(d, outPath, selfType, keyRev, metaOff, hdrLen, elfSize,
                                              elfOff, phdrOff, shdrOff, ePhOff, eShOff, ePhentsize, ePhnum,
                                              eShentsize, eShnum, out reason);
        }

        private static bool ExtractUnencryptedSelf(byte[] d, string outPath, ushort keyRev, ulong elfSize,
                                                   ulong elfOff, ulong phdrOff, ulong shdrOff, ulong segInfoOff,
                                                   ulong ePhOff, ulong eShOff, ushort ePhentsize, ushort ePhnum,
                                                   ushort eShentsize, ushort eShnum, out string reason)
        {
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
                if (comp == MetaCompressedZlib)
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
             * at a constant delta from the original section offsets. */
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

        private static bool ExtractRetailEncryptedSelf(byte[] d, string outPath, uint selfType, ushort keyRev,
                                                       ulong metaOff, ulong hdrLen, ulong elfSize, ulong elfOff,
                                                       ulong phdrOff, ulong shdrOff, ulong ePhOff, ulong eShOff,
                                                       ushort ePhentsize, ushort ePhnum, ushort eShentsize,
                                                       ushort eShnum, out string reason)
        {
            ulong metaInfoOff = metaOff + 0x20UL;
            ulong metaBodyOff = metaInfoOff + 0x40UL;
            if (metaInfoOff > (ulong)d.Length || metaBodyOff > hdrLen || hdrLen > (ulong)d.Length)
            {
                reason = "Cabecera de metadatos SELF fuera de rango";
                return false;
            }

            byte[] erk, riv;
            if (!SelfKeyVault.TryGetLocalKey(selfType, keyRev, out erk, out riv))
            {
                reason = Loc.T(
                    "EBOOT.BIN es un SELF retail cifrado (revisión 0x" + keyRev.ToString("X4") +
                    "). Proporciona un EBOOT.elf ya descifrado (--eboot) o compila con keys\\key_vault.cs.",
                    "EBOOT.BIN is an encrypted retail SELF (revision 0x" + keyRev.ToString("X4") +
                    "). Provide a decrypted EBOOT.elf (--eboot) or build with keys\\key_vault.cs.");
                return false;
            }

            byte[] metaInfo = AesCbcDecrypt(d, (int)metaInfoOff, 0x40, erk, riv);
            if (!IsValidMetadataInfo(metaInfo))
            {
                reason = Loc.T(
                    "La clave local no descifró correctamente la cabecera de metadatos de EBOOT.BIN.",
                    "The local key did not decrypt the EBOOT.BIN metadata header.");
                return false;
            }

            byte[] metaKey = Slice(metaInfo, 0x00, 16);
            byte[] metaIv = Slice(metaInfo, 0x20, 16);
            int metaBodyLen = (int)(hdrLen - metaBodyOff);
            byte[] metaBody = new byte[metaBodyLen];
            Array.Copy(d, (int)metaBodyOff, metaBody, 0, metaBodyLen);
            AesCtrTransform(metaBody, metaKey, metaIv);

            if (metaBody.Length < 0x20)
            {
                reason = "MetadataHeader demasiado pequeño";
                return false;
            }
            uint sectionCount = U32(metaBody, 0x0C);
            uint keyCount = U32(metaBody, 0x10);
            ulong sectionsBytes = (ulong)sectionCount * 0x30UL;
            ulong keysBytes = (ulong)keyCount * 0x10UL;
            if (0x20UL + sectionsBytes + keysBytes > (ulong)metaBody.Length)
            {
                reason = "Tabla de secciones/claves en metadatos fuera de rango";
                return false;
            }
            int keysBase = 0x20 + (int)sectionsBytes;

            ulong phBytes = (ulong)ePhnum * ePhentsize;
            ulong shBytes = eShnum > 0 && eShentsize >= 0x40 ? (ulong)eShnum * eShentsize : 0UL;
            ulong totalElfSize = elfSize;
            if (ePhOff + phBytes > totalElfSize) totalElfSize = ePhOff + phBytes;
            if (eShOff + shBytes > totalElfSize) totalElfSize = eShOff + shBytes;
            for (int i = 0; i < ePhnum; i++)
            {
                int ph = (int)phdrOff + i * ePhentsize;
                ulong pOffset = U64(d, ph + 8);
                ulong pFilesz = U64(d, ph + 32);
                if (pFilesz > 0 && pOffset + pFilesz > totalElfSize)
                    totalElfSize = pOffset + pFilesz;
            }
            if (totalElfSize < 0x40 || totalElfSize > MaxElfSize)
            {
                reason = "Tamaño final del ELF fuera de límites";
                return false;
            }

            byte[] elf = new byte[(int)totalElfSize];
            Array.Copy(d, (int)elfOff, elf, 0, 0x40);
            if (ePhOff + phBytes > (ulong)elf.Length)
            {
                reason = "Los program headers no caben en el ELF";
                return false;
            }
            Array.Copy(d, (int)phdrOff, elf, (int)ePhOff, (int)phBytes);

            for (uint i = 0; i < sectionCount; i++)
            {
                int ms = 0x20 + (int)i * 0x30;
                ulong dataOffset = U64(metaBody, ms + 0x00);
                ulong dataSize = U64(metaBody, ms + 0x08);
                uint secType = U32(metaBody, ms + 0x10);
                uint programIdx = U32(metaBody, ms + 0x14);
                uint encrypted = U32(metaBody, ms + 0x20);
                uint keyIdx = U32(metaBody, ms + 0x24);
                uint ivIdx = U32(metaBody, ms + 0x28);
                uint compressed = U32(metaBody, ms + 0x2C);

                if (secType != MetaSecPhdr || dataSize == 0)
                    continue;
                if (programIdx >= ePhnum)
                {
                    reason = "Índice de program header inválido en sección " + i;
                    return false;
                }
                int ph = (int)phdrOff + (int)programIdx * ePhentsize;
                ulong pOffset = U64(d, ph + 8);
                ulong pFilesz = U64(d, ph + 32);
                if (pFilesz == 0)
                    continue;
                if (dataSize > int.MaxValue || dataOffset + dataSize > (ulong)d.Length)
                {
                    reason = "Sección " + i + " fuera del SELF";
                    return false;
                }

                byte[] blob = new byte[(int)dataSize];
                Array.Copy(d, (int)dataOffset, blob, 0, (int)dataSize);

                if (encrypted == MetaEncryptedAesCtr)
                {
                    if (keyIdx >= keyCount || ivIdx >= keyCount)
                    {
                        reason = "Índice de clave/IV inválido en sección " + i;
                        return false;
                    }
                    byte[] segKey = Slice(metaBody, keysBase + (int)keyIdx * 16, 16);
                    byte[] segIv = Slice(metaBody, keysBase + (int)ivIdx * 16, 16);
                    AesCtrTransform(blob, segKey, segIv);
                }

                if (compressed == MetaCompressedZlib)
                {
                    try { blob = InflateZlib(blob); }
                    catch (Exception ex)
                    {
                        reason = "Error al descomprimir sección " + i + ": " + ex.Message;
                        return false;
                    }
                }

                if ((ulong)blob.Length < pFilesz)
                {
                    reason = "Sección " + i + " truncada: " + blob.Length + " < " + pFilesz;
                    return false;
                }
                if (pOffset + pFilesz > (ulong)elf.Length)
                {
                    reason = "Sección " + i + " fuera del ELF";
                    return false;
                }
                Array.Copy(blob, 0, elf, (int)pOffset, (int)pFilesz);
            }

            if (shBytes > 0 && shdrOff + shBytes <= (ulong)d.Length && eShOff + shBytes <= (ulong)elf.Length)
                Array.Copy(d, (int)shdrOff, elf, (int)eShOff, (int)shBytes);

            if (U32(elf, 0) != ElfMagic)
            {
                reason = "El archivo resultante no tiene cabecera ELF válida";
                return false;
            }

            File.WriteAllBytes(outPath, elf);
            reason = "SELF retail descifrado nativamente";
            return true;
        }

        private static bool IsValidMetadataInfo(byte[] info)
        {
            if (info == null || info.Length < 0x40) return false;
            for (int i = 0x10; i < 0x20; i++)
                if (info[i] != 0) return false;
            for (int i = 0x30; i < 0x40; i++)
                if (info[i] != 0) return false;
            return true;
        }

        private static byte[] AesCbcDecrypt(byte[] input, int offset, int length, byte[] key, byte[] iv)
        {
            using (Aes aes = Aes.Create())
            {
                aes.KeySize = key.Length * 8;
                aes.Mode = CipherMode.CBC;
                aes.Padding = PaddingMode.None;
                aes.Key = key;
                aes.IV = iv;
                using (ICryptoTransform dec = aes.CreateDecryptor())
                {
                    return dec.TransformFinalBlock(input, offset, length);
                }
            }
        }

        private static void AesCtrTransform(byte[] data, byte[] key, byte[] initialCounter)
        {
            byte[] counter = new byte[16];
            Array.Copy(initialCounter, 0, counter, 0, 16);
            byte[] keystream = new byte[16];

            using (Aes aes = Aes.Create())
            {
                aes.KeySize = key.Length * 8;
                aes.Mode = CipherMode.ECB;
                aes.Padding = PaddingMode.None;
                aes.Key = key;
                using (ICryptoTransform enc = aes.CreateEncryptor())
                {
                    for (int pos = 0; pos < data.Length; pos += 16)
                    {
                        enc.TransformBlock(counter, 0, 16, keystream, 0);
                        int take = Math.Min(16, data.Length - pos);
                        for (int b = 0; b < take; b++)
                            data[pos + b] ^= keystream[b];
                        for (int c = 15; c >= 0; c--)
                        {
                            if (++counter[c] != 0) break;
                        }
                    }
                }
            }
        }

        private static byte[] Slice(byte[] d, int off, int len)
        {
            byte[] s = new byte[len];
            Array.Copy(d, off, s, 0, len);
            return s;
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
                if (comp == MetaCompressedZlib)
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
