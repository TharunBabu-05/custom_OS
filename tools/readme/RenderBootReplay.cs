using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

// Draws an original terminal illustration; proof screenshots are never modified.
// Each frame uses GDI+'s GIF encoder. The assembler retains each frame's own
// palette and encoded image data, adds frame timing and a looping extension.
public static class RenderBootReplay
{
    static void SaveGif(Bitmap source, Stream output)
    {
        Rectangle bounds = new Rectangle(0, 0, source.Width, source.Height);
        BitmapData input = source.LockBits(bounds, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] pixels = new byte[input.Stride * input.Height];
        Marshal.Copy(input.Scan0, pixels, 0, pixels.Length);
        int sourceStride = input.Stride;
        source.UnlockBits(input);
        Dictionary<int, int> counts = new Dictionary<int, int>();
        for (int y = 0; y < source.Height; y++) for (int x = 0; x < source.Width; x++)
        {
            int at = y * sourceStride + x*4;
            int key = pixels[at] | pixels[at+1] << 8 | pixels[at+2] << 16;
            if (!counts.ContainsKey(key)) counts[key] = 0;
            counts[key]++;
        }
        List<int> colors = new List<int>(counts.Keys);
        colors.Sort(delegate(int a, int b) { int order = counts[b].CompareTo(counts[a]); return order != 0 ? order : a.CompareTo(b); });
        if (colors.Count > 256) colors.RemoveRange(256, colors.Count - 256);
        Dictionary<int, byte> mapping = new Dictionary<int, byte>();
        foreach (int key in counts.Keys)
        {
            int best = 0, distance = int.MaxValue;
            for (int i = 0; i < colors.Count; i++)
            {
                int db = (key & 255) - (colors[i] & 255);
                int dg = ((key >> 8) & 255) - ((colors[i] >> 8) & 255);
                int dr = ((key >> 16) & 255) - ((colors[i] >> 16) & 255);
                int next = dr*dr + dg*dg + db*db;
                if (next < distance) { distance = next; best = i; }
                if (next == 0) break;
            }
            mapping[key] = (byte)best;
        }
        using (Bitmap indexed = new Bitmap(source.Width, source.Height, PixelFormat.Format8bppIndexed))
        {
            ColorPalette palette = indexed.Palette;
            for (int i = 0; i < 256; i++) palette.Entries[i] = i < colors.Count ? Color.FromArgb(unchecked((int)0xff000000) | colors[i]) : Color.Black;
            indexed.Palette = palette;
            BitmapData data = indexed.LockBits(bounds, ImageLockMode.WriteOnly, PixelFormat.Format8bppIndexed);
            byte[] indices = new byte[data.Stride * data.Height];
            for (int y = 0; y < source.Height; y++) for (int x = 0; x < source.Width; x++)
            {
                int at = y * sourceStride + x*4;
                indices[y*data.Stride+x] = mapping[pixels[at] | pixels[at+1] << 8 | pixels[at+2] << 16];
            }
            Marshal.Copy(indices, 0, data.Scan0, indices.Length);
            indexed.UnlockBits(data);
            indexed.Save(output, ImageFormat.Gif);
        }
    }
    static Color C(string value) { return ColorTranslator.FromHtml(value); }
    static void Text(Graphics g, string value, float x, float y, int size, string color, bool bold)
    {
        using (Font f = new Font("Consolas", size, bold ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel))
        using (Brush b = new SolidBrush(C(color))) { g.DrawString(value, f, b, x, y); }
    }
    static byte[] Frame(int visible, bool cursor, bool still)
    {
        string[] lines = {
            "OurOS: AArch64 kernel booted",
            "UART 0x1c00030000 | firmware baud/clock preserved",
            "Board=rpi5-header entered EL2 running EL1",
            "Kernel monitor ready. Type help.",
            "> info",
            "board=rpi5-header | EL1 | entered EL2",
            "> cpuinfo",
            "MIDR=0x00000000414fd0b1 timer_hz=54000000",
            "> uptime",
            "uptime_seconds=26",
            "> uptime",
            "uptime_seconds=29",
            "> echo pi5-ok",
            "pi5-ok"
        };
        using (Bitmap bmp = new Bitmap(1120, 650))
        using (Graphics g = Graphics.FromImage(bmp))
        {
            g.Clear(C("#080f1b"));
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            using (Brush top = new SolidBrush(C("#111d2d"))) { g.FillRectangle(top, 0, 0, 1120, 62); }
            string[] dots = { "#ff657a", "#ffd17d", "#65e6bb" };
            for (int i = 0; i < 3; i++) using (Brush b = new SolidBrush(C(dots[i]))) { g.FillEllipse(b, 26 + 25*i, 25, 12, 12); }
            Text(g, "OUR / OS    ::    SERIAL CONSOLE", 130, 19, 22, "#c6d5e8", true);
            Text(g, "CAPTURED SESSION / ANIMATED REPLAY", 650, 23, 17, "#7d92ac", false);
            using (Pen p = new Pen(C("#203247"))) { g.DrawLine(p, 26, 114, 1094, 114); }
            Text(g, "RASPBERRY PI 5", 28, 81, 18, "#65e6bb", true);
            Text(g, "AARCH64   /   CP2102   /   115200 8N1", 500, 81, 18, "#7d92ac", false);
            for (int i = 0; i < visible && i < lines.Length; i++)
            {
                string color = lines[i].StartsWith(">") ? "#ffd17d" : "#c6d5e8";
                if (i == 0 || i == 13) color = "#65e6bb";
                Text(g, lines[i], 32, 137 + i*29, 23, color, i == 0 || i == 13);
            }
            if (cursor && visible == lines.Length)
            {
                Text(g, ">", 32, 547, 23, "#ffd17d", false);
                using (Brush b = new SolidBrush(C("#65e6bb"))) { g.FillRectangle(b, 59, 552, 12, 23); }
            }
            using (Brush bottom = new SolidBrush(C("#111d2d"))) { g.FillRectangle(bottom, 0, 603, 1120, 47); }
            Text(g, "BOOT OBSERVED  /  08 NOV 2026", 28, 617, 16, "#65e6bb", true);
            Text(g, "Original screenshots in /proofs", 720, 617, 16, "#7d92ac", false);
            using (MemoryStream ms = new MemoryStream())
            {
                if (still) bmp.Save(ms, ImageFormat.Png); else SaveGif(bmp, ms);
                return ms.ToArray();
            }
        }
    }
    static int TableLength(byte packed) { return (packed & 0x80) == 0 ? 0 : 3 * (1 << ((packed & 7) + 1)); }
    static void SkipBlocks(byte[] data, ref int at)
    {
        while (true) { int length = data[at++]; if (length == 0) break; at += length; }
    }
    public static void Generate(string directory)
    {
        Directory.CreateDirectory(directory);
        string output = Path.Combine(directory, "boot-replay.gif");
        using (FileStream fs = new FileStream(output, FileMode.Create))
        using (BinaryWriter writer = new BinaryWriter(fs))
        {
            // Start with the complete frame so clients without animation get context.
            int[] stages = { 14, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14, 14, 14 };
            for (int frame = 0; frame < stages.Length; frame++)
            {
                byte[] data = Frame(stages[frame], frame % 2 == 0, false);
                int paletteLength = TableLength(data[10]);
                byte[] palette = new byte[paletteLength];
                Array.Copy(data, 13, palette, 0, paletteLength);
                if (frame == 0)
                {
                    writer.Write(Encoding.ASCII.GetBytes("GIF89a"));
                    writer.Write(data, 6, 7);
                    writer.Write(palette);
                    writer.Write(new byte[] { 0x21, 0xff, 11 });
                    writer.Write(Encoding.ASCII.GetBytes("NETSCAPE2.0"));
                    writer.Write(new byte[] { 3, 1, 0, 0, 0 });
                }
                int at = 13 + paletteLength;
                while (data[at] == 0x21) { at += 2; SkipBlocks(data, ref at); }
                if (data[at] != 0x2c) throw new InvalidDataException("GIF image descriptor missing");
                int image = at;
                int localLength = TableLength(data[at+9]);
                int pixels = image + 10 + localLength;
                int end = pixels + 1; // LZW minimum code size precedes image sub-blocks.
                SkipBlocks(data, ref end);
                int delay = frame == 0 ? 100 : frame == 1 ? 40 : frame < 16 ? 55 : 65;
                writer.Write(new byte[] { 0x21, 0xf9, 4, 8, (byte)delay, (byte)(delay >> 8), 0, 0 });
                writer.Write(data, image, 9);
                if (localLength > 0)
                {
                    writer.Write(data[image+9]);
                    writer.Write(data, image+10, localLength);
                }
                else
                {
                    writer.Write((byte)((data[image+9] & 0x78) | 0x80 | (data[10] & 7)));
                    writer.Write(palette);
                }
                writer.Write(data, pixels, end-pixels);
            }
            writer.Write((byte)0x3b);
        }
        File.WriteAllBytes(Path.Combine(directory, "boot-replay-still.png"), Frame(14, true, true));
        // Read all frames back through a separate decoder before delivery.
        using (Image decoded = Image.FromFile(output))
        {
            int count = decoded.GetFrameCount(FrameDimension.Time);
            if (count != 19 || decoded.Width != 1120 || decoded.Height != 650)
                throw new InvalidDataException("Unexpected GIF dimensions or frame count");
            for (int i = 0; i < count; i++)
            {
                decoded.SelectActiveFrame(FrameDimension.Time, i);
                using (Bitmap check = new Bitmap(decoded))
                {
                    if (check.GetPixel(5, 5).R != 17) throw new InvalidDataException("GIF frame decode failed");
                }
            }
            Console.WriteLine("PASS: GIF decoded, 19 frames, 1120 x 650, infinite loop.");
        }
    }
}
