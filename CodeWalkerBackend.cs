using CodeWalker.GameFiles;
using CodeWalker.Utils;
using System;
using System.Collections.Generic;
using System.IO;

namespace EasyOptimizerV
{
    public class CodeWalkerBackend : IYtdBackend
    {
        public string BackendName => "CodeWalker";

        private readonly Dictionary<string, YtdFile> _ytdFiles = new();
        private readonly Dictionary<string, WtdFile> _wtdFiles = new();

        public YtdHandle LoadYtd(string filePath)
        {
            byte[] data = File.ReadAllBytes(filePath);
            YtdFile ytd = new YtdFile();
            ytd.Load(data);
            ytd.Name = Path.GetFileName(filePath);

            string id = Guid.NewGuid().ToString();
            _ytdFiles[id] = ytd;

            var handle = new YtdHandle
            {
                Id = id,
                Name = ytd.Name,
                FilePath = filePath,
                FileType = "YTD",
                NativeObject = ytd,
            };
            PopulateTextures(handle, ytd);
            return handle;
        }

        public YtdHandle? LoadWtd(string filePath)
        {
            WtdFile wtd = WtdFile.Load(filePath);
            YtdFile ytd = wtd.AsYtd;

            string id = Guid.NewGuid().ToString();
            _ytdFiles[id] = ytd;
            _wtdFiles[id] = wtd;

            var handle = new YtdHandle
            {
                Id = id,
                Name = ytd.Name,
                FilePath = filePath,
                FileType = "WTD",
                NativeObject = ytd,
            };
            PopulateTextures(handle, ytd);
            return handle;
        }

        public byte[] SaveYtd(YtdHandle handle)
        {
            if (_wtdFiles.TryGetValue(handle.Id, out WtdFile? wtd))
                return wtd.Save();
            if (_ytdFiles.TryGetValue(handle.Id, out YtdFile? ytd))
                return ytd.Save();
            throw new InvalidOperationException("Handle not found");
        }

        public byte[]? GetPixels(YtdHandle handle, string textureName, int mipLevel)
        {
            var tex = FindTexture(handle, textureName);
            if (tex == null) return null;
            return DDSIO.GetPixels(tex, mipLevel);
        }

        public byte[]? GetTextureData(YtdHandle handle, string textureName)
        {
            var tex = FindTexture(handle, textureName);
            return tex?.Data?.FullData;
        }

        public void UpdateTexture(YtdHandle handle, string textureName, int newWidth, int newHeight, string format, int levels, int stride, byte[] fullData)
        {
            var tex = FindTexture(handle, textureName);
            if (tex == null) throw new InvalidOperationException($"Texture '{textureName}' not found");

            tex.Width = (ushort)newWidth;
            tex.Height = (ushort)newHeight;
            tex.Format = Mapping.ParseFormat(format, tex.Format);
            tex.Levels = (byte)levels;
            tex.Stride = (ushort)stride;
            if (tex.Data == null) tex.Data = new TextureData();
            tex.Data.FullData = fullData;

            RefreshHandleTextures(handle);
        }

        public void RemoveTexture(YtdHandle handle, string textureName)
        {
            var ytd = GetYtd(handle);
            if (ytd?.TextureDict?.Textures?.data_items == null) return;

            var list = new List<Texture>(ytd.TextureDict.Textures.data_items);
            list.RemoveAll(t => string.Equals(t.Name, textureName, StringComparison.OrdinalIgnoreCase));
            ytd.TextureDict.Textures.data_items = list.ToArray();

            if (ytd.TextureDict.TextureNameHashes?.data_items != null)
            {
                uint hash = JenkHash.GenHash(textureName.ToLowerInvariant());
                var hlist = new List<uint>(ytd.TextureDict.TextureNameHashes.data_items);
                hlist.Remove(hash);
                ytd.TextureDict.TextureNameHashes.data_items = hlist.ToArray();
            }

            RefreshHandleTextures(handle);
        }

        public YtdHandle CreateYtd(string name, List<(string name, uint nameHash, int width, int height, string format, int levels, int stride, byte[] data)> textures)
        {
            YtdFile ytd = new YtdFile();
            ytd.Name = name;
            ytd.TextureDict = new TextureDictionary();
            ytd.TextureDict.Textures = new ResourcePointerList64<Texture>();
            ytd.TextureDict.TextureNameHashes = new ResourceSimpleList64_uint();

            var texList = new List<Texture>();
            var hashList = new List<uint>();

            foreach (var entry in textures)
            {
                var tex = new Texture();
                tex.Name = entry.name;
                tex.NameHash = entry.nameHash;
                tex.Width = (ushort)entry.width;
                tex.Height = (ushort)entry.height;
                tex.Format = Mapping.ParseFormat(entry.format, TextureFormat.D3DFMT_DXT5);
                tex.Levels = (byte)entry.levels;
                tex.Stride = (ushort)entry.stride;
                tex.Data = new TextureData();
                tex.Data.FullData = entry.data;
                texList.Add(tex);
                hashList.Add(entry.nameHash);
            }

            ytd.TextureDict.Textures.data_items = texList.ToArray();
            ytd.TextureDict.TextureNameHashes.data_items = hashList.ToArray();

            string id = Guid.NewGuid().ToString();
            _ytdFiles[id] = ytd;

            var handle = new YtdHandle
            {
                Id = id,
                Name = name,
                NativeObject = ytd,
            };
            PopulateTextures(handle, ytd);
            return handle;
        }

        public uint ComputeNameHash(string name)
        {
            return JenkHash.GenHash(name.ToLowerInvariant());
        }

        private YtdFile? GetYtd(YtdHandle handle)
        {
            _ytdFiles.TryGetValue(handle.Id, out var ytd);
            return ytd;
        }

        private Texture? FindTexture(YtdHandle handle, string name)
        {
            var ytd = GetYtd(handle);
            if (ytd?.TextureDict?.Textures?.data_items == null) return null;
            foreach (var tex in ytd.TextureDict.Textures.data_items)
            {
                if (string.Equals(tex.Name, name, StringComparison.OrdinalIgnoreCase))
                    return tex;
            }
            return null;
        }

        private void PopulateTextures(YtdHandle handle, YtdFile ytd)
        {
            handle.Textures.Clear();
            if (ytd.TextureDict?.Textures?.data_items == null) return;
            foreach (var tex in ytd.TextureDict.Textures.data_items)
            {
                handle.Textures.Add(new TextureInfo
                {
                    Name = tex.Name,
                    NameHash = tex.NameHash,
                    Width = tex.Width,
                    Height = tex.Height,
                    Format = tex.Format.ToString(),
                    Levels = tex.Levels,
                    Stride = tex.Stride,
                    DataSize = tex.Data?.FullData?.Length ?? 0,
                });
            }
        }

        private void RefreshHandleTextures(YtdHandle handle)
        {
            var ytd = GetYtd(handle);
            if (ytd != null) PopulateTextures(handle, ytd);
        }
    }
}
