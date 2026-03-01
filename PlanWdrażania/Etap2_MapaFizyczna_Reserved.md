# Etap 2 — Mapa fizycznej pamięci i lista regionów zarezerwowanych

## Uwaga o `kernel.bin` vs `kernel.elf`
- `kernel.bin` nie zmienia adresów mapowania — adresy wynikają z linkowania (`kernel/linker.ld`), nie z formatu pliku.
- `kernel.bin` to surowy obraz bez symboli i bez metadanych ELF; dlatego do planowania pamięci nadal używamy symboli z `kernel.elf` (lub `extern` w C).
- Sekcje `NOBITS` (`.bss`, `.stack`) mogą nie mieć danych w `kernel.bin`, ale **muszą** pozostać zarezerwowane w mapie.

## Cel etapu 2
- Zbudować jedną, posortowaną listę regionów:
  - `RAM regions` (kandydaci na alokację),
  - `reserved regions` (nietykalne),
  - `free regions` (wynik odejmowania).

## Wejścia (źródła prawdy)
- Z DTB:
  - `dtb_memory_regions(...)` — fizyczne zakresy RAM.
  - `dtb_reserved_memory_regions(...)` — rezerwacje z `/reserved-memory` (jeśli istnieją).
  - UART/PLIC/ACLINT/MMIO z `reg` (już wykrywane przez obecny kod DTB).
  - Adres DTB z argumentu `a1` (już przekazywany do `kmain`), rozmiar przez `fdt_totalsize`.
- Z linkera/symboli:
  - `_kernel_start`, `_kernel_end`, `_stack_bottom`, `_stack_top`, `_kernel_image_end`.
- Z platformy (QEMU virt + OpenSBI):
  - region firmware OpenSBI i ewentualne dodatkowe rezerwacje runtime.

## Format regionu (kanoniczny)
- Każdy region opisujemy jako:
  - `start` (włącznie), `end` (wyłącznie),
  - `kind` (`RAM`, `RESERVED`, `MMIO`, `FREE`),
  - `source` (`dtb-memory`, `kernel`, `opensbi`, `uart`, ...).
- Normalizacja:
  - `start = align_down(start, 0x1000)`,
  - `end = align_up(end, 0x1000)`,
  - odrzucenie pustych (`start >= end`).

## Kolejność budowy mapy
1. Pobierz wszystkie `RAM regions` z DTB.
2. Zbuduj `reserved regions` z:
   - obrazu kernela (`[_kernel_start, _kernel_image_end)`),
   - DTB (`[dtb_base, dtb_base + fdt_totalsize)`),
   - MMIO (UART, PLIC, ACLINT, itd.),
   - `/reserved-memory` z DTB,
   - regionu OpenSBI (jeśli leży w RAM widocznym dla S-mode).
3. Posortuj `reserved regions` po `start`.
4. Scal nakładające/sąsiadujące regiony rezerwacji.
5. Dla każdego regionu RAM odejmij scaloną listę rezerwacji.
6. Wynik zapisz jako `free regions`.

## Reguły odejmowania (RAM - RESERVED)
- Pracuj na przedziałach półotwartych `[start, end)`.
- Przypadki:
  - brak przecięcia → cały fragment zostaje `FREE`,
  - pełne pokrycie → nic nie zostaje,
  - przecięcie lewe/prawe/środkowe → split na 1–2 fragmenty.

## Kryteria poprawności (DoD etapu 2)
- Żaden `FREE` nie przecina żadnego `RESERVED`.
- Wszystkie `FREE` są wyrównane do `4 KiB`.
- `first_free_frame` należy do któregoś `FREE`.
- Sumarycznie:
  - `sum(FREE) + sum(RESERVED within RAM) == sum(RAM)` (po normalizacji stronowej).
- Jest czytelny dump diagnostyczny z listami `RAM/RESERVED/FREE`.

## Co przygotowujemy pod etap 3
- Wejście dla frame allocatora:
  - albo lista `FREE` (zakresy),
  - albo bezpośrednio zmaterializowana bitmapa ramek oparta na `FREE`.
