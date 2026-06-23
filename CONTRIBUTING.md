# Contributing to HPSATVIEWS

Thanks for your interest in contributing! This project is developed at the
Laboratorio Nacional de Observación de la Tierra (LANOT), UNAM.

## Reporting bugs or problems

Please open a [GitHub Issue](https://github.com/asierra/hpsatviews/issues)
and include:

- The `hpsv` version (`hpsv --version`) and how it was built (`make` vs.
  `make DEBUG=1`).
- The exact command you ran.
- The full output with `-v` (verbose mode).
- Your OS/distribution and relevant dependency versions
  (`gdal-config --version`, `nc-config --version` if available).
- If possible, which GOES product/channel triggers the issue. Full data
  files usually aren't necessary — the structure from `ncdump -h file.nc`
  is often enough to reproduce the problem.

## Proposing changes

1. Fork the repository and create a branch for your change.
2. Build and test locally:
   ```bash
   make
   tests/run_all_tests.sh
   ```
3. Follow the existing conventions: C11, `snake_case` for functions,
   `PascalCase` for types, explicit `int` return codes (`0` = success),
   no global state — see `CLAUDE.md` for the full architecture and
   conventions reference.
4. If you add a new RGB mode or CLI option, update the help text in both
   `include/help_en.h` and `include/help_es.h`, and document it in
   `README.md`.
5. Open a pull request describing the motivation and the testing you did.
   CI (GitHub Actions) runs the full test suite automatically on every PR.

## Getting support

For questions beyond a bug report, open a
[GitHub Issue](https://github.com/asierra/hpsatviews/issues) or contact
Alejandro Aguilar Sierra (asierra@unam.mx), Laboratorio Nacional de
Observación de la Tierra, UNAM.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md). By
participating, you are expected to uphold it.
