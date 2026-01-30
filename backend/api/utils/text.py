import re

FACT_STARTERS = [
    "siapa",
    "kapan",
    "dimana",
    "di mana",
    "apa",
    "mengapa",
    "kenapa",
    "bagaimana",
]

FACT_KEYWORDS = [
    "bulan",
    "matahari",
    "bumi",
    "bintang",
    "tata surya",
    "presiden",
    "wakil presiden",
    "gubernur",
    "wali kota",
    "walikota",
    "ibukota",
    "ibu kota",
    "negara",
    "benua",
    "planet",
    "samudra",
    "laut",
    "gunung",
    "sungai",
    "kota",
    "provinsi",
    "tahun berapa",
    "tanggal berapa",
    "umur",
    "lahir",
    "meninggal",
    "sekarang",
    "saat ini",
]


def is_mostly_math(text: str) -> bool:
    stripped = text.replace(" ", "")
    if not stripped:
        return False
    math_chars = re.findall(r"[0-9\+\-\*x\/\(\)\=\^]", stripped)
    ratio = len(math_chars) / len(stripped)
    return ratio > 0.5


def is_fact_question(text: str) -> bool:
    lower = text.lower().strip()

    if "?" not in lower and not any(lower.startswith(s) for s in FACT_STARTERS):
        return False

    words = lower.split()
    first_word = words[0] if words else ""

    if first_word in FACT_STARTERS and first_word not in ["apa"]:
        return True

    if first_word == "apa" and any(kw in lower for kw in FACT_KEYWORDS):
        return True

    if any(kw in lower for kw in FACT_KEYWORDS):
        return True

    return False
