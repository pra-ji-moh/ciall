# python-reference

The Python this directory was verified with before a C compiler existed.
Everything here has been replaced by C in the folder above, and kept as a
second, independent implementation to check the C against. It is not the
product and nothing depends on it at build time: `../../build_c.sh` needs
no Python.

| Python | Replaced by |
|---|---|
| kernel_py.py, verify_reason.py | smarsh_reason.c, test_smarsh_reason.c |
| verify_logic.py | test_smarsh_core.c (verify_logic was its stand-in) |
| baby_auto.py, verify_auto.py | smarsh_learner.c, test_learner.c, demo_learner.c |
| baby.py, demo_desk.py, demo_program.py | demo_baby.c, demo_desk.c, demo_program.c |
| verify_lexer.py, verify_parser.py, verify_interp.py | test_frontend.c |

The two harnesses still need both sides, so they run from here:

    python diff_kernel.py  > py.txt    # compare with ../diff_kernel.c output
    python diff_learner.py > py.txt    # compare with ../diff_learner.c output
