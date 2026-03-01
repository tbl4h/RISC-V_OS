# Szablon dumpu diagnostycznego (`RAM/RESERVED/FREE`)

## Format docelowy logu (UART)

```text
[mm] map dump begin
[mm] page_size=0x0000000000001000
[mm] ram_count=<N>
[mm] ram[00] 0x<start>..0x<end> pages=<p> src=<source>
[mm] ram[01] 0x<start>..0x<end> pages=<p> src=<source>

[mm] reserved_count=<N>
[mm] res[00] 0x<start>..0x<end> pages=<p> src=<source>
[mm] res[01] 0x<start>..0x<end> pages=<p> src=<source>

[mm] free_count=<N>
[mm] free[00] 0x<start>..0x<end> pages=<p>
[mm] free[01] 0x<start>..0x<end> pages=<p>

[mm] totals: ram_pages=<R> reserved_pages=<Z> free_pages=<F>
[mm] check: R == Z + F -> <OK|FAIL>
[mm] first_free_frame=0x<addr> -> <OK|FAIL>
[mm] overlap_free_reserved=<0|1> -> <OK|FAIL>
[mm] map dump end
```

## Konwencje
- Przedziały wypisuj jako półotwarte: `start..end` oznacza `[start, end)`.
- Wszystkie adresy drukuj jako `hex u64` z prefiksem `0x`.
- `pages = (end - start) / 0x1000`.
- `source` przykładowo: `dtb-memory`, `kernel`, `dtb`, `uart-mmio`, `plic-mmio`, `opensbi`.

## Minimalny zestaw walidacji w dumpie
- `R == Z + F` po wyrównaniu do stron.
- Brak przecięć `FREE` z `RESERVED`.
- `first_free_frame` należy do któregoś zakresu `FREE`.
- Każdy zakres ma `start < end` i wyrównanie do `0x1000`.

## Krótki przykład (liczby orientacyjne)

```text
[mm] map dump begin
[mm] page_size=0x0000000000001000
[mm] ram_count=1
[mm] ram[00] 0x0000000080000000..0x0000000088000000 pages=32768 src=dtb-memory
[mm] reserved_count=5
[mm] res[00] 0x0000000080000000..0x0000000080060000 pages=96 src=opensbi
[mm] res[01] 0x0000000080200000..0x0000000080212000 pages=18 src=kernel
[mm] res[02] 0x0000000082200000..0x0000000082202000 pages=2 src=dtb
[mm] res[03] 0x0000000010000000..0x0000000010001000 pages=1 src=uart-mmio
[mm] res[04] 0x000000000c000000..0x000000000c600000 pages=1536 src=plic-mmio
[mm] free_count=2
[mm] free[00] 0x0000000080060000..0x0000000080200000 pages=416
[mm] free[01] 0x0000000080212000..0x0000000088000000 pages=32238
[mm] totals: ram_pages=32768 reserved_pages=114 reserved_pages_in_ram=114 free_pages=32654
[mm] check: R == Z + F -> OK
[mm] first_free_frame=0x0000000080212000 -> OK
[mm] overlap_free_reserved=0 -> OK
[mm] map dump end
```
