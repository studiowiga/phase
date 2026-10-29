# PHASE 90 (Studio Wiga) - notice PDF bilingue (anglais complet puis francais complet).
# Usage : python3 tools/manual.py <fichier PDF de sortie>
# Illustrations : rendus reels du plugin et courbes du moteur, dans tools/manual/ (visuel inchange depuis la v1.1).
import os, sys
import numpy as np
from PIL import Image
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, CondPageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, "tools", "manual")
OUT = sys.argv[1]
TMP = os.path.join(os.path.dirname(os.path.abspath(OUT)), "_manual_tmp"); os.makedirs(TMP, exist_ok=True)
VERSION = next(l.split("VERSION")[1].split()[0] for l in open(os.path.join(ROOT, "CMakeLists.txt")) if l.startswith("project("))

LIB = "/usr/share/fonts/truetype/liberation/LiberationSans-"
for n, f in (("R", "Regular"), ("B", "Bold"), ("I", "Italic"), ("BI", "BoldItalic")):
    pdfmetrics.registerFont(TTFont(n, LIB + f + ".ttf"))
pdfmetrics.registerFontFamily("R", normal="R", bold="B", italic="I", boldItalic="BI")

ORANGE = colors.HexColor("#E8641B"); DARK = colors.HexColor("#1E1E1E"); GREY = colors.HexColor("#6B6B6B"); LIGHT = colors.HexColor("#F4EFEA")
W = A4[0] - 36 * mm

# couverture : pedale detouree (rendu reel), reflet au sol, halo orange tres doux
cut = Image.open(os.path.join(A, "pedal_cutout.png")).convert("RGBA"); cut = cut.crop(cut.getbbox()); cw, ch = cut.size; pad = int(cw * 0.18)
cv = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.32) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:cv.height, 0:cv.width].astype(np.float64)
glow = np.exp(-(((xx - cv.width / 2) / (cw * 0.62)) ** 2 + ((yy - (pad + ch * 0.45)) / (ch * 0.5)) ** 2)) * 34
edge = np.clip(np.minimum.reduce([xx, cv.width - 1 - xx, yy, cv.height - 1 - yy]) / (pad * 0.9), 0, 1); glow *= edge ** 2
cv = Image.alpha_composite(cv, Image.fromarray(np.dstack([glow * 0.95, glow * 0.45, glow * 0.12, np.full_like(glow, 255)]).astype(np.uint8), "RGBA"))
refl = np.array(cut.transpose(Image.FLIP_TOP_BOTTOM)).astype(np.float64)
refl[..., 3] *= (np.clip(1 - np.arange(ch) / (ch * 0.22), 0, 1) ** 1.6 * 0.22)[:, None]
cv.alpha_composite(Image.fromarray(refl.astype(np.uint8), "RGBA"), (pad, pad + ch + 4)); cv.alpha_composite(cut, (pad, pad))
cv = cv.crop((0, 0, cv.width, pad + int(ch * 1.2)))
cover_img = os.path.join(TMP, "cover.png"); cv.convert("RGB").save(cover_img)

def img(path, width):
    im = Image.open(path); return RLImage(path, width=width, height=width * im.height / im.width)
def I_(name, width): return img(os.path.join(A, name), width)

st = {
    "h1": ParagraphStyle("h1", fontName="B", fontSize=15, leading=19, textColor=ORANGE, spaceBefore=9, spaceAfter=6, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="B", fontSize=10.5, leading=14, textColor=ORANGE, spaceBefore=6, spaceAfter=3, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="R", fontSize=9, leading=13, textColor=DARK, spaceAfter=5),
    "cap": ParagraphStyle("cap", fontName="I", fontSize=7.6, leading=10, textColor=GREY, alignment=TA_CENTER, spaceBefore=2, spaceAfter=6),
    "cell": ParagraphStyle("cell", fontName="R", fontSize=8.2, leading=10.8, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="B", fontSize=8.2, leading=10.8, textColor=DARK),
    "code": ParagraphStyle("code", fontName="R", fontSize=8, leading=11, textColor=DARK, backColor=LIGHT, borderPadding=4, spaceBefore=4, spaceAfter=8),
    "cover": ParagraphStyle("cover", fontName="B", fontSize=30, leading=36, textColor=colors.white, alignment=TA_CENTER),
    "coversub": ParagraphStyle("coversub", fontName="R", fontSize=11, leading=15, textColor=colors.HexColor("#F29A5E"), alignment=TA_CENTER),
    "coverver": ParagraphStyle("coverver", fontName="R", fontSize=10, leading=14, textColor=colors.HexColor("#D6D6D6"), alignment=TA_CENTER),
    "coverlang": ParagraphStyle("coverlang", fontName="B", fontSize=8.5, leading=11, textColor=colors.HexColor("#F29A5E"), alignment=TA_CENTER),
}
def P(t, s="p"): return Paragraph(t, st[s])
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) or ci == 0 else "cell") for ci, c in enumerate(row)] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    s = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#DDD5CC")),
         ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: s += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ORANGE)]
    t.setStyle(TableStyle(s)); return t
def side(left, right, lw):
    t = Table([[left, right]], colWidths=[lw, W - lw]); t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    return t

PRESETS = [("Classic Block", "BLOCK", 0.92), ("Script '74", "SCRIPT", 0.70), ("Eruption", "SCRIPT", 0.28), ("Brown Sound", "BLOCK", 0.40),
           ("Slow Jet", "BLOCK", 0.12), ("Funk Rhythm", "BLOCK", 2.20), ("Fast Warble", "SCRIPT", 4.40), ("Rotary Fast", "BLOCK", 5.10),
           ("Bass Swirl", "SCRIPT", 0.50), ("Tempo 1/4", "BLOCK · SYNC 1/4", None), ("Tempo 1/8 Funk", "BLOCK · SYNC 1/8", None),
           ("Tempo 1/2 Swell", "SCRIPT · SYNC 1/2", None)]

from manual_text import T

def section(lang):
    t = T[lang]; f = [NextPageTemplate("p" if lang == "en" else "p_fr")]
    f += [P(t["lang"], "coverlang"), Spacer(1, 6 * mm), img(cover_img, W * 0.56), Spacer(1, 4 * mm), P("PHASE 90", "cover"), Spacer(1, 2 * mm),
          P(t["sub"], "coversub"), Spacer(1, 1 * mm), P(t["ver"], "coverver"), Spacer(1, 8 * mm), I_("logo_white.png", 46 * mm), PageBreak()]
    f.append(P(t["s1"], "h1")); f += [P(x) for x in t["intro"]]
    f.append(P(t["s2"], "h1")); f.append(table(t["inst"], [22 * mm, W - 22 * mm], header=False))
    f.append(P('xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Phase 90.vst3"', "code")); f.append(P(t["inst_after"]))
    f.append(P(t["s3"], "h1"))
    f.append(side([I_("overview.png", 70 * mm), P(t["ov_cap"], "cap")], table(t["ov"], [26 * mm, W - 74 * mm - 26 * mm], header=False), 74 * mm))
    f.append(PageBreak())
    f.append(P(t["s4"], "h1")); f.append(P("SPEED", "h2")); f += [P(x) for x in t["speed"]]
    ch = Table([[[I_("chart_speed.png", W * 0.47), P(t["cap_speed"], "cap")], [I_("chart_notch.png", W * 0.47), P(t["cap_notch"], "cap")]]], colWidths=[W / 2, W / 2])
    f.append(ch); f.append(P(t["swoosh"]))
    f.append(P(t["badge_h"], "h2")); f += [P(x) for x in t["badge"]]
    f.append(side([I_("chart_response.png", W * 0.48), P(t["cap_resp"], "cap")], [I_("status.png", W * 0.44), P(t["cap_status"], "cap")], W * 0.52))
    f.append(KeepTogether([P(t["sync_h"], "h2"), P(t["sync_intro"]), table(t["sync_rows"], [24 * mm, W - 24 * mm], header=False),
                           I_("sync_states.png", W * 0.62), P(t["cap_sync"], "cap")])); f.append(P(t["sync_more"]))
    f.append(P(t["s5"], "h1"))
    f.append(side([P(t["foot"])], [I_("footswitch.png", 58 * mm), P("ON / BYPASS", "cap")], W - 62 * mm))
    f.append(side([P(t["ticks"])], [I_("ticks.png", 58 * mm), P("ON / BYPASS", "cap")], W - 62 * mm))
    f.append(P(t["stereo"]))
    f.append(CondPageBreak(120 * mm))
    f.append(P(t["s6"], "h1")); f.append(P(t["midi_intro"]))
    f.append(table(t["midi"], [30 * mm, 24 * mm, W - 54 * mm]))
    f.append(P(t["learn_h"], "h2")); f += [P(x) for x in t["learn"]]
    f.append(P(t["midimenu_h"], "h2")); f += [P(x) for x in t["midimenu"]]
    f.append(P(t["s7"], "h1")); f += [P(x) for x in t["win"]]
    f.append(table(t["msgs"], [52 * mm, W - 52 * mm], header=False))
    f.append(CondPageBreak(110 * mm))
    f.append(P(t["s8"], "h1")); f.append(P(t["pre_intro"]))
    rows = [t["pre_hdr"]] + [[n, v, ("%.2f" % hz).replace(".", "," if lang == "fr" else ".") if hz else "tempo", t["pre"][i]] for i, (n, v, hz) in enumerate(PRESETS)]
    f.append(table(rows, [32 * mm, 30 * mm, 14 * mm, W - 76 * mm]))
    f.append(P(t["s9"], "h1")); f += [P(x) for x in t["tips"]]
    f.append(KeepTogether([P(t["s10"], "h1"), table(t["spec"], [32 * mm, W - 32 * mm], header=False), Spacer(1, 4 * mm), P(t["sources"], "cap")]))
    f.append(PageBreak())
    return f

def deco(lang):
    def _d(c, doc):
        c.saveState()
        c.setStrokeColor(colors.HexColor("#DDD5CC")); c.setLineWidth(0.6); c.line(18 * mm, A4[1] - 12 * mm, A4[0] - 18 * mm, A4[1] - 12 * mm)
        c.setFillColor(ORANGE); c.rect(0, A4[1] - 2 * mm, A4[0], 2 * mm, stroke=0, fill=1)
        c.setFont("R", 7.5); c.setFillColor(GREY)
        c.drawString(18 * mm, 10 * mm, "Phase 90 v" + VERSION + " · Studio Wiga")
        c.drawRightString(A4[0] - 18 * mm, 10 * mm, ("English" if lang == "en" else "Français") + " · " + str(doc.page)); c.restoreState()
    return _d
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ORANGE); c.rect(0, A4[1] - 3 * mm, A4[0], 3 * mm, stroke=0, fill=1); c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=17 * mm, bottomMargin=17 * mm,
                      title="Phase 90 v" + VERSION + " — Manual / Notice", author="Studio Wiga")
fr_ = lambda i: Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id=i)
doc.addPageTemplates([PageTemplate(id="cover", frames=[fr_("c")], onPage=cover_bg), PageTemplate(id="p", frames=[fr_("p")], onPage=deco("en")),
                      PageTemplate(id="cover_fr", frames=[fr_("c2")], onPage=cover_bg), PageTemplate(id="p_fr", frames=[fr_("p2")], onPage=deco("fr"))])
en = section("en"); fr = section("fr")
en[-1:] = [NextPageTemplate("cover_fr"), PageBreak()]
doc.build(en + fr[:-1])
print(OUT, VERSION)
