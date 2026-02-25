#include <stdint.h>
#include <sbi/sbi_base.h>


void kmain(uint64_t hartid, void *dtb)
{
    sbi_get_impl_id();

    
}
