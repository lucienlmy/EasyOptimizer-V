using System;
using System.Collections.Generic;
using System.Drawing;

namespace EasyOptimizerV
{
    public class TextureInfo
    {
        public string Name { get; set; } = "";
        public uint NameHash { get; set; }
        public int Width { get; set; }
        public int Height { get; set; }
        public string Format { get; set; } = "";
        public int Levels { get; set; }
        public int Stride { get; set; }
        public int DataSize { get; set; }
    }

    public class YtdHandle
    {
        public string Id { get; set; } = "";
        public string Name { get; set; } = "";
        public string FilePath { get; set; } = "";
        public string FileType { get; set; } = "YTD";
        public List<TextureInfo> Textures { get; set; } = new List<TextureInfo>();
        public object? NativeObject { get; set; }
    }

    public interface IYtdBackend
    {
        string BackendName { get; }
        YtdHandle LoadYtd(string filePath);
        YtdHandle? LoadWtd(string filePath);
        byte[] SaveYtd(YtdHandle handle);
        byte[]? GetPixels(YtdHandle handle, string textureName, int mipLevel);
        byte[]? GetTextureData(YtdHandle handle, string textureName);
        void UpdateTexture(YtdHandle handle, string textureName, int newWidth, int newHeight, string format, int levels, int stride, byte[] fullData);
        void RemoveTexture(YtdHandle handle, string textureName);
        YtdHandle CreateYtd(string name, List<(string name, uint nameHash, int width, int height, string format, int levels, int stride, byte[] data)> textures);
        uint ComputeNameHash(string name);
    }
}
