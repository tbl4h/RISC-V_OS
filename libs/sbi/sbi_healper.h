#ifndef SBI_HEALPER_H
#define SBI_HEALPER_H   
#include "sbi.h"
#include "sbi_base.h"

static void require_extension(long ext, const char *name)
{
    struct sbiret ret = sbi_probe_extension(ext);

    if (ret.error < 0)
        panic("SBI probe_extension failed");

    if (ret.value == 0)
        panic(name);
}

#endif /* SBI_HEALPER_H */