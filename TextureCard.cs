using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using System.ComponentModel;

namespace EasyOptimizerV
{
    public class TextureCard : UserControl
    {
        private TextureInfo texture;
        private YtdHandle parentHandle;
        private Image originalImage;

        public string TextureName => texture?.Name ?? "";
        public TextureInfo TextureObject => texture;
        public string ParentYtdName => parentHandle?.Name ?? "";

        private bool isHovered = false;
        private Rectangle editButtonRect;
        [Browsable(false)]
        [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public string DuplicateType { get; set; } = "";

        [Browsable(false)]
        [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public System.Collections.Generic.List<string> OtherYtds { get; set; } = new System.Collections.Generic.List<string>();

        public event Action<TextureInfo> OnResizeRequested;

        public TextureCard(TextureInfo tex, Image img, YtdHandle parent)
        {
            this.texture = tex;
            this.parentHandle = parent;
            this.originalImage = img;
            this.DoubleBuffered = true;
            this.Size = new Size(220, 260);
            this.BackColor = Color.Transparent;
            this.Margin = new Padding(8);

            var ctx = new ContextMenuStrip();
            ctx.Renderer = new ToolStripProfessionalRenderer(new DarkColorTable()) { RoundedEdges = false };
            ctx.BackColor = Theme.SurfaceDark;
            ctx.ForeColor = Theme.TextPrimaryDark;
            var itemResize = new ToolStripMenuItem("Resize...", null, (s, e) => OnResizeRequested?.Invoke(texture));
            itemResize.ForeColor = Theme.TextPrimaryDark;
            ctx.Items.Add(itemResize);
            this.ContextMenuStrip = ctx;

            this.MouseEnter += (s, e) => { isHovered = true; Invalidate(); };
            this.MouseLeave += (s, e) => { isHovered = false; Invalidate(); };
            this.MouseMove += TextureCard_MouseMove;
            this.MouseClick += TextureCard_MouseClick;
        }

        private void TextureCard_MouseMove(object? sender, MouseEventArgs e)
        {
            this.Cursor = editButtonRect.Contains(e.Location) ? Cursors.Hand : Cursors.Default;
            Invalidate(editButtonRect);
        }

        private void TextureCard_MouseClick(object? sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left && editButtonRect.Contains(e.Location))
                OnResizeRequested?.Invoke(texture);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;

            Rectangle rect = new Rectangle(0, 0, this.Width - 1, this.Height - 1);
            using (GraphicsPath path = Theme.CreateRoundedRect(rect, 8))
            {
                using (SolidBrush bgBrush = new SolidBrush(Theme.SurfaceDark))
                    e.Graphics.FillPath(bgBrush, path);

                using (Pen borderPen = new Pen(isHovered ? Theme.Primary : Theme.BorderDark, 1))
                    e.Graphics.DrawPath(borderPen, path);

                e.Graphics.SetClip(path);

                int footerHeight = 80;
                int imgHeight = this.Height - footerHeight;
                Rectangle imgRect = new Rectangle(0, 0, this.Width, imgHeight);

                using (SolidBrush imgBg = new SolidBrush(Color.Black))
                    e.Graphics.FillRectangle(imgBg, imgRect);

                if (originalImage != null)
                {
                    float ratio = Math.Min((float)imgRect.Width / originalImage.Width, (float)imgRect.Height / originalImage.Height);
                    int w = (int)(originalImage.Width * ratio);
                    int h = (int)(originalImage.Height * ratio);
                    int x = (imgRect.Width - w) / 2;
                    int y = (imgRect.Height - h) / 2;
                    e.Graphics.DrawImage(originalImage, x, y, w, h);
                }

                string fmt = texture.Format.Replace("D3DFMT_", "");
                Font badgeFont = Theme.FontMono;
                SizeF badgeSize = e.Graphics.MeasureString(fmt, badgeFont);
                int padX = 4, padY = 2;
                Rectangle badgeRect = new Rectangle(
                    imgRect.Right - (int)badgeSize.Width - padX * 2 - 8, imgRect.Top + 8,
                    (int)badgeSize.Width + padX * 2, (int)badgeSize.Height + padY * 2);

                using (GraphicsPath badgePath = Theme.CreateRoundedRect(badgeRect, 3))
                {
                    using (SolidBrush badgeBg = new SolidBrush(Color.FromArgb(150, 0, 0, 0)))
                        e.Graphics.FillPath(badgeBg, badgePath);
                    e.Graphics.DrawString(fmt, badgeFont, Brushes.White, badgeRect.X + padX, badgeRect.Y + padY);
                }

                if (!string.IsNullOrEmpty(DuplicateType))
                {
                    string dupText = DuplicateType == "Hex" ? "HEX DUP" : "NAME DUP";
                    Color dupColor = DuplicateType == "Hex" ? Color.FromArgb(220, 38, 38) : Color.FromArgb(234, 179, 8);
                    Font dupFont = new Font("Segoe UI", 7F, FontStyle.Bold);
                    SizeF dupSize = e.Graphics.MeasureString(dupText, dupFont);
                    Rectangle dupRect = new Rectangle(imgRect.Left + 8, imgRect.Top + 8, (int)dupSize.Width + 8, (int)dupSize.Height + 4);
                    using (GraphicsPath dp = Theme.CreateRoundedRect(dupRect, 3))
                    {
                        using (SolidBrush db = new SolidBrush(dupColor)) e.Graphics.FillPath(db, dp);
                        e.Graphics.DrawString(dupText, dupFont, Brushes.White, dupRect.X + 4, dupRect.Y + 2);
                    }
                }

                e.Graphics.ResetClip();

                int textY = imgHeight + 12;
                int textX = 12;

                string name = texture.Name;
                long sizeBytes = texture.DataSize;
                if (sizeBytes == 0) sizeBytes = (long)(texture.Width * texture.Height * 4 * 1.33);

                double mib = sizeBytes / (1024.0 * 1024.0);
                string sizeText = $"{mib:F2} MiB";

                Color sizeColor;
                if (mib <= 0.70) sizeColor = Color.FromArgb(22, 163, 74);
                else if (mib <= 1.0) sizeColor = Color.FromArgb(74, 222, 128);
                else if (mib <= 1.5) sizeColor = Color.FromArgb(253, 224, 71);
                else if (mib <= 2.0) sizeColor = Color.FromArgb(234, 179, 8);
                else if (mib <= 2.5) sizeColor = Color.FromArgb(251, 146, 60);
                else if (mib <= 3.5) sizeColor = Color.FromArgb(249, 115, 22);
                else if (mib < 4.5) sizeColor = Color.FromArgb(239, 68, 68);
                else sizeColor = Color.FromArgb(220, 38, 38);

                using (SolidBrush textBrush = new SolidBrush(Theme.TextPrimaryDark))
                {
                    StringFormat sf = new StringFormat(StringFormatFlags.NoWrap) { Trimming = StringTrimming.EllipsisCharacter };
                    Rectangle nameRect = new Rectangle(textX, textY, this.Width - textX * 2 - 70, 20);
                    e.Graphics.DrawString(name, Theme.FontTitle, textBrush, nameRect, sf);
                }

                using (SolidBrush sizeBrush = new SolidBrush(sizeColor))
                {
                    StringFormat sfSize = new StringFormat { Alignment = StringAlignment.Far };
                    Rectangle sizeRect = new Rectangle(this.Width - 80, textY, 70, 20);
                    e.Graphics.DrawString(sizeText, Theme.FontSmallBold, sizeBrush, sizeRect, sfSize);
                }

                textY += 24;
                string meta = $"{texture.Width} x {texture.Height}";
                string mips = $"Mips: {texture.Levels}";

                using (SolidBrush textSec = new SolidBrush(Theme.TextSecondaryDark))
                {
                    e.Graphics.DrawString(meta, Theme.FontSmall, textSec, textX, textY);
                    e.Graphics.DrawString(mips, Theme.FontSmall, textSec, textX, textY + 14);
                    e.Graphics.DrawString(parentHandle?.Name ?? "Unknown", Theme.FontSmall, textSec, textX, textY + 28);
                }

                int btnW = 60, btnH = 24;
                editButtonRect = new Rectangle(this.Width - btnW - 12, this.Height - btnH - 12, btnW, btnH);
                bool isMouseOverEdit = editButtonRect.Contains(this.PointToClient(Cursor.Position));

                using (GraphicsPath btnPath = Theme.CreateRoundedRect(editButtonRect, 4))
                using (SolidBrush btnBrush = new SolidBrush(isMouseOverEdit ? Theme.Primary : Color.FromArgb(60, 60, 60)))
                    e.Graphics.FillPath(btnBrush, btnPath);

                using (SolidBrush textBrush = new SolidBrush(Color.White))
                {
                    StringFormat sfBtn = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
                    e.Graphics.DrawString("Edit", Theme.FontSmall, textBrush, editButtonRect, sfBtn);
                }
            }
        }
    }
}
