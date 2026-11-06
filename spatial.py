"""OCR word grouping keeps separate navigation labels and numeric badges."""
import re
import statistics


def union(boxes):
    left = min(b[0] for b in boxes)
    top = min(b[1] for b in boxes)
    right = max(b[0] + b[2] for b in boxes)
    bottom = max(b[1] + b[3] for b in boxes)
    return [left, top, right - left, bottom - top]


def cjk(char):
    return "\u2e80" <= char <= "\u9fff" or "\uf900" <= char <= "\ufaff"


def refine(blocks, candidates):
    """Use the best overlapping second pass; preserve badges and genuine short prefixes."""
    output = []
    for block in blocks:
        if re.fullmatch(r"\d+(?:[.,]\d+)*(?:[%+])?", block["text"].strip()):
            output.append(block)
            continue
        x, y, width, height = block["box"]
        def overlap(other):
            a, b, c, d = other["box"]
            return max(0, min(x + width, a + c) - max(x, a)) * max(0, min(y + height, b + d) - max(y, b))
        candidate = max(candidates, key=overlap, default=None)
        replacement = block
        if candidate:
            a, b, c, d = candidate["box"]
            area = overlap(candidate)
            prefix = block["text"][:-len(candidate["text"])].strip() if candidate["text"] else ""
            symbol = prefix and all(not ch.isalpha() or ch in "\u4e3f\u4e28" for ch in prefix)
            mixed = bool(re.fullmatch("[A-Z][0-9]", prefix)) and height >= d * 1.1
            cleaner = (bool(re.match("^[A-Za-z]", candidate["text"])) and
                       block["text"].lower().endswith(candidate["text"].lower()) and
                       len(prefix) <= 3 and (symbol or mixed) and d * .4 <= a - x <= height * 2.5)
            more_letters = sum(ch.isalpha() for ch in candidate["text"]) > sum(ch.isalpha() for ch in block["text"])
            if area > width * height * .5 and area > c * d * .5 and (cleaner or more_letters):
                replacement = candidate
        output.append(replacement)
    return output or candidates


def group(words):
    rows = []
    for word in sorted(words, key=lambda w: (w["box"][1], w["box"][0])):
        x, y, width, height = word["box"]
        if not word["text"] or width <= 0 or height <= 0:
            continue
        for row in rows:
            _, top, _, h = row[0]["box"]
            if min(top + h, y + height) - max(top, y) >= min(h, height) * .5:
                row.append(word)
                break
        else:
            rows.append([word])
    output, paragraph = [], None
    for row in sorted(rows, key=lambda r: min(w["box"][1] for w in r)):
        ordered = sorted(row, key=lambda w: w["box"][0])
        height = statistics.median(w["box"][3] for w in row)
        gap_limit = max(14, height * 1.2)
        gaps = sum(b["box"][0] - a["box"][0] - a["box"][2] > gap_limit for a, b in zip(ordered, ordered[1:]))
        text = "".join(w["text"] for w in row)
        navigation = gaps >= 2 and len(re.findall("[A-Za-z]", text)) > len(re.findall("[\u3400-\u9fff]", text)) * 3
        blocks, run = [], []

        def flush():
            if not run:
                return
            text, previous = "", None
            for word in run:
                if previous and not (cjk(text[-1]) and cjk(word["text"][0])) and word["box"][0] - previous["box"][0] - previous["box"][2] > max(1, min(previous["box"][3], word["box"][3]) * .16):
                    text += " "
                text += word["text"]
                previous = word
            blocks.append({"text": text, "box": union([w["box"] for w in run]), "glyphHeight": height})
            run.clear()

        for i, word in enumerate(ordered):
            x, y, width, h = word["box"]
            nxt = ordered[i + 1] if i + 1 < len(ordered) else None
            symbol = not re.search("[A-Za-z]", word["text"])
            square = h * .65 <= width <= h * 1.6 or bool(re.fullmatch("[0\u4e3f\u4e28]+", word["text"]))
            if navigation and square and symbol and len(word["text"]) <= 3 and nxt and re.match("[A-Za-z]", nxt["text"]) and (not i or x - ordered[i - 1]["box"][0] - ordered[i - 1]["box"][2] > height):
                flush()
                continue
            if run and x - run[-1]["box"][0] - run[-1]["box"][2] > gap_limit:
                flush()
            run.append(word)
        flush()
        if len(blocks) == 1:
            block = blocks[0]
            list_item = re.match(r"^(?:[-*]|\d+[.)])\s", block["text"])
            if paragraph and not list_item and abs(paragraph["box"][0] - block["box"][0]) <= height * .6 and -2 <= block["box"][1] - paragraph["box"][1] - paragraph["box"][3] <= max(8, height) and abs(paragraph["glyphHeight"] - height) <= 3:
                paragraph["text"] += "\n" + block["text"]
                paragraph["box"] = union([paragraph["box"], block["box"]])
                continue
            paragraph = None if list_item else block
        else:
            paragraph = None
        output.extend(blocks)
    return output
