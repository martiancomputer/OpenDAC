# Agent instructions

- Preserve the F401CC hardware target, 25 MHz HSE assumption, and PB12/PB13/PB15
  I²S wiring unless the user supplies a revised schematic.
- Keep USB Audio, rate correction, I²S DMA, diagnostics, and DFU as separate
  modules. Avoid heap allocation, work or logging that blocks in interrupts,
  and DMA buffer writes without checking producer/consumer distance.
- Treat `ref/`, `prev/`, `instructions.md`, and `research.md` as local-only
  evidence. They are intentionally ignored; do not add them to Git.
- Use only the official STM32CubeF4 submodule for HAL, CMSIS, and USB core.
  Initialize its HAL and CMSIS device nested submodules. Do not vendor copied
  ST files.
- Run `make -j4` and `make test` after changes. Hardware/audio claims require
  physical testing; a successful compile is not enumeration proof.
- Do not call a newly advertised rate hardware-validated until physical
  DMA-clock measurements and USB host tests support it. Keep the distinction
  explicit in README. Do not convert producer pressure into an I²S restart
  loop.
- Keep documentation, logs, examples, commit messages, and staged changes free
  of usernames, home paths, hostnames, serial numbers, MACs, private IPs,
  personal email, credentials, tokens, and other identifying information.
  Use placeholders. Never commit `.env`, keys, session data, local scratch,
  or authentication dumps.
- Before every commit inspect `git diff --cached` and scan staged content for
  secrets/identifiers; before every push inspect the entire outgoing range.
  Stop and sanitize if anything questionable appears.
