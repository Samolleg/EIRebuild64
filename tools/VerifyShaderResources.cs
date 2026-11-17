using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

// Compares already-compiled RCDATA bytes; it never invokes a shader compiler.
public static class EIShaderResourceVerifier
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr FindResourceExW(IntPtr module, IntPtr type, string name, ushort language);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool FreeLibrary(IntPtr module);

    public static int Verify(string path, string[] names, int[] languages, long[] sizes, string[] hashes)
    {
        if (names == null || names.Length == 0 || languages == null || sizes == null || hashes == null ||
            names.Length != languages.Length || names.Length != sizes.Length || names.Length != hashes.Length)
            throw new ArgumentException("Invalid compiled-resource manifest");
        IntPtr module = LoadLibraryExW(path, IntPtr.Zero, 2); // LOAD_LIBRARY_AS_DATAFILE
        if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            byte[] buffer = new byte[1024 * 1024];
            for (int i = 0; i < names.Length; ++i)
            {
                if (String.IsNullOrEmpty(names[i]) || languages[i] < 0 || languages[i] > UInt16.MaxValue ||
                    sizes[i] <= 0 || sizes[i] > Int32.MaxValue || String.IsNullOrEmpty(hashes[i]))
                    throw new ArgumentException("Invalid compiled-resource record");
                IntPtr info = FindResourceExW(module, new IntPtr(10), names[i], (ushort)languages[i]);
                if (info == IntPtr.Zero) throw new InvalidOperationException("Missing resource: " + names[i]);
                uint size = SizeofResource(module, info);
                if (size != sizes[i]) throw new InvalidOperationException("Resource size changed: " + names[i]);
                IntPtr bytes = LockResource(LoadResource(module, info));
                if (bytes == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                using (SHA256 hash = SHA256.Create())
                {
                    int offset = 0;
                    while (offset < size)
                    {
                        int count = Math.Min(buffer.Length, (int)size - offset);
                        Marshal.Copy(IntPtr.Add(bytes, offset), buffer, 0, count);
                        hash.TransformBlock(buffer, 0, count, buffer, 0);
                        offset += count;
                    }
                    hash.TransformFinalBlock(new byte[0], 0, 0);
                    string actual = BitConverter.ToString(hash.Hash).Replace("-", "").ToLowerInvariant();
                    if (!String.Equals(actual, hashes[i], StringComparison.Ordinal))
                        throw new InvalidOperationException("Resource bytes changed: " + names[i]);
                }
            }
            return names.Length;
        }
        finally { FreeLibrary(module); }
    }
}
