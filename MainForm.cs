using CodeWalker.GameFiles;
using CodeWalker.Utils;
using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using System.Security.Cryptography;
using BCnEncoder.Encoder;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;

namespace EasyOptimizerV
{
    public partial class MainForm : Form
    {
        private FlowLayoutPanel? flowLayoutPanel;
        private StatusStrip? statusStrip;
        private ToolStripStatusLabel? statusLabel;
        private MenuStrip? menuStrip;
        private ToolStrip? toolStrip;
        private System.Collections.Generic.List<YtdHandle> loadedHandles = new System.Collections.Generic.List<YtdHandle>();
        private TextBox? searchTextBox;
        private string currentSearch = "";
        private string currentPreviewRes = "128";
        private System.Collections.Generic.HashSet<string> expandedHandleIds = new System.Collections.Generic.HashSet<string>();
        private System.Collections.Generic.HashSet<string> expandedVirtualFolders = new System.Collections.Generic.HashSet<string>();
        private System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(TextureInfo, YtdHandle)>> duplicateGroups = new System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(TextureInfo, YtdHandle)>>();
        private bool showingDuplicates = false;
        private EncoderEngine selectedEngine = EncoderEngine.BCnEncoder;
        private ComboBox? encoderCombo;
        private IYtdBackend backend;
        private ComboBox? backendCombo;

        public MainForm()
        {
            InitializeComponent();
            backend = new CodeWalkerBackend();
            try
            {
                SetupUI();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Startup Error: {ex.Message}\n{ex.StackTrace}", "Critical Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void SetupUI()
        {
            this.Controls.Clear();
            this.Text = "EasyOptimizer-V";
            this.Size = new Size(1024, 768);
            Theme.Apply(this);
            this.ShowIcon = false;

            // Root Layout - TableLayout to guarantee no overlap
            TableLayoutPanel mainLayout = new TableLayoutPanel();
            mainLayout.Dock = DockStyle.Fill;
            mainLayout.ColumnCount = 1;
            mainLayout.RowCount = 2;
            mainLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 60F)); // Header
            mainLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F)); // Content
            mainLayout.Margin = new Padding(0);
            mainLayout.Padding = new Padding(0);
            mainLayout.BackColor = Theme.BackgroundDark;

            // HEADER (Row 0)
            TableLayoutPanel headerTable = new TableLayoutPanel();
            headerTable.Dock = DockStyle.Fill;
            headerTable.Margin = new Padding(0);
            headerTable.BackColor = Theme.SurfaceDark;
            headerTable.ColumnCount = 3;
            headerTable.RowCount = 1;

            headerTable.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize)); // Title
            headerTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F)); // Search

            headerTable.Padding = new Padding(16, 0, 16, 0);

            // 1. Title
            Label lblTitle = new Label();
            lblTitle.Text = "EasyOptimizer-V";
            lblTitle.Font = new Font("Segoe UI", 12F, FontStyle.Bold);
            lblTitle.ForeColor = Theme.TextPrimaryDark;
            lblTitle.AutoSize = true;
            lblTitle.Anchor = AnchorStyles.Left;
            lblTitle.Margin = new Padding(0, 0, 12, 0);
            headerTable.Controls.Add(lblTitle, 0, 0);

            // 2. Search Box
            Panel searchContainer = new Panel();
            searchContainer.Height = 36;
            searchContainer.BackColor = Theme.BackgroundDark;
            searchContainer.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            searchContainer.Margin = new Padding(0, 12, 0, 0);

            searchTextBox = new TextBox();
            searchTextBox.BorderStyle = BorderStyle.None;
            searchTextBox.BackColor = Theme.BackgroundDark;
            searchTextBox.ForeColor = Theme.TextPrimaryDark;
            searchTextBox.Font = new Font("Segoe UI", 10F);
            searchTextBox.PlaceholderText = "Search textures...";

            // Y=9 to center vertically (36 - TextHeight)/2
            searchTextBox.Location = new Point(12, 9);
            searchTextBox.Width = 200;
            searchTextBox.Anchor = AnchorStyles.Left | AnchorStyles.Right;
            searchTextBox.TextChanged += (s, e) => { SearchTextBox_TextChanged(s, e); };

            searchContainer.Controls.Add(searchTextBox);
            headerTable.Controls.Add(searchContainer, 1, 0);

            // Add Header to Root Layout (Row 0)
            mainLayout.Controls.Add(headerTable, 0, 0);

            // 1. Grid Size / Preview Button (Cycles through Small, Medium, Native)
            string[] gridNames = { "Small", "Medium", "Native" };
            string[] resValues = { "128", "256", "Native" };
            int currentSizeIndex = 1; // Start at Medium

            Button btnGridSize = new Button();
            btnGridSize.Text = $"Grid: {gridNames[currentSizeIndex]}";
            btnGridSize.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnGridSize.BackColor = Theme.Primary;
            btnGridSize.ForeColor = Color.White;
            btnGridSize.FlatStyle = FlatStyle.Flat;
            btnGridSize.FlatAppearance.BorderSize = 0;
            btnGridSize.Height = 36;
            btnGridSize.Margin = new Padding(0, 4, 0, 4);
            btnGridSize.Cursor = Cursors.Hand;

            btnGridSize.Click += (s, e) =>
            {
                currentSizeIndex = (currentSizeIndex + 1) % gridNames.Length;
                btnGridSize.Text = $"Grid: {gridNames[currentSizeIndex]}";
                currentPreviewRes = resValues[currentSizeIndex];

                Size newSize = new Size(220, 260);
                if (currentPreviewRes == "128") newSize = new Size(160, 200);
                else if (currentPreviewRes == "Native") newSize = new Size(300, 340);

                if (flowLayoutPanel != null)
                {
                    flowLayoutPanel.SuspendLayout();
                    foreach (Control c in flowLayoutPanel.Controls)
                    {
                        if (c is TextureCard tc) { tc.Size = newSize; tc.Invalidate(); }
                    }
                    flowLayoutPanel.ResumeLayout();
                }
                RenderTextures();
            };

            Button btnOpen = new Button();
            btnOpen.Text = "Import Files";
            btnOpen.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnOpen.BackColor = Theme.Primary;
            btnOpen.ForeColor = Color.White;
            btnOpen.FlatStyle = FlatStyle.Flat;
            btnOpen.FlatAppearance.BorderSize = 0;
            btnOpen.Height = 36;
            btnOpen.Margin = new Padding(0, 4, 0, 4);
            btnOpen.Cursor = Cursors.Hand;
            btnOpen.Click += (s, e) => OpenFile_Click(s, e);

            Button btnFolder = new Button();
            btnFolder.Text = "Import Folder";
            btnFolder.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnFolder.BackColor = Theme.Primary;
            btnFolder.ForeColor = Color.White;
            btnFolder.FlatStyle = FlatStyle.Flat;
            btnFolder.FlatAppearance.BorderSize = 0;
            btnFolder.Height = 36;
            btnFolder.Margin = new Padding(0, 4, 0, 4);
            btnFolder.Cursor = Cursors.Hand;
            btnFolder.Click += (s, e) => ImportFolder_Click();

            Button btnClear = new Button();
            btnClear.Text = "Clear All";
            btnClear.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnClear.BackColor = Color.FromArgb(180, 60, 60);
            btnClear.ForeColor = Color.White;
            btnClear.FlatStyle = FlatStyle.Flat;
            btnClear.FlatAppearance.BorderSize = 0;
            btnClear.Height = 36;
            btnClear.Margin = new Padding(0, 4, 0, 4);
            btnClear.Cursor = Cursors.Hand;
            btnClear.Click += (s, e) => ClearAll_Click();

            Button btnSaveAll = new Button();
            btnSaveAll.Text = "Save All";
            btnSaveAll.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnSaveAll.BackColor = Theme.Primary;
            btnSaveAll.ForeColor = Color.White;
            btnSaveAll.FlatStyle = FlatStyle.Flat;
            btnSaveAll.FlatAppearance.BorderSize = 0;
            btnSaveAll.Height = 36;
            btnSaveAll.Margin = new Padding(0, 4, 0, 4);
            btnSaveAll.Cursor = Cursors.Hand;
            btnSaveAll.Click += (s, e) => SaveAll_Click();

            Button btnNameDup = new Button();
            btnNameDup.Text = "Detect Names";
            btnNameDup.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnNameDup.BackColor = Color.FromArgb(70, 70, 70);
            btnNameDup.ForeColor = Color.White;
            btnNameDup.FlatStyle = FlatStyle.Flat;
            btnNameDup.FlatAppearance.BorderSize = 0;
            btnNameDup.Height = 36;
            btnNameDup.Width = 188;
            btnNameDup.Margin = new Padding(0, 4, 0, 4);
            btnNameDup.Cursor = Cursors.Hand;
            btnNameDup.Click += (s, e) => PerformDeDuplicationAnalysis(true, false);

            Button btnHexDup = new Button();
            btnHexDup.Text = "Detect Hex";
            btnHexDup.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnHexDup.BackColor = Color.FromArgb(70, 70, 70);
            btnHexDup.ForeColor = Color.White;
            btnHexDup.FlatStyle = FlatStyle.Flat;
            btnHexDup.FlatAppearance.BorderSize = 0;
            btnHexDup.Height = 36;
            btnHexDup.Width = 188;
            btnHexDup.Margin = new Padding(0, 4, 0, 4);
            btnHexDup.Cursor = Cursors.Hand;
            btnHexDup.Click += (s, e) => PerformDeDuplicationAnalysis(false, true);

            Button btnMigrate = new Button();
            btnMigrate.Text = "Migrate Duplicates";
            btnMigrate.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnMigrate.BackColor = Color.FromArgb(0, 122, 204);
            btnMigrate.ForeColor = Color.White;
            btnMigrate.FlatStyle = FlatStyle.Flat;
            btnMigrate.FlatAppearance.BorderSize = 0;
            btnMigrate.Height = 36;
            btnMigrate.Margin = new Padding(0, 4, 0, 4);
            btnMigrate.Cursor = Cursors.Hand;
            btnMigrate.Click += (s, e) => MigrateDuplicates_Click();

            Button btnSmartOpt = new Button();
            btnSmartOpt.Text = "Smart Optimize";
            btnSmartOpt.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnSmartOpt.BackColor = Color.FromArgb(154, 73, 222); // Purple
            btnSmartOpt.ForeColor = Color.White;
            btnSmartOpt.FlatStyle = FlatStyle.Flat;
            btnSmartOpt.FlatAppearance.BorderSize = 0;
            btnSmartOpt.Height = 36;
            btnSmartOpt.Margin = new Padding(0, 4, 0, 4);
            btnSmartOpt.Cursor = Cursors.Hand;
            btnSmartOpt.Click += (s, e) => SmartOptimize_Click();

            // CONTENT AREA (Row 1)
            TableLayoutPanel contentLayout = new TableLayoutPanel();
            contentLayout.Dock = DockStyle.Fill;
            contentLayout.ColumnCount = 2;
            contentLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 220F)); // Sidebar
            contentLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F)); // Flow Grid
            contentLayout.Margin = new Padding(0);

            // SIDEBAR
            FlowLayoutPanel sidebar = new FlowLayoutPanel();
            sidebar.Dock = DockStyle.Fill;
            sidebar.BackColor = Theme.SurfaceDark;
            sidebar.FlowDirection = FlowDirection.TopDown;
            sidebar.WrapContents = false;
            sidebar.Padding = new Padding(16);
            sidebar.AutoScroll = true;

            void AddSidebarLabel(string text)
            {
                Label lbl = new Label();
                lbl.Text = text;
                lbl.ForeColor = Theme.TextSecondaryDark;
                lbl.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
                lbl.Margin = new Padding(0, 8, 0, 4);
                lbl.AutoSize = true;
                sidebar.Controls.Add(lbl);
            }

            AddSidebarLabel("VIEW CONTROLS");
            sidebar.Controls.Add(btnGridSize);
            btnGridSize.Width = 188; // Fill sidebar width minus padding

            AddSidebarLabel("IMPORT");
            btnOpen.Width = 188;
            btnFolder.Width = 188;
            sidebar.Controls.Add(btnOpen);
            sidebar.Controls.Add(btnFolder);

            AddSidebarLabel("OPTIMIZATION");
            btnSaveAll.Width = 188;
            btnClear.Width = 188;
            sidebar.Controls.Add(btnSaveAll);
            sidebar.Controls.Add(btnClear);

            AddSidebarLabel("DUPLICATE ANALYSIS");
            btnMigrate.Width = 188;
            btnSmartOpt.Width = 188;
            sidebar.Controls.Add(btnNameDup);
            sidebar.Controls.Add(btnHexDup);
            sidebar.Controls.Add(btnMigrate);
            sidebar.Controls.Add(btnSmartOpt);

            AddSidebarLabel("ENCODER ENGINE");
            encoderCombo = new ComboBox();
            encoderCombo.Width = 188;
            encoderCombo.DataSource = Enum.GetValues(typeof(EncoderEngine));
            encoderCombo.SelectedItem = selectedEngine;
            encoderCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            encoderCombo.BackColor = Theme.BackgroundDark;
            encoderCombo.ForeColor = Color.White;
            encoderCombo.FlatStyle = FlatStyle.Flat;
            encoderCombo.SelectedIndexChanged += (s, e) =>
            {
                if (encoderCombo.SelectedItem is EncoderEngine engine)
                    selectedEngine = engine;
            };
            sidebar.Controls.Add(encoderCombo);

            AddSidebarLabel("DECODER ENGINE");
            backendCombo = new ComboBox();
            backendCombo.Width = 188;
            backendCombo.Items.AddRange(new object[] { "CodeWalker", "FiveFury" });
            backendCombo.SelectedItem = "CodeWalker";
            backendCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            backendCombo.BackColor = Theme.BackgroundDark;
            backendCombo.ForeColor = Color.White;
            backendCombo.FlatStyle = FlatStyle.Flat;
            backendCombo.SelectedIndexChanged += (s, e) =>
            {
                string? selected = backendCombo.SelectedItem?.ToString();
                if (selected == "FiveFury")
                {
                    try
                    {
                        backend = new FiveFuryBackend();
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Failed to initialize FiveFury backend:\n{ex.Message}\n\nPlace fivefury-cli.exe in the libs/ folder.", "Backend Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        backendCombo.SelectedItem = "CodeWalker";
                        backend = new CodeWalkerBackend();
                    }
                }
                else
                {
                    backend = new CodeWalkerBackend();
                }
            };
            sidebar.Controls.Add(backendCombo);

            contentLayout.Controls.Add(sidebar, 0, 0);

            // FLOW GRID (Vertical stack for Folders)
            flowLayoutPanel = new FlowLayoutPanel();
            flowLayoutPanel.Dock = DockStyle.Fill;
            flowLayoutPanel.AutoScroll = true;
            flowLayoutPanel.BackColor = Theme.BackgroundDark;
            flowLayoutPanel.FlowDirection = FlowDirection.TopDown;
            flowLayoutPanel.WrapContents = false;
            flowLayoutPanel.Padding = new Padding(16);

            // Force folders to 100% width on resize
            flowLayoutPanel.SizeChanged += (s, e) =>
            {
                if (flowLayoutPanel.ClientSize.Width <= 0) return;
                int targetWidth = flowLayoutPanel.ClientSize.Width - flowLayoutPanel.Padding.Horizontal - 25;
                flowLayoutPanel.SuspendLayout();
                foreach (Control c in flowLayoutPanel.Controls)
                {
                    if (c is YtdFolderCard folder) folder.Width = targetWidth;
                }
                flowLayoutPanel.ResumeLayout();
            };

            contentLayout.Controls.Add(flowLayoutPanel, 1, 0);

            mainLayout.Controls.Add(contentLayout, 0, 1);

            this.Controls.Add(mainLayout);

            // Status Strip (Hidden but initialized)
            statusStrip = new StatusStrip();
            statusStrip.Visible = false;
            statusLabel = new ToolStripStatusLabel();
            statusStrip.Items.Add(statusLabel);
            this.Controls.Add(statusStrip);
        }

        private void SearchTextBox_TextChanged(object? sender, EventArgs e)
        {
            currentSearch = searchTextBox?.Text?.ToLowerInvariant() ?? "";
            RenderTextures();
        }

        private void OpenFile_Click(object? sender, EventArgs e)
        {
            using (OpenFileDialog ofd = new OpenFileDialog())
            {
                ofd.Filter = "Texture Dicts (*.ytd;*.wtd)|*.ytd;*.wtd|YTD Files (*.ytd)|*.ytd|WTD Files (*.wtd)|*.wtd|All Files (*.*)|*.*";
                ofd.Multiselect = true;
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    foreach (string file in ofd.FileNames)
                    {
                        if (file.EndsWith(".wtd", StringComparison.OrdinalIgnoreCase))
                            AddWtd(file);
                        else
                            AddYtd(file);
                    }
                    RenderTextures();
                }
            }
        }

        private void ImportFolder_Click()
        {
            using (FolderBrowserDialog fbd = new FolderBrowserDialog())
            {
                fbd.Description = "Select folder to import YTD/WTD files recursively";
                fbd.UseDescriptionForTitle = true;
                if (fbd.ShowDialog() == DialogResult.OK)
                {
                    try
                    {
                        string[] ytdFiles = Directory.GetFiles(fbd.SelectedPath, "*.ytd", SearchOption.AllDirectories);
                        string[] wtdFiles = Directory.GetFiles(fbd.SelectedPath, "*.wtd", SearchOption.AllDirectories);
                        var allFiles = new System.Collections.Generic.List<string>(ytdFiles);
                        allFiles.AddRange(wtdFiles);

                        if (allFiles.Count == 0)
                        {
                            MessageBox.Show("No YTD or WTD files found in the selected folder.", "Import Folder", MessageBoxButtons.OK, MessageBoxIcon.Information);
                            return;
                        }

                        if (statusLabel != null) statusLabel.Text = $"Importing {allFiles.Count} files...";
                        Application.DoEvents();

                        foreach (string file in allFiles)
                        {
                            if (file.EndsWith(".wtd", StringComparison.OrdinalIgnoreCase))
                                AddWtd(file);
                            else
                                AddYtd(file);
                        }
                        RenderTextures();

                        MessageBox.Show($"Successfully imported {allFiles.Count} files ({ytdFiles.Length} YTD, {wtdFiles.Length} WTD).", "Import Folder", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Error importing folder: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                }
            }
        }

        private void ClearAll_Click()
        {
            loadedHandles.Clear();
            expandedHandleIds.Clear();
            expandedVirtualFolders.Clear();
            duplicateGroups.Clear();
            showingDuplicates = false;
            RenderTextures();
            if (statusLabel != null) statusLabel.Text = "Cleared all files.";
        }

        private void SaveAll_Click()
        {
            if (loadedHandles.Count == 0) return;

            var result = MessageBox.Show("Deseja sobrescrever os arquivos originais?\n\n'Sim' para Sobrescrever\n'Não' para Selecionar Nova Pasta\n'Cancelar' para Abortar", "Salvar Tudo", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);

            if (result == DialogResult.Cancel) return;

            string? targetFolder = null;
            if (result == DialogResult.No)
            {
                using (FolderBrowserDialog fbd = new FolderBrowserDialog())
                {
                    fbd.Description = "Selecione a pasta para salvar todos os arquivos otimizados";
                    if (fbd.ShowDialog() == DialogResult.OK)
                    {
                        targetFolder = fbd.SelectedPath;
                    }
                    else return;
                }
            }

            int savedCount = 0;
            try
            {
                foreach (var handle in loadedHandles)
                {
                    if (!string.IsNullOrEmpty(handle.FilePath))
                    {
                        string savePath = targetFolder != null ? Path.Combine(targetFolder, Path.GetFileName(handle.FilePath)) : handle.FilePath;
                        byte[] data = backend.SaveYtd(handle);
                        File.WriteAllBytes(savePath, data);
                        savedCount++;
                    }
                }
                MessageBox.Show($"Sucesso ao salvar {savedCount} arquivos.", "Salvar Tudo", MessageBoxButtons.OK, MessageBoxIcon.Information);
                if (statusLabel != null) statusLabel.Text = $"Salvos {savedCount} arquivos.";
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Erro ao salvar arquivos: {ex.Message}", "Erro", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void AddYtd(string filename)
        {
            try
            {
                if (statusLabel != null) statusLabel.Text = $"Loading {Path.GetFileName(filename)}...";
                Application.DoEvents();

                var handle = backend.LoadYtd(filename);
                loadedHandles.Add(handle);

                if (statusLabel != null) statusLabel.Text = $"Loaded {handle.Textures.Count} textures from {handle.Name}";

                duplicateGroups.Clear();
                showingDuplicates = false;
                expandedVirtualFolders.Clear();
                RenderTextures();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error loading YTD {filename}: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void AddWtd(string filename)
        {
            try
            {
                if (statusLabel != null) statusLabel.Text = $"Loading {Path.GetFileName(filename)}...";
                Application.DoEvents();

                var handle = backend.LoadWtd(filename);
                if (handle == null)
                {
                    MessageBox.Show($"The current backend ({backend.BackendName}) does not support WTD files.", "Unsupported", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                loadedHandles.Add(handle);

                if (statusLabel != null) statusLabel.Text = $"Loaded {handle.Textures.Count} textures from {handle.Name}";

                duplicateGroups.Clear();
                showingDuplicates = false;
                expandedVirtualFolders.Clear();
                RenderTextures();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error loading WTD {filename}: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void RenderTextures()
        {
            if (flowLayoutPanel == null) return;
            flowLayoutPanel.SuspendLayout();

            int scrollY = flowLayoutPanel.VerticalScroll.Value;
            flowLayoutPanel.Controls.Clear();

            int targetWidth = flowLayoutPanel.ClientSize.Width - flowLayoutPanel.Padding.Horizontal - 25;
            if (targetWidth < 200) targetWidth = 800;

            int totalVisibleTextures = 0;

            if (showingDuplicates && duplicateGroups.Count > 0)
            {
                bool rootExpanded = expandedVirtualFolders.Contains("DUPLICATES_ROOT");
                var rootFolder = new YtdFolderCard("DUPLICATES_ROOT", "DUPLICATES (FOLDER)", $"{duplicateGroups.Count} unique groups detected", Color.Cyan, rootExpanded, targetWidth);
                rootFolder.OnToggleRequested += (card) =>
                {
                    if (expandedVirtualFolders.Contains(card.VirtualId!)) expandedVirtualFolders.Remove(card.VirtualId!);
                    else expandedVirtualFolders.Add(card.VirtualId!);
                    RenderTextures();
                };
                flowLayoutPanel.Controls.Add(rootFolder);

                if (rootExpanded)
                {
                    foreach (var kvp in duplicateGroups)
                    {
                        string groupId = $"DUP_GROUP:{kvp.Key}";
                        bool groupExpanded = expandedVirtualFolders.Contains(groupId);
                        string displayName = kvp.Value.Count > 0 ? kvp.Value[0].Item1.Name : "Unknown Group";

                        var groupCard = new YtdFolderCard(groupId, $"Group: {displayName}", $"{kvp.Value.Count} instances", Color.Yellow, groupExpanded, targetWidth - 20);
                        groupCard.Margin = new Padding(20, 0, 0, 4);
                        groupCard.OnToggleRequested += (card) =>
                        {
                            if (expandedVirtualFolders.Contains(card.VirtualId!)) expandedVirtualFolders.Remove(card.VirtualId!);
                            else expandedVirtualFolders.Add(card.VirtualId!);
                            RenderTextures();
                        };
                        flowLayoutPanel.Controls.Add(groupCard);

                        if (groupExpanded)
                        {
                            FlowLayoutPanel subGrid = new FlowLayoutPanel();
                            subGrid.FlowDirection = FlowDirection.LeftToRight;
                            subGrid.WrapContents = true;
                            subGrid.AutoSize = true;
                            subGrid.Width = targetWidth;
                            subGrid.Padding = new Padding(40, 12, 0, 24);
                            subGrid.BackColor = Color.Transparent;

                            foreach (var item in kvp.Value)
                            {
                                AddTextureToGrid(item.Item1, item.Item2, subGrid);
                                totalVisibleTextures++;
                            }
                            flowLayoutPanel.Controls.Add(subGrid);
                        }
                    }
                }
            }

            foreach (var handle in loadedHandles)
            {
                bool isExpanded = expandedHandleIds.Contains(handle.Id);
                var folderCard = new YtdFolderCard(handle.Id, handle.Name, $"{handle.Textures.Count} textures", handle.FileType == "WTD" ? Color.FromArgb(255, 179, 71) : Theme.Primary, isExpanded, targetWidth);
                folderCard.OnToggleRequested += (card) =>
                {
                    bool wasExpanded = expandedHandleIds.Contains(card.VirtualId!);
                    expandedHandleIds.Clear();
                    if (!wasExpanded) expandedHandleIds.Add(card.VirtualId!);
                    RenderTextures();
                };
                flowLayoutPanel.Controls.Add(folderCard);

                if (isExpanded && handle.Textures.Count > 0)
                {
                    var filtered = new System.Collections.Generic.List<TextureInfo>();
                    foreach (var tex in handle.Textures)
                    {
                        if (string.IsNullOrEmpty(currentSearch) || tex.Name.ToLowerInvariant().Contains(currentSearch))
                            filtered.Add(tex);
                    }
                    filtered.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
                    totalVisibleTextures += filtered.Count;

                    if (filtered.Count > 0)
                    {
                        FlowLayoutPanel subGrid = new FlowLayoutPanel();
                        subGrid.FlowDirection = FlowDirection.LeftToRight;
                        subGrid.WrapContents = true;
                        subGrid.AutoSize = true;
                        subGrid.Width = targetWidth;
                        subGrid.Padding = new Padding(24, 12, 0, 24);
                        subGrid.BackColor = Color.Transparent;

                        foreach (var tex in filtered)
                            AddTextureToGrid(tex, handle, subGrid);
                        flowLayoutPanel.Controls.Add(subGrid);
                    }
                }
            }

            if (statusLabel != null)
                statusLabel.Text = $"[{backend.BackendName}] Loaded {loadedHandles.Count} files. Showing {totalVisibleTextures} textures.";

            flowLayoutPanel.ResumeLayout();
            try { flowLayoutPanel.VerticalScroll.Value = Math.Min(scrollY, flowLayoutPanel.VerticalScroll.Maximum); } catch { }
        }

        private void AddTextureToGrid(TextureInfo tex, YtdHandle parent, FlowLayoutPanel targetGrid)
        {
            try
            {
                int previewSize = 128;
                bool nativeSize = false;
                if (currentPreviewRes == "Native") nativeSize = true;
                else int.TryParse(currentPreviewRes, out previewSize);

                byte[]? pixels = backend.GetPixels(parent, tex.Name, 0);
                if (pixels == null) return;

                int width = tex.Width;
                int height = tex.Height;

                using (Bitmap originalBmp = new Bitmap(width, height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    var data = originalBmp.LockBits(new Rectangle(0, 0, width, height), System.Drawing.Imaging.ImageLockMode.WriteOnly, originalBmp.PixelFormat);
                    System.Runtime.InteropServices.Marshal.Copy(pixels, 0, data.Scan0, Math.Min(pixels.Length, data.Stride * height));
                    originalBmp.UnlockBits(data);

                    int displayW = nativeSize ? width : previewSize;
                    int displayH = nativeSize ? height : previewSize;

                    if (!nativeSize)
                    {
                        float ratio = (float)width / height;
                        if (ratio > 1) displayH = (int)(displayW / ratio);
                        else displayW = (int)(displayH * ratio);
                    }

                    Bitmap displayBmp = new Bitmap(originalBmp, displayW, displayH);

                    var card = new TextureCard(tex, displayBmp, parent);
                    card.OnResizeRequested += (t) => ResizeTexture_Click(t, parent);

                    Size newSize = new Size(220, 260);
                    if (currentPreviewRes == "128") newSize = new Size(160, 200);
                    else if (currentPreviewRes == "Native") newSize = new Size(300, 340);
                    card.Size = newSize;

                    var ctx = new ContextMenuStrip();
                    var darkRenderer = new ToolStripProfessionalRenderer(new DarkColorTable());
                    ctx.Renderer = darkRenderer;
                    ctx.BackColor = Color.FromArgb(45, 45, 48);
                    ctx.ForeColor = Color.White;
                    var resizeItem = new ToolStripMenuItem("Resize...", null, (s, e) => ResizeTexture_Click(tex, parent));
                    resizeItem.ForeColor = Color.White;
                    ctx.Items.Add(resizeItem);

                    card.ContextMenuStrip = ctx;
                    targetGrid.Controls.Add(card);
                }
            }
            catch
            {
            }
        }

        private void ResizeTexture_Click(TextureInfo tex, YtdHandle parent)
        {
            using (var dialog = new ResizeDialog(tex.Width, tex.Height))
            {
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    PerformTextureResize(tex, parent, dialog.NewWidth, dialog.NewHeight, dialog.SelectedFormat, dialog.NewMips);
                }
            }
        }

        private byte[]? DecodeTexturePixels(TextureInfo tex, YtdHandle parent)
        {
            byte[]? rawBgraPixels = null;

            byte[]? texData = backend.GetTextureData(parent, tex.Name);
            if (texData != null)
            {
                try
                {
                    TextureFormat cwFormat = Mapping.ParseFormat(tex.Format, TextureFormat.D3DFMT_DXT5);
                    CompressionFormat? inputFormat = null;
                    if (cwFormat == TextureFormat.D3DFMT_DXT1) inputFormat = CompressionFormat.Bc1;
                    else if (cwFormat == TextureFormat.D3DFMT_DXT3) inputFormat = CompressionFormat.Bc2;
                    else if (cwFormat == TextureFormat.D3DFMT_DXT5) inputFormat = CompressionFormat.Bc3;
                    else if (cwFormat == TextureFormat.D3DFMT_ATI1) inputFormat = CompressionFormat.Bc4;
                    else if (cwFormat == TextureFormat.D3DFMT_ATI2) inputFormat = CompressionFormat.Bc5;
                    else if (tex.Format.Contains("BC7")) inputFormat = CompressionFormat.Bc7;

                    if (inputFormat.HasValue)
                    {
                        var decoder = new BcDecoder();
                        var decodedColors = decoder.DecodeRaw(texData, tex.Width, tex.Height, inputFormat.Value);
                        if (decodedColors != null)
                        {
                            rawBgraPixels = new byte[decodedColors.Length * 4];
                            for (int i = 0; i < decodedColors.Length; i++)
                            {
                                var color = decodedColors[i];
                                int offset = i * 4;
                                rawBgraPixels[offset] = color.b;
                                rawBgraPixels[offset + 1] = color.g;
                                rawBgraPixels[offset + 2] = color.r;
                                rawBgraPixels[offset + 3] = color.a;
                            }
                        }
                    }
                }
                catch { }
            }

            if (rawBgraPixels == null)
                rawBgraPixels = backend.GetPixels(parent, tex.Name, 0);

            return rawBgraPixels;
        }

        private int CalculateStride(string formatName, int width)
        {
            TextureFormat fmt = Mapping.ParseFormat(formatName, TextureFormat.D3DFMT_DXT5);
            if (Mapping.IsCompressed(fmt))
            {
                int blocksWide = Math.Max(1, (width + 3) / 4);
                int blockSize = (fmt == TextureFormat.D3DFMT_DXT1 || fmt == TextureFormat.D3DFMT_ATI1) ? 8 : 16;
                return blocksWide * blockSize;
            }
            int bpp = (fmt == TextureFormat.D3DFMT_A1R5G5B5) ? 2 : ((fmt == TextureFormat.D3DFMT_A8) ? 1 : 4);
            return width * bpp;
        }

        private void PerformTextureResize(TextureInfo tex, YtdHandle parent, int newWidth, int newHeight, string formatSelection, int desiredMips)
        {
            try
            {
                byte[]? rawBgraPixels = DecodeTexturePixels(tex, parent);
                if (rawBgraPixels == null) throw new Exception("Could not decode texture.");

                TextureFormat targetFormat = Mapping.ParseFormat(formatSelection, Mapping.ParseFormat(tex.Format, TextureFormat.D3DFMT_DXT5));

                using (Bitmap fullBmp = new Bitmap(tex.Width, tex.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    var bmpData = fullBmp.LockBits(new Rectangle(0, 0, tex.Width, tex.Height), System.Drawing.Imaging.ImageLockMode.WriteOnly, fullBmp.PixelFormat);
                    System.Runtime.InteropServices.Marshal.Copy(rawBgraPixels, 0, bmpData.Scan0, Math.Min(rawBgraPixels.Length, bmpData.Stride * tex.Height));
                    fullBmp.UnlockBits(bmpData);

                    using (Bitmap resizedBmp = new Bitmap(newWidth, newHeight, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                    {
                        using (Graphics g = Graphics.FromImage(resizedBmp))
                        {
                            g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.HighQuality;
                            g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                            g.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.HighQuality;

                            using (var wrapMode = new System.Drawing.Imaging.ImageAttributes())
                            {
                                wrapMode.SetWrapMode(System.Drawing.Drawing2D.WrapMode.TileFlipXY);
                                g.DrawImage(fullBmp, new Rectangle(0, 0, newWidth, newHeight), 0, 0, fullBmp.Width, fullBmp.Height, GraphicsUnit.Pixel, wrapMode);
                            }
                        }

                        int mipsGenerated = 0;
                        int mipLimit = desiredMips == -2 ? tex.Levels : (desiredMips == -1 ? 99 : (desiredMips == 0 ? 1 : desiredMips));

                        var encoder = EncoderManager.GetEncoder(selectedEngine);
                        byte[] fullData = encoder.Encode(resizedBmp, targetFormat, mipLimit, out mipsGenerated);

                        int oldSize = tex.DataSize;
                        int stride = CalculateStride(targetFormat.ToString(), newWidth);

                        backend.UpdateTexture(parent, tex.Name, newWidth, newHeight, targetFormat.ToString(), mipsGenerated, stride, fullData);

                        RenderTextures();
                        MessageBox.Show($"Resized to {newWidth}x{newHeight} using {selectedEngine}.\nFormat: {targetFormat}, Mips: {mipsGenerated}\nSize: {oldSize:N0} -> {fullData.Length:N0} bytes");
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error resizing texture: {ex.Message}");
            }
        }
        private void SmartOptimize_Click()
        {
            YtdHandle? targetHandle = null;
            if (expandedHandleIds.Count == 1)
            {
                string expandedId = "";
                foreach (var id in expandedHandleIds) expandedId = id;
                targetHandle = loadedHandles.Find(h => h.Id == expandedId);
            }

            if (targetHandle == null && loadedHandles.Count == 1) targetHandle = loadedHandles[0];

            if (targetHandle == null)
            {
                MessageBox.Show("Please expand a YTD file folder to optimize it, or load only one file.", "Smart Optimize", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            using (var dialog = new SmartOptimizeDialog())
            {
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    PerformSmartOptimize(targetHandle, dialog.OptimizeBySize, dialog.TargetMiB, dialog.MaxResolution, dialog.PreferredFormat);
                }
            }
        }

        private float CalculateTotalMiB(YtdHandle handle)
        {
            long totalBytes = 0;
            foreach (var tex in handle.Textures)
                totalBytes += tex.DataSize;
            return totalBytes / 1024f / 1024f;
        }

        private void PerformSmartOptimize(YtdHandle handle, bool bySize, float targetMiB, int maxRes, string prefFormat)
        {
            if (handle.Textures.Count == 0) return;

            int optimizedCount = 0;
            float startMiB = CalculateTotalMiB(handle);

            try
            {
                if (bySize)
                {
                    while (CalculateTotalMiB(handle) > targetMiB)
                    {
                        var sorted = new System.Collections.Generic.List<TextureInfo>(handle.Textures);
                        sorted.Sort((a, b) => b.DataSize.CompareTo(a.DataSize));

                        TextureInfo? toReduce = null;
                        foreach (var t in sorted)
                        {
                            if (t.Width > 16 && t.Height > 16) { toReduce = t; break; }
                        }
                        if (toReduce == null) break;

                        int nw = Math.Max(16, toReduce.Width / 2);
                        int nh = Math.Max(16, toReduce.Height / 2);
                        InternalResizeTexture(toReduce, handle, nw, nh, prefFormat, -1);
                        optimizedCount++;
                    }
                }
                else
                {
                    foreach (var tex in new System.Collections.Generic.List<TextureInfo>(handle.Textures))
                    {
                        if (tex.Width > maxRes || tex.Height > maxRes)
                        {
                            float ratio = (float)tex.Width / tex.Height;
                            int nw, nh;
                            if (ratio >= 1) { nw = maxRes; nh = (int)Math.Round(maxRes / ratio); }
                            else { nh = maxRes; nw = (int)Math.Round(maxRes * ratio); }
                            InternalResizeTexture(tex, handle, nw, nh, prefFormat, -1);
                            optimizedCount++;
                        }
                    }
                }

                float endMiB = CalculateTotalMiB(handle);
                RenderTextures();
                MessageBox.Show($"Smart Optimization Complete!\nTextures affected: {optimizedCount}\nSize: {startMiB:F2} MiB -> {endMiB:F2} MiB", "Smart Optimize", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error during smart optimization: {ex.Message}", "Error");
            }
        }

        private void InternalResizeTexture(TextureInfo tex, YtdHandle parent, int newWidth, int newHeight, string formatSelection, int desiredMips)
        {
            byte[]? rawBgraPixels = DecodeTexturePixels(tex, parent);
            if (rawBgraPixels == null) return;

            TextureFormat targetFormat = Mapping.ParseFormat(formatSelection, Mapping.ParseFormat(tex.Format, TextureFormat.D3DFMT_DXT5));

            using (Bitmap fullBmp = new Bitmap(tex.Width, tex.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
            {
                var bmpData = fullBmp.LockBits(new Rectangle(0, 0, tex.Width, tex.Height), System.Drawing.Imaging.ImageLockMode.WriteOnly, fullBmp.PixelFormat);
                System.Runtime.InteropServices.Marshal.Copy(rawBgraPixels, 0, bmpData.Scan0, Math.Min(rawBgraPixels.Length, bmpData.Stride * tex.Height));
                fullBmp.UnlockBits(bmpData);

                using (Bitmap resizedBmp = new Bitmap(newWidth, newHeight, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    using (Graphics g = Graphics.FromImage(resizedBmp))
                    {
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.HighQuality;
                        g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                        g.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.HighQuality;

                        using (var wrapMode = new System.Drawing.Imaging.ImageAttributes())
                        {
                            wrapMode.SetWrapMode(System.Drawing.Drawing2D.WrapMode.TileFlipXY);
                            g.DrawImage(fullBmp, new Rectangle(0, 0, newWidth, newHeight), 0, 0, fullBmp.Width, fullBmp.Height, GraphicsUnit.Pixel, wrapMode);
                        }
                    }

                    int mipsGenerated = 0;
                    int mipLimit = desiredMips == -2 ? tex.Levels : (desiredMips == -1 ? 99 : (desiredMips == 0 ? 1 : desiredMips));

                    var encoder = EncoderManager.GetEncoder(selectedEngine);
                    byte[] fullData = encoder.Encode(resizedBmp, targetFormat, mipLimit, out mipsGenerated);

                    int stride = CalculateStride(targetFormat.ToString(), newWidth);
                    backend.UpdateTexture(parent, tex.Name, newWidth, newHeight, targetFormat.ToString(), mipsGenerated, stride, fullData);
                }
            }
        }

        private void PerformDeDuplicationAnalysis(bool byName, bool byHex)
        {
            if (loadedHandles.Count == 0) return;

            duplicateGroups.Clear();
            var allTextures = new System.Collections.Generic.List<(TextureInfo tex, YtdHandle handle)>();

            foreach (var handle in loadedHandles)
            {
                foreach (var tex in handle.Textures)
                    allTextures.Add((tex, handle));
            }

            var groups = new System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(TextureInfo, YtdHandle)>>(StringComparer.OrdinalIgnoreCase);

            foreach (var item in allTextures)
            {
                string key = "";
                if (byName) key = item.tex.Name;
                else if (byHex) key = GetTextureHash(item.tex, item.handle);

                if (string.IsNullOrEmpty(key)) continue;

                if (!groups.ContainsKey(key))
                    groups[key] = new System.Collections.Generic.List<(TextureInfo, YtdHandle)>();

                groups[key].Add(item);
            }

            int dupCount = 0;
            foreach (var kvp in groups)
            {
                if (kvp.Value.Count > 1)
                {
                    duplicateGroups[kvp.Key] = kvp.Value;
                    dupCount++;
                }
            }

            showingDuplicates = true;
            expandedHandleIds.Clear();
            RenderTextures();

            string modeStr = byHex ? "Hex (Conteúdo)" : "Nome";
            MessageBox.Show($"Análise concluída usando matching por {modeStr}.\nEncontrados {dupCount} grupos de duplicatas.", "Análise de Duplicatas", MessageBoxButtons.OK, MessageBoxIcon.Information);
            if (statusLabel != null) statusLabel.Text = $"Encontrados {dupCount} grupos duplicados ({modeStr})";
        }

        private string GetTextureHash(TextureInfo tex, YtdHandle handle)
        {
            byte[]? data = backend.GetTextureData(handle, tex.Name);
            if (data == null) return "";
            using (SHA256 sha256 = SHA256.Create())
            {
                byte[] hashBytes = sha256.ComputeHash(data);
                return BitConverter.ToString(hashBytes).Replace("-", "").ToLower();
            }
        }

        private void MigrateDuplicates_Click()
        {
            if (duplicateGroups.Count == 0)
            {
                MessageBox.Show("Please run Duplicate Analysis first and ensure duplicates were found.", "Migrate Duplicates", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            var confirm = MessageBox.Show($"This will move one instance of each duplicate group into a new YTD and REMOVE them from their original files.\n\nGroups to migrate: {duplicateGroups.Count}\n\nDo you want to proceed?", "Confirm Migration", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (confirm != DialogResult.Yes) return;

            try
            {
                var consolidatedTextures = new System.Collections.Generic.List<(string name, uint nameHash, int width, int height, string format, int levels, int stride, byte[] data)>();

                foreach (var kvp in duplicateGroups)
                {
                    if (kvp.Value == null || kvp.Value.Count == 0) continue;

                    var masterTex = kvp.Value[0].Item1;
                    var masterHandle = kvp.Value[0].Item2;
                    byte[]? data = backend.GetTextureData(masterHandle, masterTex.Name);
                    if (data == null) continue;

                    consolidatedTextures.Add((masterTex.Name, masterTex.NameHash, masterTex.Width, masterTex.Height, masterTex.Format, masterTex.Levels, masterTex.Stride, data));

                    foreach (var occurrence in kvp.Value)
                    {
                        backend.RemoveTexture(occurrence.Item2, occurrence.Item1.Name);
                    }
                }

                var consolidatedHandle = backend.CreateYtd("consolidated_textures.ytd", consolidatedTextures);

                SaveFileDialog sfd = new SaveFileDialog();
                sfd.FileName = "consolidated_textures.ytd";
                sfd.Filter = "YTD Files (*.ytd)|*.ytd";
                if (sfd.ShowDialog() == DialogResult.OK)
                {
                    byte[] savedData = backend.SaveYtd(consolidatedHandle);
                    File.WriteAllBytes(sfd.FileName, savedData);

                    consolidatedHandle.FilePath = sfd.FileName;
                    loadedHandles.Add(consolidatedHandle);

                    MessageBox.Show("Migration complete! Duplicate textures have been consolidated and original files optimized.", "Success", MessageBoxButtons.OK, MessageBoxIcon.Information);

                    duplicateGroups.Clear();
                    showingDuplicates = false;
                    expandedVirtualFolders.Clear();
                    RenderTextures();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error during migration: {ex.Message}\n\nStack Trace: {ex.StackTrace}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
