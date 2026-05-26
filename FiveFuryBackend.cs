using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.Json;

namespace EasyOptimizerV
{
    public class FiveFuryBackend : IYtdBackend
    {
        public string BackendName => "FiveFury";

        private readonly string _cliPath;
        private readonly Dictionary<string, string> _handlePaths = new();
        private readonly Dictionary<string, List<TextureInfo>> _handleTextures = new();
        private readonly Dictionary<string, Dictionary<string, byte[]>> _modifiedData = new();

        public FiveFuryBackend()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            _cliPath = Path.Combine(baseDir, "libs", "fivefury-cli.exe");
            if (!File.Exists(_cliPath))
            {
                _cliPath = Path.Combine(baseDir, "fivefury-cli.exe");
            }
            if (!File.Exists(_cliPath))
            {
                throw new FileNotFoundException($"fivefury-cli.exe not found in {baseDir} or libs/");
            }
        }

        public FiveFuryBackend(string cliPath)
        {
            _cliPath = cliPath;
            if (!File.Exists(_cliPath))
                throw new FileNotFoundException($"fivefury-cli.exe not found at {cliPath}");
        }

        public YtdHandle LoadYtd(string filePath)
        {
            var result = RunCli("info", filePath);
            var doc = JsonDocument.Parse(result);
            var root = doc.RootElement;

            string id = Guid.NewGuid().ToString();
            _handlePaths[id] = filePath;

            var handle = new YtdHandle
            {
                Id = id,
                Name = Path.GetFileName(filePath),
                FilePath = filePath,
                FileType = "YTD",
            };

            var textures = new List<TextureInfo>();
            foreach (var tex in root.GetProperty("textures").EnumerateArray())
            {
                textures.Add(new TextureInfo
                {
                    Name = tex.GetProperty("name").GetString() ?? "",
                    NameHash = tex.GetProperty("nameHash").GetUInt32(),
                    Width = tex.GetProperty("width").GetInt32(),
                    Height = tex.GetProperty("height").GetInt32(),
                    Format = tex.GetProperty("format").GetString() ?? "",
                    Levels = tex.GetProperty("levels").GetInt32(),
                    Stride = tex.GetProperty("stride").GetInt32(),
                    DataSize = tex.GetProperty("dataSize").GetInt32(),
                });
            }
            handle.Textures = textures;
            _handleTextures[id] = textures;
            _modifiedData[id] = new Dictionary<string, byte[]>();
            return handle;
        }

        public YtdHandle? LoadWtd(string filePath)
        {
            return null;
        }

        public byte[] SaveYtd(YtdHandle handle)
        {
            if (!_handlePaths.TryGetValue(handle.Id, out string? originalPath))
                throw new InvalidOperationException("Handle not found");

            if (_modifiedData.TryGetValue(handle.Id, out var mods) && mods.Count > 0)
            {
                string tempDir = Path.Combine(Path.GetTempPath(), "fivefury_save_" + handle.Id);
                Directory.CreateDirectory(tempDir);
                try
                {
                    var descriptor = new
                    {
                        game = "gta5",
                        textures = BuildTextureDescriptors(handle, tempDir)
                    };
                    string descPath = Path.Combine(tempDir, "descriptor.json");
                    string outPath = Path.Combine(tempDir, "output.ytd");
                    File.WriteAllText(descPath, JsonSerializer.Serialize(descriptor));
                    RunCli("save", descPath, outPath);
                    return File.ReadAllBytes(outPath);
                }
                finally
                {
                    try { Directory.Delete(tempDir, true); } catch { }
                }
            }
            else
            {
                return File.ReadAllBytes(originalPath);
            }
        }

        public byte[]? GetPixels(YtdHandle handle, string textureName, int mipLevel)
        {
            if (!_handlePaths.TryGetValue(handle.Id, out string? filePath))
                return null;

            string tempFile = Path.GetTempFileName();
            try
            {
                RunCli("decode", filePath, textureName, tempFile);
                return File.ReadAllBytes(tempFile);
            }
            catch
            {
                return null;
            }
            finally
            {
                try { File.Delete(tempFile); } catch { }
            }
        }

        public byte[]? GetTextureData(YtdHandle handle, string textureName)
        {
            if (_modifiedData.TryGetValue(handle.Id, out var mods) && mods.TryGetValue(textureName, out byte[]? data))
                return data;

            if (!_handlePaths.TryGetValue(handle.Id, out string? filePath))
                return null;

            string tempFile = Path.GetTempFileName();
            try
            {
                RunCli("get-data", filePath, textureName, tempFile);
                return File.ReadAllBytes(tempFile);
            }
            catch
            {
                return null;
            }
            finally
            {
                try { File.Delete(tempFile); } catch { }
            }
        }

        public void UpdateTexture(YtdHandle handle, string textureName, int newWidth, int newHeight, string format, int levels, int stride, byte[] fullData)
        {
            if (!_modifiedData.ContainsKey(handle.Id))
                _modifiedData[handle.Id] = new Dictionary<string, byte[]>();
            _modifiedData[handle.Id][textureName] = fullData;

            var texInfo = handle.Textures.Find(t => string.Equals(t.Name, textureName, StringComparison.OrdinalIgnoreCase));
            if (texInfo != null)
            {
                texInfo.Width = newWidth;
                texInfo.Height = newHeight;
                texInfo.Format = format;
                texInfo.Levels = levels;
                texInfo.Stride = stride;
                texInfo.DataSize = fullData.Length;
            }
        }

        public void RemoveTexture(YtdHandle handle, string textureName)
        {
            handle.Textures.RemoveAll(t => string.Equals(t.Name, textureName, StringComparison.OrdinalIgnoreCase));
            if (_modifiedData.TryGetValue(handle.Id, out var mods))
                mods.Remove(textureName);
        }

        public YtdHandle CreateYtd(string name, List<(string name, uint nameHash, int width, int height, string format, int levels, int stride, byte[] data)> textures)
        {
            string id = Guid.NewGuid().ToString();
            var handle = new YtdHandle
            {
                Id = id,
                Name = name,
                FileType = "YTD",
            };

            _modifiedData[id] = new Dictionary<string, byte[]>();
            foreach (var entry in textures)
            {
                handle.Textures.Add(new TextureInfo
                {
                    Name = entry.name,
                    NameHash = entry.nameHash,
                    Width = entry.width,
                    Height = entry.height,
                    Format = entry.format,
                    Levels = entry.levels,
                    Stride = entry.stride,
                    DataSize = entry.data.Length,
                });
                _modifiedData[id][entry.name] = entry.data;
            }
            _handleTextures[id] = handle.Textures;
            return handle;
        }

        public uint ComputeNameHash(string name)
        {
            var result = RunCli("hash", name.ToLowerInvariant());
            var doc = JsonDocument.Parse(result);
            return doc.RootElement.GetProperty("hash").GetUInt32();
        }

        private List<object> BuildTextureDescriptors(YtdHandle handle, string tempDir)
        {
            var list = new List<object>();
            foreach (var tex in handle.Textures)
            {
                string dataFile = Path.Combine(tempDir, tex.Name + ".bin");
                byte[]? data = null;

                if (_modifiedData.TryGetValue(handle.Id, out var mods) && mods.TryGetValue(tex.Name, out data))
                {
                    File.WriteAllBytes(dataFile, data);
                }
                else if (_handlePaths.TryGetValue(handle.Id, out string? filePath))
                {
                    RunCli("get-data", filePath, tex.Name, dataFile);
                }

                list.Add(new
                {
                    name = tex.Name,
                    width = tex.Width,
                    height = tex.Height,
                    format = tex.Format,
                    levels = tex.Levels,
                    dataFile = dataFile,
                });
            }
            return list;
        }

        private string RunCli(params string[] args)
        {
            var psi = new ProcessStartInfo
            {
                FileName = _cliPath,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            foreach (var arg in args)
                psi.ArgumentList.Add(arg);

            using var process = Process.Start(psi)!;
            string stdout = process.StandardOutput.ReadToEnd();
            string stderr = process.StandardError.ReadToEnd();
            process.WaitForExit();

            if (process.ExitCode != 0)
            {
                string errorMsg = stderr;
                try
                {
                    var errDoc = JsonDocument.Parse(stderr);
                    errorMsg = errDoc.RootElement.GetProperty("error").GetString() ?? stderr;
                }
                catch { }
                throw new Exception($"fivefury-cli error: {errorMsg}");
            }
            return stdout.Trim();
        }
    }
}
