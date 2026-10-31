"""ADR-047 L1 B1+ (opt-in, --news): article text for the story open on the AURA.

The AURA never fetches an article page and never sends a URL. It answers NEWSQ with
"NEWS <idx> <key8> <title>". This helper finds that headline in its OWN copy of the one
allowlisted feed, follows the item's <link> only if it is https on an allowlisted BBC host
under /news/, extracts the first paragraphs, folds to printable ASCII, and caps the text at
479 bytes. TLS certificate verification is the Python default (verified, never disabled).
Redirects are re-checked against the same allowlist.
"""
import html
import html.parser
import re
import ssl
import time
import unicodedata
import urllib.parse
import urllib.request
import zlib

FEED_URL = "https://feeds.bbci.co.uk/news/rss.xml"   # == firmware NEWS_HOST + NEWS_PATH
ARTICLE_HOSTS = {"www.bbc.co.uk", "www.bbc.com", "bbc.co.uk", "bbc.com"}
ARTICLE_PATH_PREFIX = "/news/"
BODY_MAX = 479            # == firmware COMP_NEWSB_MAX (NEWS_BODY_MAX - 1)
FEED_TTL_S = 300
FETCH_TIMEOUT_S = 8
MAX_DOWNLOAD = 2 * 1024 * 1024
UA = "AURA-companion/047"

_PUNCT = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-", "\u2026": "...",
          "\u00a0": " "}


def ascii_fold(s):
    s = "".join(_PUNCT.get(c, c) for c in s)
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
    return re.sub(r"\s+", " ", "".join(c if 0x20 <= ord(c) <= 0x7E else " " for c in s)).strip()


def title_norm(s):
    """Loose match key: the device folds UTF-8 its own way and may end with '...'; compare alnum only."""
    s = ascii_fold(html.unescape(s)).lower()
    s = re.sub(r"\.\.\.$|\.$", "", s)
    return re.sub(r"[^a-z0-9]+", "", s)


def allowed_article(url):
    try:
        u = urllib.parse.urlsplit(url)
    except ValueError:
        return False
    return (u.scheme == "https" and (u.hostname or "").lower() in ARTICLE_HOSTS and u.port in (None, 443)
            and u.path.startswith(ARTICLE_PATH_PREFIX) and not u.username and not u.password)


class _AllowRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not allowed_article(newurl) and newurl != FEED_URL:
            raise urllib.error.HTTPError(newurl, code, "redirect off allowlist", headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


_OPENER = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ssl.create_default_context()), _AllowRedirect())


def _get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Encoding": "identity"})
    with _OPENER.open(req, timeout=FETCH_TIMEOUT_S) as r:
        data = r.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise ValueError("response too large")
    return data.decode("utf-8", "replace")


class _Paras(html.parser.HTMLParser):
    """Text of <p> elements (BBC article text-blocks), skipping script/style/nav/figure/aside/footer."""
    SKIP = {"script", "style", "nav", "figure", "figcaption", "aside", "footer", "header", "noscript"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.paras, self._buf, self._in_p, self._skip = [], [], 0, 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self._skip += 1
        elif tag == "p" and not self._skip:
            self._in_p += 1
            self._buf = []

    def handle_endtag(self, tag):
        if tag in self.SKIP and self._skip:
            self._skip -= 1
        elif tag == "p" and self._in_p:
            self._in_p -= 1
            t = ascii_fold("".join(self._buf))
            if len(t) >= 40:
                self.paras.append(t)

    def handle_data(self, data):
        if self._in_p and not self._skip:
            self._buf.append(data)


def extract_text(page_html, cap=BODY_MAX):
    p = _Paras()
    p.feed(page_html)
    out = ""
    for para in p.paras:
        cand = (out + "\n" + para) if out else para  # \n = paragraph break on the AURA reader
        if len(cand) <= cap:
            out = cand
            continue
        if not out:  # first paragraph alone is too long: cut on a word boundary
            cut = para[: cap - 3].rsplit(" ", 1)[0]
            out = cut + "..."
        break
    return out[:cap]


class NewsBody:
    def __init__(self):
        self._feed, self._feed_at = [], 0.0
        self._done = {}   # key -> time served (or failed), avoids re-fetching the same story every poll

    def _items(self):
        if time.time() - self._feed_at > FEED_TTL_S or not self._feed:
            x = _get(FEED_URL)
            items = []
            for it in re.findall(r"<item>(.*?)</item>", x, re.S):
                t = re.search(r"<title>(?:<!\[CDATA\[)?(.*?)(?:\]\]>)?</title>", it, re.S)
                l = re.search(r"<link>(?:<!\[CDATA\[)?(.*?)(?:\]\]>)?</link>", it, re.S)
                if t and l:
                    items.append((title_norm(t.group(1)), html.unescape(l.group(1).strip())))
            self._feed, self._feed_at = items, time.time()
        return self._feed

    def wanted(self, key):
        t = self._done.get(key)
        return t is None or time.time() - t > 600

    def body_for(self, key, title):
        """Return printable-ASCII text (<= 479 B) or "" if the story or its text cannot be found."""
        self._done[key] = time.time()
        want = title_norm(title)
        if not want:
            return ""
        link = next((l for (t, l) in self._items() if t == want or (len(want) > 20 and t.startswith(want))), None)
        if not link or not allowed_article(link):
            return ""
        return extract_text(_get(link))


def newsb_frame(key_hex, text):
    text = "".join(c if (0x20 <= ord(c) <= 0x7E or c == "\n") else " " for c in text)
    b = text.encode("ascii", "replace")[:BODY_MAX]
    return ("NEWSB %s %d %08x\n" % (key_hex, len(b), zlib.crc32(b) & 0xFFFFFFFF)).encode("ascii") + b
