# BearSSL for ntrdps (experiment M12)

TL;DR: the TLS stack for the modern-client experiment (`docs/TASKS.md` M12).
**Not in the release binaries.** MIT, so it may live in the core.

- Upstream: BearSSL, https://bearssl.org/, git `7bea48e` (2026-04-06). MIT (`LICENSE.txt`).
  The same commit as the IE 1.5 client port in `$NTOS/nt31ie`.
- A pruned copy: the 53 source files for ONE suite,
  TLS 1.2 `ECDHE_ECDSA_WITH_AES_128_GCM_SHA256` (P-256), lean `i15` big-integers.

## Layout

- `src/`     the `.c` files, plus `inner.h` and upstream's `config.h` (all knobs auto-detect).
- `inc/`     the public headers.
- `compat/stdint.h`  a `<stdint.h>` for Visual C++ 4.2 (cl 10.20), which ships none.

## Build (Intel / NT 4.0, cl 10.20)

- include: `-Iinc -Isrc -Icompat`
- flags:   `-Dinline=__inline -DBR_USE_WIN32_RAND=0`
- cl 10.20 has `__int64`, so the files are **unmodified** upstream.
- For NT 3.1 / RISC (the 1993 compilers, no 64-bit) the upstream files would need
  the port's 64-bit-split tweaks instead (see `$NTOS/nt31ie`); not done here (M12.7).
- `BR_USE_WIN32_RAND=0`: ntrdps injects entropy itself (M12.3), so the TLS code
  does not depend on NT 4.0's CryptoAPI and can run on older NT later.

## Server context (manual init, not the canned profiles)

The canned `br_ssl_server_init_minf2g` pulls BearSSL's heavier `i31` big-integers.
ntrdps sets the context by hand (as the client port's `w32ssl.c` does), choosing the
lean impls: `br_ec_p256_m15`, `br_ecdsa_i15_sign_asn1`, and GCM via `br_aes_ct_ctr`
/ `br_ghash_ctmul32`. See `src/tls/tls.c` (M12.3).
