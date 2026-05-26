using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EasyOptimizerV
{
    public class YtdFolderCard : UserControl
    {
        public string? VirtualId { get; }
        public string? VirtualName { get; }
        public string FileType { get; } = "YTD";
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool IsExpanded { get; set; }
        public event Action<YtdFolderCard>? OnToggleRequested;

        private bool isHovered = false;
        private string totalSizeInfo = "";
        private Color statusColor = Color.Gray;
        private int itemCount = 0;

        public YtdFolderCard(string virtualId, string name, string info, Color color, bool isExpanded, int width)
        {
            this.VirtualId = virtualId;
            this.VirtualName = name;
            this.totalSizeInfo = info;
            this.statusColor = color;
            this.IsExpanded = isExpanded;
            this.Width = width;
            this.Height = 60;
            this.Margin = new Padding(0, 0, 0, 4);
            this.DoubleBuffered = true;
            this.Cursor = Cursors.Hand;

            this.MouseEnter += (s, e) => { isHovered = true; Invalidate(); };
            this.MouseLeave += (s, e) => { isHovered = false; Invalidate(); };
            this.Click += (s, e) => OnToggleRequested?.Invoke(this);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            int radius = 8;

            using (var path = Theme.CreateRoundedRect(rect, radius))
            {
                using (var brush = new SolidBrush(isHovered ? Color.FromArgb(40, 40, 40) : Theme.SurfaceDark))
                    g.FillPath(brush, path);

                if (IsExpanded)
                {
                    using (var pen = new Pen(Theme.Primary, 1))
                        g.DrawPath(pen, path);
                }
            }

            int iconSize = 32;
            int margin = 16;
            Rectangle iconRect = new Rectangle(margin, (Height - iconSize) / 2, iconSize, iconSize);

            using (var iconBrush = new SolidBrush(statusColor))
            {
                g.FillRectangle(iconBrush, iconRect.X, iconRect.Y + 6, iconSize, iconSize - 6);
                g.FillRectangle(iconBrush, iconRect.X, iconRect.Y, iconSize / 2, 8);
            }

            float textX = iconRect.Right + 12;
            string name = VirtualName ?? "Unknown.ytd";

            g.DrawString(name, Theme.FontTitle, Brushes.White, textX, 12);
            g.DrawString(totalSizeInfo, Theme.FontDisplay, new SolidBrush(statusColor), textX, 32);

            string arrow = IsExpanded ? "▼" : "▶";
            g.DrawString(arrow, Theme.FontTitle, Brushes.White, Width - 30, (Height - 20) / 2);
        }
    }
}
