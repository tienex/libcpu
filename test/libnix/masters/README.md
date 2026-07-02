# Vendored BSD `syscalls.master` files

Historical BSD `syscalls.master` sources, vendored for the emulator's
syscall-version-gating feature. Each file is the upstream plain-text
BSD `syscalls.master` (RCS `$NetBSD$` / `$OpenBSD$` id line followed by
the `STD`/`OBSOL`/`UNIMPL` syscall table).

- Fetched on: **2026-07-02**
- Fetch tool: `curl -fsS --max-time 30`
- Layout: `<os>/<version>/syscalls.master`

Each file was verified after download: its first line contains the
expected `$NetBSD` / `$OpenBSD` RCS marker and the body contains `STD`
entries. No file was fabricated; failed fetches are logged under
"Skipped" below.

## NetBSD

Fetched by CVS tag from cvsweb (checkout, plain text):

```
https://cvsweb.netbsd.org/bsdweb.cgi/~checkout~/src/sys/kern/syscalls.master?only_with_tag=<TAG>&content-type=text/plain
```

HEAD (10.1) uses the same URL with no `only_with_tag` parameter.

| version | CVS tag                 | file rev  |
|---------|-------------------------|-----------|
| 1.0     | netbsd-1-0              | 1.22      |
| 1.1     | netbsd-1-1              | 1.29      |
| 1.2     | netbsd-1-2              | 1.32.4.1  |
| 1.3     | netbsd-1-3              | 1.63.2.1  |
| 1.4     | netbsd-1-4              | 1.90.2.2  |
| 1.5     | netbsd-1-5              | 1.101     |
| 1.6     | netbsd-1-6              | 1.111     |
| 2.0     | netbsd-2-0              | 1.138     |
| 3.0     | netbsd-3-0              | 1.145     |
| 4.0     | netbsd-4-0              | 1.160     |
| 5.0     | netbsd-5-0              | 1.211     |
| 6.0     | netbsd-6-0              | 1.254     |
| 7.0     | netbsd-7-0              | 1.270.2.1 |
| 8.0     | netbsd-8-0-RELEASE      | 1.286     |
| 9.0     | netbsd-9                | 1.295     |
| 10.0    | netbsd-10               | 1.309     |
| 10.1    | HEAD (no tag)           | 1.316     |

Note: the requested tag `netbsd-8-0` did not exist (fetch failed); the
release tag `netbsd-8-0-RELEASE` was used instead for version 8.0.

## OpenBSD

The OpenBSD GitHub mirror (`github.com/openbsd/src`) exposes **no**
release tags (`git ls-remote --tags` and the tags API both return
empty), so the raw.githubusercontent.com fallback is unavailable.

Instead, files were fetched by CVS **revision** number from cvsweb:

```
https://cvsweb.openbsd.org/checkout/src/sys/kern/syscalls.master?rev=1.<N>
```

The full revision->date map (revisions 1.1 .. 1.272, dated
1995-10-18 .. 2026-06-02) was read once from the cvsweb log page
(`https://cvsweb.openbsd.org/log/src/sys/kern/syscalls.master`).
For each target release, the newest revision whose commit date is
`<=` the release's approximate date was selected.

| version | approx release | chosen rev | rev commit date |
|---------|----------------|------------|-----------------|
| 2.0     | 1996-10        | 1.12       | 1996-10-29      |
| 2.6     | 1999-12        | 1.37       | 1999-06-07      |
| 3.0     | 2001-12        | 1.47       | 2001-06-26      |
| 3.6     | 2004-11        | 1.76       | 2004-07-15      |
| 4.0     | 2006-11        | 1.86       | 2006-09-22      |
| 4.6     | 2009-10        | 1.93       | 2009-06-03      |
| 5.0     | 2011-11        | 1.119      | 2011-10-15      |
| 5.5     | 2014-05        | 1.138      | 2014-02-09      |
| 5.9     | 2016-03        | 1.169      | 2016-03-30      |
| 6.0     | 2016-09        | 1.174      | 2016-09-04      |
| 6.5     | 2019-04        | 1.189      | 2019-01-11      |
| 7.0     | 2021-10        | 1.219      | 2021-10-27      |
| 7.4     | 2023-10        | 1.250      | 2023-08-20      |
| 7.9     | 2026-05        | 1.271      | 2026-03-08      |

Notes on selection:
- The chosen revision is the last one committed on/before the release
  month, i.e. the source state closest to what shipped in that release.
- 5.9 (1.169, 2016-03-30) and 7.0 (1.219, 2021-10-27) fall a few days
  after the nominal month boundary but are the correct pre-release
  revisions (the next revision is materially later).
- 7.9 uses rev 1.271 (2026-03-08); the newer 1.272 (2026-06-02) post-
  dates the ~May 2026 release window.

## Skipped / failed

- **NetBSD tag `netbsd-8-0`** — 404 / empty; substituted
  `netbsd-8-0-RELEASE` (succeeded), so version 8.0 is present.
- No other fetch failed. All 17 NetBSD versions and all 14 OpenBSD
  milestones were vendored successfully.

## Totals

- NetBSD: 17 versions (1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.0, 3.0,
  4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 10.1)
- OpenBSD: 14 versions (2.0, 2.6, 3.0, 3.6, 4.0, 4.6, 5.0, 5.5, 5.9,
  6.0, 6.5, 7.0, 7.4, 7.9)
