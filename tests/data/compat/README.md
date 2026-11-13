# Compatibility fixtures (FK-014, U028)

Files written by earlier releases' own code, for `tests/core/test_compat.cpp`. Synthetic content: invented paths
under `/Users/example`, invented hashes, no image data. Do not edit by hand — regenerate with
`tools/compat/make-fixtures.sh` (it builds the core of each release from its tag and `tools/compat/fixtures.cpp`).

| Folder | Written by | Files |
| --- | --- | --- |
| `v0.5.0/` | release 0.5.0 | `project.fkproj`, `export_01.fk.json` (export record / sidecar), `jig.fktemplate` |
| `v0.6.0-alpha.1/` | release 0.6.0-alpha.1 | the same three |
| `rollback-v0.6.0-alpha.1/` | `before.fkproj`: 0.7.0-alpha.15 (all fields added since 0.6.0-alpha.1 filled); `after.fkproj`: the same project opened and saved by 0.6.0-alpha.1 | what a rollback keeps and loses (`docs/project-format.md` §Compatibility) |
