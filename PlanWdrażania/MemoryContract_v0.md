# Memory Contract v0 (Etap 1)

## Założenia
- Architektura: `RV64`
- Tryb stronicowania: `Sv39`
- Rozmiar strony: `4 KiB` (`0x1000`)
- Strategia mapowania na start: `identity mapping` (`VA == PA`)
- Granice RAM (z DTB, runtime): `0x80000000 .. 0x88000000` (128 MiB)

## Symbole kernela (z `kernel.elf`)
- `_kernel_start = 0x80200000`
- `_text_start = 0x80200000`
- `_text_end = 0x80209658`
- `_rodata_start = 0x8020a000`
- `_rodata_end = 0x8020a864`
- `_data_start = 0x8020d000`
- `_data_end = 0x8020d000`
- `_bss_start = 0x8020d000`
- `_bss_end = 0x8020d0a0`
- `_kernel_end = 0x8020e000`
- `_stack_bottom = 0x8020e000`
- `_stack_top = 0x80212000`
- `_kernel_image_end = 0x80212000`

## Mapa pamięci v0 (VA=PA, zakresy półotwarte)

| Region | VA/PA start | VA/PA end | Flagi PTE (plan) | Źródło | Zarezerwowany |
|---|---:|---:|---|---|---|
| OpenSBI firmware | `0x80000000` | `0x80060000` | nie mapować w S-mode | log OpenSBI (`Firmware Base/Size`, Domain) | tak |
| Kernel `.text` | `0x80200000` | `0x8020a000` | `V,R,X` | `kernel/linker.ld` + symbole | tak |
| Kernel RO (`.rodata` + orphany RO) | `0x8020a000` | `0x8020d000` | `V,R` | sekcje ELF (`.rodata/.eh_frame/.srodata`) | tak |
| Kernel RW (`.data/.bss`) | `0x8020d000` | `0x8020e000` | `V,R,W` | `kernel/linker.ld` + symbole | tak |
| Kernel stack | `0x8020e000` | `0x80212000` | `V,R,W` | `kernel/linker.ld` | tak |
| DTB przekazany w `a1` | `0x82200000` | `align_up(0x82200000 + fdt_totalsize, 0x1000)` | `V,R` | runtime (`Domain0 Next Arg1`) | tak |
| UART NS16550A | `0x10000000` | `0x10000100` | `V,R,W` i `!X` | DTB/runtime | tak (MMIO) |
| ACLINT MTIMER | `0x02000000` | `0x02010000` | na razie nie mapować | OpenSBI Domain | tak (MMIO) |
| PLIC | `0x0c000000` | `0x0c600000` | etap późniejszy: `V,R,W` i `!X` | OpenSBI Domain | tak (MMIO) |
| Kandydat dla frame alloc | `0x80212000` | `0x88000000` | `V,R,W` (po mapowaniu) | RAM z DTB | nie (po odjęciu rezerwacji) |

## Niezmienniki (must-have)
- Frame allocator **nigdy** nie oddaje ramek z regionów: OpenSBI, kernel image, stack, DTB, MMIO.
- Wszystkie mapowania MMIO muszą mieć `!X`.
- Kod kernela (`.text`) pozostaje `RX`, dane/stos `RW`.
- `first_free_frame = align_up(_kernel_image_end, 0x1000) = 0x80212000`.
- Wolna pamięć to: `[first_free_frame, RAM_END)` minus wszystkie regiony zarezerwowane.

## Decyzja na koniec etapu 1
- Etap 2 zaczyna się od budowy listy `reserved regions` (na podstawie tabeli powyżej) i przekazania jej do alokatora ramek.
