#include <stdint.h>
#include <libfdt.h>
#include <sbi/sbi.h>
#include <sbi/sbi_healper.h>
#include <sbi/sbi_base.h>
#include <sbi/sbi_ performance monitoring unit extension.h>
#include <sbi/sbi_hart state management extension.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_reference_extension.h>
#include <sbi/sbi_system reset extension.h>
#include <sbi/sbi_timer.h>
#include <panic.h>

void kmain(uint64_t hartid, void *dtb)
{
    if (hartid != 0) {
        while (1)
            asm volatile("wfi");
    }

    struct sbiret spec = sbi_get_spec_version();
    if (spec.error < 0)
        panic("SBI BASE niedostępne");

    require_extension(SBI_EXT_TIME,   "Brak TIME");
    require_extension(SBI_EXT_IPI,    "Brak IPI");
    require_extension(SBI_EXT_RFENCE, "Brak RFENCE");
    require_extension(SBI_EXT_HSM,    "Brak HSM");
    require_extension(SBI_EXT_SRST,   "Brak SRST");
    require_extension(SBI_EXT_PMU,    "Brak PMU");

    struct sbiret hs = sbi_hart_get_status(hartid);
    if (hs.error < 0 || hs.value != SBI_HSM_STATE_STARTED)
        panic("Boot hart nie jest STARTED");

    if (!dtb)
        panic("Brak DTB");

    if (fdt_magic(dtb) != FDT_MAGIC)
        panic("Nieprawidłowy DTB");

    if (fdt_version(dtb) < 17)
        panic("Za stary DTB");

    const void *fdt = dtb;
}
