using System;
using System.Runtime.InteropServices;

namespace EasyOptimizerV
{
    public enum TfBCFormat
    {
        BC1 = 0,
        BC3 = 1,
        BC4 = 2,
        BC5 = 3,
        BC7 = 4,
        A8R8G8B8 = 5,
        BC1A = 8,
    }

    public static class TexFuryNative
    {
        private const string DLL = "texfury_native";

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr tf_create_image(int width, int height, IntPtr rgba);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr tf_compress(IntPtr image, int format, int mipmaps, int min_mip_size, float quality, int filter);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr tf_compressed_data(IntPtr compressed);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr tf_compressed_size(IntPtr compressed);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tf_compressed_mip_count(IntPtr compressed);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr tf_decompress(IntPtr compressed, int mip, out int width, out int height);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern void tf_free_image(IntPtr image);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern void tf_free_compressed(IntPtr compressed);

        [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
        public static extern void tf_free_buffer(IntPtr buffer);
    }
}
