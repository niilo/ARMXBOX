#include <stdint.h>
__attribute__((visibility("default"))) uint32_t
armxbox_host_mapping_probe(uint32_t value)
{
    return value ^ UINT32_C(0xa5c36f19);
}
