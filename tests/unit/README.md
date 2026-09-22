# Unit tests (QtTest) — diagnosis suite

Tests are written to **expose** defects in production code; failing tests are
expected until the corresponding bug is fixed. Production sources are compiled
read-only from the repository; nothing outside `tests/unit/` is modified.
Databases used by tests live in `QTemporaryDir`s (fresh DB from
`database/migrations/init.sql`, or a *copy* of `sqlite.db`).

| executable | covers |
|---|---|
| `tst_comptr` | `decklink/com_ptr.h` refcount semantics; `DeckLinkInputDevice` ctor/dtor IDeckLink refcount (mock IDeckLink) |
| `tst_database` | `runMigrations()` (verbatim replica of `main.cpp`), `settings` row growth, `SettingsPage` save SQL, Qt vs Flask settings read, `;`-split migration parser, `DatabaseManager` error reporting, all `api/*` controllers vs schema |
| `tst_widget_lifetime` | offscreen create/show/close ×100 of dialogs/pages; production `exec()` paths that allocate dialogs with `new ...(this)` |

## Build & run

```sh
cd /home/brainwave/QT-C-VMS

# plain
cmake -S tests/unit -B build-tests
make -C build-tests -j3
cd build-tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -j2; cd ..

# ASan + UBSan (+ LSan)
cmake -S tests/unit -B build-tests-asan -DUNIT_SANITIZE=ON
make -C build-tests-asan -j3
cd build-tests-asan && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -j2; cd ..
# (ctest sets ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 and
#  LSAN_OPTIONS=suppressions=tests/unit/lsan.supp)

# everything, with raw logs saved to tests/unit/results/
tests/unit/run_tests.sh all      # or: plain | asan
```

Single test / single function:
```sh
QT_QPA_PLATFORM=offscreen build-tests/tst_database controllers:Patient
QT_QPA_PLATFORM=offscreen build-tests/tst_widget_lifetime path_SurgeryDetailsPage_editPatient
```

## Files
- `CMakeLists.txt` – standalone project; `-DUNIT_SANITIZE=ON` for ASan/UBSan.
- `common/TestSupport.hpp` – DB copy helper, verbatim `runMigrations()` replica, RSS reader, log capture.
- `common/ModalCloser.hpp` – timer that rejects `QApplication::activeModalWidget()` so `exec()` paths can be driven.
- `lsan.supp` – suppressions for system/Qt-plugin noise only (Qt core libs are
  intentionally not suppressed: LSan matches any frame, which would hide app leaks).
- `summarize_lsan.py` – groups LSan reports by application frames.
- `run_tests.sh` – builds both variants, runs, stores logs in `results/`.
- `results/` – raw outputs (`<test>.plain.txt`, `<test>.asan.txt`, `ctest_*.txt`, `lsan_summary.txt`).

Notes: the ASan run with full unwinding takes ~5 min. RSS numbers come from
`/proc/self/statm` and are informational (not asserted).
