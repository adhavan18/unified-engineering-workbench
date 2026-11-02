# ownership statement

**reused third-party code:** none. there are no external c++ or javascript libraries. only the standard
library, the windows api and the browser's built-in webgl/canvas are used.

**reused data / constants:** inferno and viridis colour-map stop values (matplotlib, cc0 public domain) and
the cool–warm stops from moreland's diverging map (free to use). standard textbook
methods (linear fe for 1d conduction, the thomas algorithm, central differences) implemented from scratch.

**ai-generated:** the c++ backend, frontend, tests, tools, example data generator and documentation were
written in an ai-assisted session with claude code (claude opus 5.5), directed by the candidate. the
session, prompts and every error the ai made are recorded in [ai_use_log.md](ai_use_log.md); commits from
that session carry a `Co-Authored-By: Claude` trailer.

**candidate's original work:** direction and acceptance of the work; the handwriting of the backpropagation
appendix (its content was provided by the assistant and copied by hand, see the ai-use log); and the edits
listed at the end of the ai-use log. (the screenshot walkthrough was captured by an ai-written
script, `scripts/capture_walkthrough.mjs`, driving the real app; a demo video, if added, is the candidate's.)

signed: Tamil Adhavan S K (RA2311003010896)  date: 2026-11-02
