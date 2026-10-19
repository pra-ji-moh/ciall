"""
arc_bridge.py -- carries frames and actions between a real ARC-AGI-3 game and
the child. It does no reasoning and makes no choices.

The child is play_arc.exe, in C. This script only:
  - starts a game in the official ARC Prize engine (the arc_agi toolkit)
  - hands the child each frame exactly as the engine gives it
  - hands the engine each action exactly as the child chooses it
  - keeps the child's own account of the game in child/arc_journal.txt

    python arc/arc_bridge.py                  every public game it can find
    python arc/arc_bridge.py ls20 ft09        just these
    python arc/arc_bridge.py --standin        a tiny world in this file, to
                                              prove the link without the toolkit
    python arc/arc_bridge.py --budget 3000    actions the child may spend per game

The protocol (see play_arc.c):
  to the child     OBS <state> <levels> <h> <w> <n> <actions...>  then h rows of hex
  from the child   ACT <n> | RESET | QUIT
"""

import datetime
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_start
light_start.light()   # the drawing library and the web server are not needed to play

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHILD = os.environ.get("CIALL_CHILD") or os.path.join(ROOT, ".build", "play_arc.exe")
JOURNAL = os.path.join(ROOT, "child", "arc_journal.txt")

STATE_PLAYING, STATE_WON, STATE_OVER = 0, 1, 2

HEX = bytes.maketrans(bytes(range(16)), b"0123456789abcdef")


def _rows(grid, h, w):
    """The picture as lines of hex. A whole grid at a time: cell by cell was a third
    of the time a turn took."""
    try:
        import numpy as np
        flat = (np.asarray(grid, dtype=np.uint8) & 15).tobytes().translate(HEX)
        return [flat[r * w:(r + 1) * w].decode("ascii") for r in range(h)]
    except Exception:
        return ["".join("%x" % (int(v) & 15) for v in row) for row in grid]


def encode(state, levels, grid, actions):
    h = len(grid)
    w = len(grid[0]) if h else 0
    head = "OBS %d %d %d %d %d %s" % (state, levels, h, w, len(actions),
                                      " ".join(str(a) for a in actions))
    rows = _rows(grid, h, w)
    return head.strip() + "\n" + "\n".join(rows) + "\n"


# ---- a tiny world, to prove the link ----------------------------------------

class Standin:
    """A corridor maze: body 3, walls 5, ending 4. Three levels."""
    LEVELS = [
        ["WWWWWWWW", "WP....TW", "WWWWWWWW"],
        ["WWWWWW", "WT...W", "WWWW.W", "WP...W", "WWWWWW"],
        ["WWWWWWW", "WP.W..W", "W..W.WW", "W....TW", "WWWWWWW"],
    ]
    MOVES = {1: (-1, 0), 2: (1, 0), 3: (0, -1), 4: (0, 1)}

    def __init__(self):
        self.level = 0
        self.start()

    def start(self):
        rows = self.LEVELS[self.level]
        for r, line in enumerate(rows):
            for c, ch in enumerate(line):
                if ch == "P":
                    self.pr, self.pc = r, c

    def grid(self):
        rows = self.LEVELS[self.level]
        g = [[0] * 24 for _ in range(24)]
        for r, line in enumerate(rows):
            for c, ch in enumerate(line):
                colour = {"W": 5, "T": 4}.get(ch, 0)
                for i in range(2):
                    for j in range(2):
                        g[2 + 2 * r + i][2 + 2 * c + j] = colour
        for i in range(2):
            for j in range(2):
                g[2 + 2 * self.pr + i][2 + 2 * self.pc + j] = 3
        return g

    def observe(self, state):
        return encode(state, self.level, self.grid(), [1, 2, 3, 4])

    def reset(self):
        self.start()
        return self.observe(STATE_PLAYING)

    def step(self, action, x=0, y=0):
        dr, dc = self.MOVES.get(action, (0, 0))
        rows = self.LEVELS[self.level]
        nr, nc = self.pr + dr, self.pc + dc
        if rows[nr][nc] != "W":
            self.pr, self.pc = nr, nc
        if rows[self.pr][self.pc] == "T":
            self.level += 1
            if self.level >= len(self.LEVELS):
                self.level = len(self.LEVELS)
                return encode(STATE_WON, self.level, [[0]], [1, 2, 3, 4])
            self.start()
        return self.observe(STATE_PLAYING)


# ---- the real engine ----------------------------------------------------------

class RealGame:
    """One ARC-AGI-3 game in the official toolkit, spoken to only as frames and actions."""

    def __init__(self, arcade, game_id, baseline=None):
        from arcengine import GameAction
        self.GameAction = GameAction
        self.env = arcade.make(game_id)
        if self.env is None:
            raise RuntimeError("the engine would not make this game")
        self.game_id = game_id
        self.baseline = list(baseline or [])
        self.last = None

    @staticmethod
    def _state_code(state):
        name = getattr(state, "name", str(state)).upper()
        if "WIN" in name:
            return STATE_WON
        if "GAME_OVER" in name or name.endswith("OVER"):
            return STATE_OVER
        return STATE_PLAYING

    @staticmethod
    def _last_grid(frame):
        # a response carries the frames of an animation: the child sees where it came to rest
        if isinstance(frame, (list, tuple)) and frame and getattr(frame[-1], "ndim", 0) == 2:
            frame = frame[-1]
        if hasattr(frame, "tolist"):
            frame = frame.tolist()
        if frame and isinstance(frame[0], list) and frame[0] and isinstance(frame[0][0], list):
            frame = frame[-1]
        return frame

    @staticmethod
    def _first_grid(frame):
        if isinstance(frame, (list, tuple)) and frame:
            first = frame[0]
            first = first.tolist() if hasattr(first, "tolist") else first
            if first and isinstance(first[0], list) and first[0] and not isinstance(first[0][0], list):
                return first
        return None

    @staticmethod
    def _actions(obs):
        out = []
        for a in getattr(obs, "available_actions", []) or []:
            v = getattr(a, "value", a)
            try:
                out.append(int(v))
            except (TypeError, ValueError):
                name = getattr(a, "name", "")
                if name.startswith("ACTION"):
                    out.append(int(name[6:]))
        return out

    def _encode(self, obs):
        # an answer with no frame means nothing was drawn anew: the world is as it was
        if obs is None or not getattr(obs, "frame", None):
            if self.last is None:
                raise RuntimeError("the engine answered with no frame at the start")
            obs = self.last
        before = int(getattr(self.last, "levels_completed", 0)) if self.last is not None else None
        self.last = obs
        grid = self._last_grid(obs.frame)
        text = encode(self._state_code(obs.state), int(getattr(obs, "levels_completed", 0)), grid,
                      self._actions(obs))
        # an act that ended a level: the response animates from the board that act left
        # to the next level. The child is sent the first too, since the board that ended
        # the level is the only evidence there is of what ends one.
        levels = int(getattr(obs, "levels_completed", 0))
        if before is not None and (levels > before or self._state_code(obs.state) == STATE_WON):
            first = self._first_grid(obs.frame)
            if first:
                rows = [bytes((int(v) & 15) for v in row).translate(HEX).decode("ascii") for row in first]
                text = "END %d %d\n%s\n" % (len(first), len(first[0]), "\n".join(rows)) + text
        return text

    def reset(self):
        return self._encode(self.env.reset())

    def step(self, action, x=0, y=0):
        allowed = self._actions(self.last) if self.last is not None else []
        if allowed and action not in allowed:
            return self._encode(self.last)          # not on offer: nothing happens
        act = getattr(self.GameAction, "ACTION%d" % action)
        if action == 6:
            return self._encode(self.env.step(act, data={"x": int(x), "y": int(y)}))
        return self._encode(self.env.step(act))


def public_games(arcade):
    """Every public game, with what a person needed per level. Keyboard games first:
    those are the ones the child can engage with at all today."""
    order = {"keyboard": 0, "keyboard_click": 1, "click": 2}
    infos = arcade.get_environments() or []
    games = []
    for e in infos:
        tags = e.tags or []
        rank = min([order.get(t, 3) for t in tags] or [3])
        games.append((rank, e.game_id, list(e.baseline_actions or []), ",".join(tags) or "untagged"))
    games.sort()
    return [(gid, base, tags) for _, gid, base, tags in games]


# ---- carrying one game ----------------------------------------------------------

def families_elsewhere(mind_dir, game):
    """Family names that survived an ending in any other game's mind: read, not judged."""
    names = set()
    try:
        files = os.listdir(mind_dir)
    except OSError:
        return []
    for name in files:
        if not name.endswith(".txt") or name[:-4] == game:
            continue
        try:
            with open(os.path.join(mind_dir, name), encoding="utf-8") as f:
                for line in f:
                    part = line.split()
                    if len(part) == 3 and part[0] == "family" and part[2] == "1":
                        names.add(part[1])
                    # a theory of three facts that names no colour: it is about the way
                    # worlds can be, not about this game, so another game may hold it
                    if len(part) == 7 and part[0] == "deep":
                        names.add("DEEP:" + ":".join(part[1:]))
        except OSError:
            pass
    return sorted(names)


def _levels_of(text):
    """levels completed, from the OBS line of what was sent to the child"""
    for line in text.split("\n"):
        if line.startswith("OBS "):
            parts = line.split()
            return int(parts[2]), int(parts[1])
    return 0, 0


def _load_solutions(folder, short):
    import json
    try:
        with open(os.path.join(folder, short + ".json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save_solutions(folder, short, found):
    """keep, for each level, the shortest sequence of acts known to win it from its start"""
    import json
    if not found:
        return
    os.makedirs(folder, exist_ok=True)
    have = _load_solutions(folder, short)
    for k, seq in found.items():
        if k not in have or len(seq) < len(have[k]):
            have[k] = seq
    with open(os.path.join(folder, short + ".json"), "w", encoding="utf-8") as f:
        json.dump(have, f)


def _step(game, a):
    return game.step(a[0], a[1], a[2]) if a[0] == 6 else game.step(a[0])


def carry(game, label, budget, journal, mind_dir=None):
    # the child's account goes to a file as it is written, not a pipe read at the end:
    # a pipe that nobody reads fills up, and then the child waits to write while this
    # waits for its next move, and neither ever moves again
    import tempfile
    account = tempfile.TemporaryFile(mode="w+", encoding="utf-8")
    env = dict(os.environ)
    if mind_dir:   # what the child settled about this game is kept here between runs
        os.makedirs(mind_dir, exist_ok=True)
        env["CIALL_MIND"] = os.path.join(mind_dir, getattr(game, "game_id", "standin").split("-")[0] + ".txt")
        if "CIALL_LIVE" not in env:   # the kinds of world other games turned out to be
            env["CIALL_LIVE"] = ",".join(families_elsewhere(mind_dir, getattr(game, "game_id", "standin").split("-")[0]))
    # what it could not account for, and why it believes what it does: a file per game,
    # for its study between games (child/study.py)
    short = getattr(game, "game_id", "standin").split("-")[0]
    env["CIALL_GAME"] = short   # which game this is: kinds of goal carried from OTHER games are read
    for var, folder in (("CIALL_DEADDUMP", "CIALL_DEADDUMP_DIR"), ("CIALL_REASONS", "CIALL_REASONS_DIR")):
        if os.environ.get(folder):
            os.makedirs(os.environ[folder], exist_ok=True)
            env[var] = os.path.join(os.environ[folder], short + (".jsonl" if var == "CIALL_DEADDUMP" else ".txt"))
    obs = game.reset()
    base = getattr(game, "baseline", [])
    # Practice: every winning life is kept, act by act, as the way to win that level from
    # its start (CIALL_SOLUTIONS_DIR); and a practised run plays the ways it knows first
    # (CIALL_PREFIX_DIR), level by level, before the child takes over where they end.
    sol_dir = os.environ.get("CIALL_SOLUTIONS_DIR")
    pre_dir = os.environ.get("CIALL_PREFIX_DIR")
    level, _ = _levels_of(obs)
    life, found, spent = [], {}, {}
    replayed = 0
    if pre_dir:
        known = _load_solutions(pre_dir, short)
        while str(level + 1) in known and replayed < budget:
            seq, began = known[str(level + 1)], level
            for a in seq:
                obs = _step(game, a)
                replayed += 1
                spent[began] = spent.get(began, 0) + 1
                life.append(list(a))
                if _levels_of(obs)[0] > began or _levels_of(obs)[1] != 0:
                    break
            now, state = _levels_of(obs)
            if now > began:
                found[str(began + 1)] = life
                life, level = [], now
                if state != 0:   # the game is won, or over
                    break
            else:
                obs = game.reset()   # the way it knew did not win here: the level from its start, for the child
                level, _ = _levels_of(obs)
                life = []
                break
    # the child plays what is left of the budget, from where the ways it knew ended
    child = subprocess.Popen([CHILD, str(max(1, budget - replayed))], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=account, text=True, cwd=ROOT, env=env)
    child.stdin.write("BASE %d %s\n" % (len(base), " ".join(str(b) for b in base)))
    child.stdin.write(obs)
    child.stdin.flush()
    actions = 0
    trace_path = os.environ.get("CIALL_TRACE")
    trace = open(trace_path, "w", encoding="utf-8") if trace_path else None
    while True:
        line = child.stdout.readline()
        if not line:
            break
        cmd = line.split()
        if not cmd or cmd[0] == "QUIT":
            break
        if cmd[0] == "RESET":
            obs = game.reset()
            life = []   # the level begins again: a new life
        elif cmd[0] == "ACT" and len(cmd) == 2:
            obs = game.step(int(cmd[1]))
            life.append([int(cmd[1]), 0, 0])
        elif cmd[0] == "ACT" and len(cmd) == 4:
            obs = game.step(int(cmd[1]), int(cmd[2]), int(cmd[3]))
            life.append([int(cmd[1]), int(cmd[2]), int(cmd[3])])
        else:
            break
        actions += 1
        if cmd[0] != "RESET":
            spent[level] = spent.get(level, 0) + 1
        now, _ = _levels_of(obs)
        if now > level:
            found[str(level + 1)] = life   # the life that won this level, from its start
            life, level = [], now
        if trace is not None:   # every move and what came of it, for looking back
            trace.write("%s %s\n" % (line.strip(), obs.split("\n", 1)[0]))
        try:
            child.stdin.write(obs)
            child.stdin.flush()
        except (BrokenPipeError, OSError):
            break
    try:
        child.stdin.close()
    except OSError:
        pass
    child.wait()
    if trace is not None:
        trace.close()
    account.seek(0)
    report = account.read()
    account.close()
    if sol_dir:
        _save_solutions(sol_dir, short, found)
    if pre_dir:
        # the levels as they were played, counted here: the ways replayed, then the child
        parts = ["level %d took it %d, a person %d" % (k + 1, spent.get(k, 0), base[k])
                 for k in range(level) if k < len(base)]
        report += "  [practised] played first %d acts it knew\n" % replayed
        report += "  against a person: %s;\n" % "; ".join(parts) if parts else ""
        report += "  levels finished: %d of %d\n" % (level, len(base))
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    entry = "\n%s  %s  (%d actions carried)\n%s" % (stamp, label, actions + replayed, report)
    journal.write(entry)
    journal.flush()
    print(entry)


def main(argv):
    budget = 2000
    mind_dir = None
    games = []
    standin = False
    i = 0
    while i < len(argv):
        if argv[i] == "--budget" and i + 1 < len(argv):
            budget = int(argv[i + 1])
            i += 2
            continue
        if argv[i] == "--mind" and i + 1 < len(argv):
            mind_dir = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--standin":
            standin = True
        else:
            games.append(argv[i])
        i += 1

    if not os.path.exists(CHILD):
        print("the child is not built: run build_c.sh first (%s)" % CHILD)
        return 2
    os.makedirs(os.path.dirname(JOURNAL), exist_ok=True)
    with open(JOURNAL, "a", encoding="utf-8") as journal:
        if standin:
            carry(Standin(), "stand-in corridor world", budget, journal)
            return 0
        import logging
        logging.disable(logging.WARNING)
        import arc_agi
        here = os.path.dirname(os.path.abspath(__file__))
        # The games can be played where they stand, or over the network at
        # three.arcprize.org. Over the network each act costs about 50 milliseconds of
        # waiting, which is nearly all the time a run takes; played where they stand the
        # child is not kept waiting at all. CIALL_ONLINE=1 uses the network, for a run
        # that is to be reported to ARC Prize; otherwise the games are played here.
        # Offline only where the games are actually here. They are not kept in the
        # repository, so a fresh checkout (the cloud) has none, and playing "offline"
        # there plays nothing: that is what stopped the cloud child playing any game for
        # four weeks from 2026-09-24. Without them it goes over the network, as before.
        games_dir = os.path.join(here, "environment_files")
        games_here = os.path.isdir(games_dir) and any(os.scandir(games_dir))
        mode = (arc_agi.OperationMode.OFFLINE if games_here and not os.environ.get("CIALL_ONLINE")
                else arc_agi.OperationMode.ONLINE)
        arcade = arc_agi.Arcade(environments_dir=os.path.join(here, "environment_files"),
                                recordings_dir=os.path.join(here, "recordings"),
                                operation_mode=mode)
        known = public_games(arcade)
        chosen = [g for g in known if not games or g[0] in games or g[0][:4] in games]
        for gid, base, tags in chosen:
            try:
                carry(RealGame(arcade, gid, base), "ARC-AGI-3 game %s (%s)" % (gid, tags), budget, journal, mind_dir)
            except Exception as err:  # a game that will not start is written down, not hidden
                journal.write("\n  ARC-AGI-3 game %s could not be started: %r\n" % (gid, err))
                print("  %s could not be started: %r" % (gid, err))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
