"""
fetch_books.py -- the child's books, fetched from Hugging Face. Relay only: it
reads nothing and decides nothing, it only writes stories one per line.

    python read/fetch_books.py [count] [offset]

Source: the public dataset roneneldan/TinyStories (stories in the words a three
or four year old knows), through Hugging Face's dataset viewer API, 100 rows a call.
"""
import json, os, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "books", "tinystories.txt")
URL = ("https://datasets-server.huggingface.co/rows?dataset=roneneldan%2FTinyStories"
       "&config=default&split=train&offset={}&length=100")

def main(argv):
    if argv and argv[0] == "more":   # the next stories after the ones already on the shelf
        have = 0
        if os.path.exists(OUT):
            with open(OUT, encoding="utf-8") as f:
                have = sum(1 for _ in f)
        argv = [argv[1] if len(argv) > 1 else "500", str(have)]
        if have == 0:
            argv[0] = "2000"
    count = int(argv[0]) if argv else 2000
    start = int(argv[1]) if len(argv) > 1 else 0
    got = []
    for off in range(start, start + count, 100):
        with urllib.request.urlopen(URL.format(off), timeout=60) as r:
            rows = json.load(r)["rows"]
        for row in rows:
            text = " ".join(row["row"]["text"].split())
            if text:
                got.append(text)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "a" if start > 0 else "w", encoding="utf-8") as f:
        f.write("\n".join(got) + "\n")
    print("%d stories, %d bytes -> %s" % (len(got), os.path.getsize(OUT), OUT))

if __name__ == "__main__":
    main(sys.argv[1:])
