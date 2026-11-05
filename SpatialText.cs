using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

internal static class SpatialText
{
    internal static bool IsLiteral(string text) { return Regex.IsMatch(text.Trim(), @"^[0-9]+(?:[.,][0-9]+)*(?:[%+])?$"); }
    internal static Program.TextLine[] Group(IEnumerable<Program.TextLine> input)
    {
        var rows = new List<List<Program.TextLine>>();
        foreach (var word in input.Where(x => !x.Box.IsEmpty && x.Text.Length > 0).OrderBy(x => x.Box.Top).ThenBy(x => x.Box.Left))
        {
            var row = rows.FirstOrDefault(x => {
                RectangleF box = x[0].Box;
                float overlap = Math.Min(box.Bottom, word.Box.Bottom) - Math.Max(box.Top, word.Box.Top);
                return overlap >= Math.Min(box.Height, word.Box.Height) * .5f;
            });
            if (row == null) { row = new List<Program.TextLine>(); rows.Add(row); }
            row.Add(word);
        }
        var output = new List<Program.TextLine>();
        Program.TextLine paragraph = null;
        foreach (var row in rows.OrderBy(x => x.Min(y => y.Box.Top)))
        {
            int firstBlock = output.Count;
            var ordered = row.OrderBy(x => x.Box.Left).ToArray();
            float height = ordered.Select(x => x.Box.Height).OrderBy(x => x).ElementAt(ordered.Length / 2);
            int gaps = Enumerable.Range(1, Math.Max(0, ordered.Length - 1))
                .Count(i => ordered[i].Box.Left - ordered[i - 1].Box.Right > Math.Max(14, height * 1.2f));
            bool navigation = gaps >= 2 && ordered.Sum(x => Regex.Matches(x.Text, "[A-Za-z]").Count) > ordered.Sum(x => Regex.Matches(x.Text, @"[\u3400-\u9fff]").Count) * 3;
            var run = new List<Program.TextLine>();
            for (int i = 0; i < ordered.Length; i++)
            {
                var word = ordered[i];
                bool square = word.Box.Width >= word.Box.Height * .65f && word.Box.Width <= word.Box.Height * 1.6f;
                int next = i + 1;
                while (next < ordered.Length && next <= i + 2 && Regex.IsMatch(ordered[next].Text, @"^[^\p{L}\p{N}]+$|^[丿丨]+$")) next++;
                bool symbol = !Regex.IsMatch(word.Text, "[A-Za-z]") || word.Text.Length <= 3 && Regex.IsMatch(word.Text, @"[^\p{L}\p{N}]");
                bool stroke = navigation && Regex.IsMatch(word.Text, "^[0丿丨]+$") &&
                    word.Box.Width <= word.Box.Height * 1.6f && word.Box.Height >= height * .75f;
                bool icon = (square || stroke) && word.Text.Length <= 3 && symbol
                    && next < ordered.Length && Regex.IsMatch(ordered[next].Text, "^[A-Za-z]")
                    && (navigation || word.Box.Height > ordered[next].Box.Height * 1.5f)
                    && (i == 0 || word.Box.Left - ordered[i - 1].Box.Right > height);
                if (icon) { Flush(run, output); i = next - 1; continue; }
                if (run.Count > 0 && word.Box.Left - run[run.Count - 1].Box.Right > Math.Max(14, height * 1.2f)) Flush(run, output);
                run.Add(word);
            }
            Flush(run, output);
            for (int i = firstBlock; i < output.Count; i++) output[i].GlyphHeight = height;
            if (output.Count - firstBlock == 1)
            {
                var block = output[firstBlock];
                bool listItem = Regex.IsMatch(block.Text, @"^(?:[-*]|[0-9]+[.)])\s");
                if (paragraph != null && !listItem && Math.Abs(paragraph.Box.Left - block.Box.Left) <= block.GlyphHeight * .6 &&
                    block.Box.Top - paragraph.Box.Bottom >= -2 && block.Box.Top - paragraph.Box.Bottom <= Math.Max(8, block.GlyphHeight) &&
                    Math.Abs(paragraph.GlyphHeight - block.GlyphHeight) <= 3)
                {
                    paragraph.Text += "\n" + block.Text;
                    paragraph.Box = RectangleF.Union(paragraph.Box, block.Box);
                    output.RemoveAt(firstBlock);
                }
                else paragraph = listItem ? null : block;
            }
            else paragraph = null;
        }
        return output.ToArray();
    }
    private static bool Cjk(char c) { return c >= 0x2e80 && c <= 0x9fff || c >= 0xf900 && c <= 0xfaff || Char.IsSurrogate(c); }
    private static void Flush(List<Program.TextLine> words, List<Program.TextLine> output)
    {
        if (words.Count == 0) return;
        var text = new StringBuilder();
        RectangleF box = words[0].Box;
        Program.TextLine previous = null;
        foreach (var word in words)
        {
            if (text.Length > 0 && !(Cjk(text[text.Length - 1]) && Cjk(word.Text[0])) &&
                word.Box.Left - previous.Box.Right > Math.Max(1, Math.Min(previous.Box.Height, word.Box.Height) * .16f)) text.Append(' ');
            text.Append(word.Text);
            box = RectangleF.Union(box, word.Box);
            previous = word;
        }
        output.Add(new Program.TextLine { Text = text.ToString(), Box = box, GlyphHeight = words.Select(x => x.Box.Height).OrderBy(x => x).ElementAt(words.Count / 2) });
        words.Clear();
    }
    internal static Program.TextLine[] Refine(Program.TextLine[] original, Program.TextLine[] enlarged)
    {
        var output = new List<Program.TextLine>();
        foreach (var block in original)
        {
            if (IsLiteral(block.Text)) { output.Add(block); continue; }
            var candidate = enlarged.OrderByDescending(x => RectangleF.Intersect(block.Box, x.Box).Width * RectangleF.Intersect(block.Box, x.Box).Height).FirstOrDefault();
            float overlap = candidate == null ? 0 : RectangleF.Intersect(block.Box, candidate.Box).Width * RectangleF.Intersect(block.Box, candidate.Box).Height;
            bool cleaner = candidate != null && CleanerIconPrefix(block, candidate);
            if (candidate != null && overlap > block.Box.Width * block.Box.Height * .5 &&
                overlap > candidate.Box.Width * candidate.Box.Height * .5 &&
                (cleaner || Regex.Matches(candidate.Text, @"\p{L}").Count > Regex.Matches(block.Text, @"\p{L}").Count))
                output.Add(candidate);
            else output.Add(block);
        }
        return output.ToArray();
    }
    private static bool CleanerIconPrefix(Program.TextLine block, Program.TextLine candidate)
    {
        if (!Regex.IsMatch(candidate.Text, "^[A-Za-z]") || !block.Text.EndsWith(candidate.Text, StringComparison.OrdinalIgnoreCase)) return false;
        string prefix = block.Text.Substring(0, block.Text.Length - candidate.Text.Length).Trim();
        float offset = candidate.Box.Left - block.Box.Left;
        bool symbol = Regex.IsMatch(prefix, @"^[0-9\p{P}\p{S}\s丿丨]+$");
        bool mixedGlyph = Regex.IsMatch(prefix, "^[A-Z][0-9]$") && block.Box.Height >= candidate.Box.Height * 1.1f;
        // Require a second OCR pass to agree on the label and isolate a small leading glyph.
        return prefix.Length <= 3 && (symbol || mixedGlyph) &&
            offset >= candidate.Box.Height * .4f && offset <= block.Box.Height * 2.5f;
    }
    internal static void Verify()
    {
        var words = new[] {
            new Program.TextLine { Text = "quality", Box = new RectangleF(405, 45, 42, 18) },
            new Program.TextLine { Text = "Pull", Box = new RectangleF(35, 45, 28, 18) },
            new Program.TextLine { Text = "0", Box = new RectangleF(5, 45, 18, 18) },
            new Program.TextLine { Text = "requests", Box = new RectangleF(69, 45, 52, 18) },
            new Program.TextLine { Text = "Actions", Box = new RectangleF(185, 45, 55, 18) },
            new Program.TextLine { Text = "Security", Box = new RectangleF(310, 45, 62, 18) },
            new Program.TextLine { Text = "and", Box = new RectangleF(378, 45, 21, 18) },
            new Program.TextLine { Text = "Repository", Box = new RectangleF(35, 5, 90, 18) }
        };
        var blocks = Group(words);
        if (blocks.Length != 4 || blocks[0].Text != "Repository" || blocks[1].Text != "Pull requests" ||
            blocks[2].Text != "Actions" || blocks[3].Text != "Security and quality" || blocks[1].Box.Left != 35)
            throw new Exception("Spatial reading order or icon filtering failed");
        if (Group(words.Concat(new[] { new Program.TextLine { Text = "丿", Box = new RectangleF(162, 45, 4, 18) } })).Any(x => x.Text.Contains("丿")))
            throw new Exception("Thin navigation icon was treated as text");
        var paragraph = Group(new[] {
            new Program.TextLine { Text = "First paragraph line", Box = new RectangleF(35, 5, 150, 18) },
            new Program.TextLine { Text = "Second paragraph line", Box = new RectangleF(35, 31, 170, 18) }
        });
        if (paragraph.Length != 1 || !paragraph[0].Text.Contains("\n") || paragraph[0].GlyphHeight != 18) throw new Exception("Paragraph grouping lost line breaks or font size");
        var cleaned = Refine(new[] { new Program.TextLine { Text = "0丿 Discussions", Box = new RectangleF(10, 5, 110, 18) } },
            new[] { new Program.TextLine { Text = "Discussions", Box = new RectangleF(22, 5, 98, 18) } });
        if (cleaned[0].Text != "Discussions") throw new Exception("Refined OCR retained a leading icon artifact");
        var icons = Refine(new[] {
            new Program.TextLine { Text = "G2 Agents", Box = new RectangleF(265, 69, 85, 20) },
            new Program.TextLine { Text = "《丿 Discussions", Box = new RectangleF(381, 68, 120, 18) }
        }, new[] {
            new Program.TextLine { Text = "Agents", Box = new RectangleF(296.5f, 71.5f, 54, 18) },
            new Program.TextLine { Text = "Discussions", Box = new RectangleF(413.5f, 71.5f, 87, 14) }
        });
        if (icons[0].Text != "Agents" || icons[1].Text != "Discussions" || icons[0].Box.Left != 296.5f)
            throw new Exception("Refined OCR retained a square navigation icon");
        var label = Refine(new[] { new Program.TextLine { Text = "B2 Agents", Box = new RectangleF(10, 5, 110, 18) } },
            new[] { new Program.TextLine { Text = "Agents", Box = new RectangleF(35, 5, 85, 18) } });
        if (label[0].Text != "B2 Agents") throw new Exception("Refined OCR removed a same-height alphanumeric label");
        var count = Refine(new[] { new Program.TextLine { Text = "499", Box = new RectangleF(160, 5, 25, 11) } },
            new[] { new Program.TextLine { Text = "Pull requests 499", Box = new RectangleF(35, 5, 150, 18) } });
        if (count[0].Text != "499" || count[0].Box.Left != 160) throw new Exception("Refined OCR merged a numeric badge into its neighbour");
    }
}
