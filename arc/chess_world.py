"""
chess_world.py -- chess, offered to the child in exactly the way an ARC-AGI-3 game is:
a grid of coloured cells, and one act, pointing at a place.

Nothing is explained to it. It is not told that the board is 8 by 8, which colours
are its own, that pointing at a piece takes hold of it and pointing again moves it,
that a move may be refused, or what ends a level. All of that it must rule out for
itself from what happens, the same way it does with a real game.

The rules here are written in full (castling, en passant, promotion, check,
checkmate, stalemate) so that what it is playing is chess and not an imitation.
The opponent plays a legal move at random.

Four levels, so there is a ladder to climb rather than one wall:
    1  make any legal move
    2  take any piece of the other colour
    3  give check
    4  checkmate

    python arc/chess_world.py --budget 4000

What a person needs, for the comparison the child reports against, is set from
what these take a beginner who can see the board: a few moves each.
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arc_bridge   # noqa: E402  (the same carrier the real games use; it only relays)

EMPTY_DARK, EMPTY_LIGHT = 0, 1
WHITE_PIECES = {"P": 2, "N": 3, "B": 4, "R": 5, "Q": 6, "K": 7}
BLACK_PIECES = {"p": 8, "n": 9, "b": 10, "r": 11, "q": 12, "k": 13}
HELD = 14          # the square of the piece it has taken hold of
START = "rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR"


def on_board(r, c):
    return 0 <= r < 8 and 0 <= c < 8


class Position:
    """A chess position, and every legal move from it."""

    def __init__(self):
        self.sq = list(START)
        self.white = True
        self.castle = {"K": True, "Q": True, "k": True, "q": True}
        self.passing = None      # the square a pawn may be taken on in passing
        self.halfmoves = 0
        self._legal = None

    def copy(self):
        p = Position()
        p.sq = list(self.sq)
        p.white = self.white
        p.castle = dict(self.castle)
        p.passing = self.passing
        p.halfmoves = self.halfmoves
        p._legal = None
        return p

    def at(self, r, c):
        return self.sq[r * 8 + c]

    def put(self, r, c, ch):
        self.sq[r * 8 + c] = ch

    def mine(self, ch):
        return ch != "." and (ch.isupper() if self.white else ch.islower())

    def theirs(self, ch):
        return ch != "." and (ch.islower() if self.white else ch.isupper())

    # ---- how each piece may go ------------------------------------------------

    def _raw_moves(self, white):
        """Every move ignoring whether it leaves the king attacked."""
        out = []
        back = 7 if white else 0
        forward = -1 if white else 1
        for r in range(8):
            for c in range(8):
                ch = self.at(r, c)
                if ch == "." or (ch.isupper() != white):
                    continue
                k = ch.upper()
                if k == "P":
                    one = r + forward
                    if on_board(one, c) and self.at(one, c) == ".":
                        out.append((r, c, one, c))
                        two = r + 2 * forward
                        if r == (6 if white else 1) and self.at(two, c) == ".":
                            out.append((r, c, two, c))
                    for dc in (-1, 1):
                        if not on_board(one, c + dc):
                            continue
                        target = self.at(one, c + dc)
                        if target != "." and (target.isupper() != white):
                            out.append((r, c, one, c + dc))
                        elif self.passing == (one, c + dc):
                            out.append((r, c, one, c + dc))
                elif k == "N":
                    for dr, dc in ((1, 2), (2, 1), (-1, 2), (-2, 1), (1, -2), (2, -1), (-1, -2), (-2, -1)):
                        nr, nc = r + dr, c + dc
                        if on_board(nr, nc) and not (self.at(nr, nc) != "." and self.at(nr, nc).isupper() == white):
                            out.append((r, c, nr, nc))
                elif k in ("B", "R", "Q"):
                    ways = []
                    if k in ("B", "Q"):
                        ways += [(1, 1), (1, -1), (-1, 1), (-1, -1)]
                    if k in ("R", "Q"):
                        ways += [(1, 0), (-1, 0), (0, 1), (0, -1)]
                    for dr, dc in ways:
                        nr, nc = r + dr, c + dc
                        while on_board(nr, nc):
                            t = self.at(nr, nc)
                            if t != "." and t.isupper() == white:
                                break
                            out.append((r, c, nr, nc))
                            if t != ".":
                                break
                            nr, nc = nr + dr, nc + dc
                elif k == "K":
                    for dr in (-1, 0, 1):
                        for dc in (-1, 0, 1):
                            if dr == 0 and dc == 0:
                                continue
                            nr, nc = r + dr, c + dc
                            if on_board(nr, nc) and not (self.at(nr, nc) != "." and self.at(nr, nc).isupper() == white):
                                out.append((r, c, nr, nc))
                    # castling: king and rook unmoved, squares empty, king not through check
                    side = "K" if white else "k"
                    other = "Q" if white else "q"
                    if self.castle[side] and all(self.at(back, x) == "." for x in (5, 6)):
                        if not self.attacked(back, 4, not white) and not self.attacked(back, 5, not white):
                            out.append((r, c, back, 6))
                    if self.castle[other] and all(self.at(back, x) == "." for x in (1, 2, 3)):
                        if not self.attacked(back, 4, not white) and not self.attacked(back, 3, not white):
                            out.append((r, c, back, 2))
        return out

    def attacked(self, r, c, by_white):
        for (_, _, tr, tc) in self._raw_moves(by_white):
            if (tr, tc) == (r, c):
                return True
        return False

    def king(self, white):
        ch = "K" if white else "k"
        for i, s in enumerate(self.sq):
            if s == ch:
                return i // 8, i % 8
        return None

    def legal_moves(self):
        if getattr(self, "_legal", None) is not None:
            return self._legal
        out = []
        for m in self._raw_moves(self.white):
            after = self.copy()
            after.play(m)
            k = after.king(self.white)
            if k is None or not after.attacked(k[0], k[1], not self.white):
                out.append(m)
        self._legal = out
        return out

    def play(self, m):
        self._legal = None
        r, c, nr, nc = m
        ch = self.at(r, c)
        k = ch.upper()
        taken = self.at(nr, nc) != "."
        # en passant capture
        if k == "P" and self.passing == (nr, nc) and self.at(nr, nc) == ".":
            self.put(r, nc, ".")
            taken = True
        # rook alongside a castling king
        if k == "K" and abs(nc - c) == 2:
            back = r
            if nc == 6:
                self.put(back, 5, self.at(back, 7))
                self.put(back, 7, ".")
            else:
                self.put(back, 3, self.at(back, 0))
                self.put(back, 0, ".")
        self.put(nr, nc, ch)
        self.put(r, c, ".")
        # promotion, always to a queen: the child is not asked to choose
        if k == "P" and nr in (0, 7):
            self.put(nr, nc, "Q" if ch.isupper() else "q")
        # what may no longer castle
        if k == "K":
            if ch.isupper():
                self.castle["K"] = self.castle["Q"] = False
            else:
                self.castle["k"] = self.castle["q"] = False
        if k == "R":
            if (r, c) == (7, 0): self.castle["Q"] = False
            if (r, c) == (7, 7): self.castle["K"] = False
            if (r, c) == (0, 0): self.castle["q"] = False
            if (r, c) == (0, 7): self.castle["k"] = False
        self.passing = None
        if k == "P" and abs(nr - r) == 2:
            self.passing = ((r + nr) // 2, c)
        self.halfmoves = 0 if (k == "P" or taken) else self.halfmoves + 1
        self.white = not self.white
        return taken

    def in_check(self, white):
        k = self.king(white)
        return k is not None and self.attacked(k[0], k[1], not white)


class Chess:
    """Chess as the child meets it: frames of colours, and pointing."""

    LEVELS = ("make a move", "take a piece", "give check", "checkmate")
    baseline = [2, 8, 12, 40]    # clicks a beginner who can see the board would need

    def __init__(self, seed=1):
        self.game_id = "chess-ladder"
        self.rng = random.Random(seed)
        self.level = 0
        self.start()

    def start(self):
        self.pos = Position()
        self.held = None
        self.moves = 0

    # ---- what it sees ---------------------------------------------------------

    def grid(self):
        g = [[0] * 64 for _ in range(64)]
        for r in range(8):
            for c in range(8):
                ch = self.pos.at(r, c)
                base = EMPTY_LIGHT if (r + c) % 2 == 0 else EMPTY_DARK
                colour = WHITE_PIECES.get(ch, BLACK_PIECES.get(ch, base))
                for i in range(8):
                    for j in range(8):
                        rr, cc = r * 8 + i, c * 8 + j
                        edge = i == 0 or j == 0 or i == 7 or j == 7
                        if self.held == (r, c) and edge:
                            g[rr][cc] = HELD
                        else:
                            g[rr][cc] = colour
        return g

    def observe(self, state):
        return arc_bridge.encode(state, self.level, self.grid(), [6])

    def reset(self):
        self.start()
        return self.observe(arc_bridge.STATE_PLAYING)

    # ---- what it does ---------------------------------------------------------

    def step(self, action, x=0, y=0):
        if action != 6:
            return self.observe(arc_bridge.STATE_PLAYING)
        r, c = min(7, y // 8), min(7, x // 8)
        ch = self.pos.at(r, c)
        if self.held is None:
            # taking hold of one of its own pieces; anything else does nothing
            if self.pos.mine(ch):
                self.held = (r, c)
            return self.observe(arc_bridge.STATE_PLAYING)
        move = (self.held[0], self.held[1], r, c)
        if move not in self.pos.legal_moves():
            self.held = None if (r, c) == self.held else self.held
            if self.pos.mine(ch):
                self.held = (r, c)      # taking hold of another piece instead
            return self.observe(arc_bridge.STATE_PLAYING)

        took = self.pos.play(move)
        self.held = None
        self.moves += 1
        done = self.level == 0                      # a legal move at all
        if self.level == 1 and took:
            done = True
        if self.level == 2 and not self.pos.in_check(True) and self.pos.in_check(False):
            done = True
        theirs = self.pos.legal_moves()
        if self.level == 3 and self.pos.in_check(False) and not theirs:
            done = True
        if done:
            self.level += 1
            if self.level >= len(self.LEVELS):
                return arc_bridge.encode(arc_bridge.STATE_WON, self.level, self.grid(), [6])
            self.start()
            return self.observe(arc_bridge.STATE_PLAYING)
        # the opponent answers, at random among its legal moves
        if theirs:
            self.pos.play(self.rng.choice(theirs))
        lost = (self.pos.in_check(True) and not self.pos.legal_moves()) or self.moves > 120
        if lost:
            self.start()
            return self.observe(arc_bridge.STATE_OVER)
        return self.observe(arc_bridge.STATE_PLAYING)


def main(argv):
    budget = 4000
    seed = 1
    i = 0
    while i < len(argv):
        if argv[i] == "--budget" and i + 1 < len(argv):
            budget = int(argv[i + 1]); i += 2; continue
        if argv[i] == "--seed" and i + 1 < len(argv):
            seed = int(argv[i + 1]); i += 2; continue
        i += 1
    os.makedirs(os.path.dirname(arc_bridge.JOURNAL), exist_ok=True)
    with open(arc_bridge.JOURNAL, "a", encoding="utf-8") as journal:
        arc_bridge.carry(Chess(seed), "chess (take a piece, give check, checkmate)", budget, journal)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
